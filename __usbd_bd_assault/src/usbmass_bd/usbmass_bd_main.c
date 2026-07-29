#define MAJOR_VER 1
#define MINOR_VER 1

#include "include/scsi.h"
#include <bdm.h>
#include <ioman.h>
#include <io_common.h>
#include <irx.h>
#include <loadcore.h>
#include <stdio.h>
#include <sysclib.h>
#include <thbase.h>

// #define DEBUG  //comment out this line when not debugging
#include "include/module_debug.h"

/* FatFs挂载与USB枚举均为异步；等到mass上出现POPS/或超时后再返回给POPStarter */
#define USB_POPS_WAIT_TOTAL_US (30000000)
#define USB_POPS_WAIT_STEP_US  (50000)
#define USB_MOUNT_SETTLE_US    (100000)
/* 前几秒只观察、不重挂，避免打断正常枚举/分区/FatFs */
#define USB_REMOUNT_GRACE_US   (8000000)
#define USB_BD_MAX             20
#define USB_MASS_MAX           10

extern int usb_mass_init(void);

static void usb_make_mass_path(char *path, int unit, const char *name)
{
    int i;

    path[0] = 'm';
    path[1] = 'a';
    path[2] = 's';
    path[3] = 's';
    path[4] = '0' + unit;
    path[5] = ':';
    path[6] = '/';
    for (i = 0; name[i]; i++)
        path[7 + i] = name[i];
    path[7 + i] = '\0';
}

/* 打印当前BDM块设备快照，便于判断USB是否已connect、分区是否已切出 */
static void usb_log_bd_snapshot(const char *tag)
{
    struct block_device *bds[USB_BD_MAX];
    int i;
    int n;

    memset(bds, 0, sizeof(bds));
    bdm_get_bd(bds, USB_BD_MAX);

    n = 0;
    for (i = 0; i < USB_BD_MAX; i++) {
        if (!bds[i] || !bds[i]->name)
            continue;
        M_PRINTF("状态[%s]: bd%d name=%s dev=%u par=%u id=%02X sec=%08x%08x sz=%u\n",
                 tag, n, bds[i]->name, bds[i]->devNr, bds[i]->parNr, bds[i]->parId,
                 U64_2XU32(&bds[i]->sectorCount), bds[i]->sectorSize);
        n++;
    }
    if (n == 0)
        M_PRINTF("状态[%s]: 尚无块设备\n", tag);
}

static int usb_count_named_parts(const char *name)
{
    struct block_device *bds[USB_BD_MAX];
    int i;
    int parts;

    memset(bds, 0, sizeof(bds));
    bdm_get_bd(bds, USB_BD_MAX);
    parts = 0;
    for (i = 0; i < USB_BD_MAX; i++) {
        if (bds[i] && bds[i]->name && strcmp(bds[i]->name, name) == 0 && bds[i]->parNr != 0)
            parts++;
    }
    return parts;
}

static int usb_remount_named_wholes(const char *name)
{
    struct block_device *bds[USB_BD_MAX];
    struct block_device *wholes[USB_BD_MAX];
    int n;
    int i;

    memset(bds, 0, sizeof(bds));
    bdm_get_bd(bds, USB_BD_MAX);

    n = 0;
    for (i = 0; i < USB_BD_MAX; i++) {
        if (bds[i] && bds[i]->name && strcmp(bds[i]->name, name) == 0 && bds[i]->parNr == 0)
            wholes[n++] = bds[i];
    }
    if (n <= 0)
        return -1;

    M_PRINTF("重挂: 断开/重连 %s 整盘 x%d，再次触发分区+FatFs\n", name, n);
    for (i = 0; i < n; i++)
        bdm_disconnect_bd(wholes[i]);
    DelayThread(USB_MOUNT_SETTLE_US);
    for (i = 0; i < n; i++)
        bdm_connect_bd(wholes[i]);
    DelayThread(USB_MOUNT_SETTLE_US);
    return 0;
}

static int usb_mass_ready(int unit)
{
    char path[12];
    int fd;

    if (unit < 0 || unit >= USB_MASS_MAX)
        return 0;

    if (unit == 0) {
        fd = dopen("mass:/", FIO_O_RDONLY);
        if (fd >= 0) {
            dclose(fd);
            return 1;
        }
        fd = dopen("mass0:/", FIO_O_RDONLY);
        if (fd >= 0) {
            dclose(fd);
            return 1;
        }
        return 0;
    }

    usb_make_mass_path(path, unit, "");
    fd = dopen(path, FIO_O_RDONLY);
    if (fd >= 0) {
        dclose(fd);
        return 1;
    }
    return 0;
}

static int usb_mass_has_pops(int unit)
{
    char path[16];
    int fd;

    if (unit < 0 || unit >= USB_MASS_MAX)
        return 0;

    if (unit == 0) {
        fd = dopen("mass:/POPS", FIO_O_RDONLY);
        if (fd >= 0) {
            dclose(fd);
            return 1;
        }
        fd = dopen("mass0:/POPS", FIO_O_RDONLY);
        if (fd >= 0) {
            dclose(fd);
            return 1;
        }
        return 0;
    }

    usb_make_mass_path(path, unit, "POPS");
    fd = dopen(path, FIO_O_RDONLY);
    if (fd >= 0) {
        dclose(fd);
        return 1;
    }
    return 0;
}

/* 将含POPS的USB分区提升到mass0，避免POPStarter只认mass0 */
static int usb_promote_usb_pops_to_mass0(void)
{
    struct block_device *bds[USB_BD_MAX];
    struct block_device *usb_parts[USB_BD_MAX];
    struct block_device *pops_bd;
    int n;
    int i;
    int j;

    memset(bds, 0, sizeof(bds));
    bdm_get_bd(bds, USB_BD_MAX);

    n = 0;
    for (i = 0; i < USB_BD_MAX; i++) {
        if (bds[i] && bds[i]->name && strcmp(bds[i]->name, "usb") == 0 && bds[i]->parNr != 0)
            usb_parts[n++] = bds[i];
    }

    if (n <= 0)
        return -1;
    if (n == 1)
        return usb_mass_has_pops(0) ? 0 : -1;

    M_PRINTF("提升: 多USB分区，尝试把含POPS的卷放到mass0 (parts=%d)\n", n);
    for (i = 0; i < n; i++)
        bdm_disconnect_bd(usb_parts[i]);
    DelayThread(USB_MOUNT_SETTLE_US);

    pops_bd = NULL;
    for (i = 0; i < n; i++) {
        bdm_connect_bd(usb_parts[i]);
        for (j = 0; j < 40; j++) {
            DelayThread(USB_POPS_WAIT_STEP_US);
            if (usb_mass_has_pops(0)) {
                pops_bd = usb_parts[i];
                break;
            }
        }
        if (pops_bd)
            break;
        bdm_disconnect_bd(usb_parts[i]);
        DelayThread(USB_MOUNT_SETTLE_US);
    }

    if (!pops_bd) {
        for (i = 0; i < n; i++)
            bdm_connect_bd(usb_parts[i]);
        DelayThread(USB_MOUNT_SETTLE_US);
        return -1;
    }

    for (i = 0; i < n; i++) {
        if (usb_parts[i] != pops_bd)
            bdm_connect_bd(usb_parts[i]);
    }
    DelayThread(USB_MOUNT_SETTLE_US);
    return 0;
}

/* 等待mass上出现POPS；必要时重挂USB整盘并尝试提升到mass0 */
static int usb_wait_pops_ready(void)
{
    unsigned int waited;
    unsigned int last_log;
    int unit;
    int found;
    int promoted;
    int parts;
    int remount_cd;
    int mass_any;
    int mass_bits;
    int pops_bits;

    M_PRINTF("等待: 开始等mass:/POPS（最长%u秒）\n", USB_POPS_WAIT_TOTAL_US / 1000000);
    DelayThread(USB_MOUNT_SETTLE_US * 2);
    usb_log_bd_snapshot("wait-start");

    promoted = 0;
    remount_cd = 0;
    last_log = 0;
    for (waited = 0; waited < USB_POPS_WAIT_TOTAL_US; waited += USB_POPS_WAIT_STEP_US) {
        parts = usb_count_named_parts("usb");
        mass_any = 0;
        mass_bits = 0;
        pops_bits = 0;
        for (unit = 0; unit < USB_MASS_MAX; unit++) {
            if (usb_mass_ready(unit)) {
                mass_any = 1;
                mass_bits |= (1 << unit);
            }
            if (usb_mass_has_pops(unit))
                pops_bits |= (1 << unit);
        }

        if ((waited - last_log) >= 1000000) {
            M_PRINTF("等待: t=%us usb_parts=%d mass_mask=0x%X pops_mask=0x%X\n",
                     waited / 1000000, parts, mass_bits, pops_bits);
            usb_log_bd_snapshot("wait");
            assault_mc_log_flush();
            last_log = waited;
        }

        /* 宽限期后再考虑重挂；且仅在完全没有mass时重挂，避免打断已在挂载的盘 */
        if (!mass_any) {
            if (waited >= USB_REMOUNT_GRACE_US && remount_cd <= 0) {
                if (usb_remount_named_wholes("usb") == 0)
                    M_PRINTF("等待: 宽限期后重挂USB整盘\n");
                remount_cd = 2000000 / USB_POPS_WAIT_STEP_US; /* 重挂间隔拉到约2秒 */
            } else if (remount_cd > 0) {
                remount_cd--;
            }
            DelayThread(USB_POPS_WAIT_STEP_US);
            continue;
        }
        remount_cd = 0;

        if (usb_mass_has_pops(0)) {
            M_PRINTF("等待: 已在mass0找到POPS（耗时%uus）\n", waited);
            assault_mc_log_flush();
            return 0;
        }

        found = -1;
        for (unit = 1; unit < USB_MASS_MAX; unit++) {
            if (usb_mass_has_pops(unit)) {
                found = unit;
                break;
            }
        }

        if (found > 0 && !promoted) {
            M_PRINTF("等待: POPS在mass%d，尝试提升到mass0\n", found);
            if (usb_promote_usb_pops_to_mass0() == 0 && usb_mass_has_pops(0)) {
                M_PRINTF("等待: 提升成功，mass0已有POPS\n");
                assault_mc_log_flush();
                return 0;
            }
            promoted = 1;
            M_PRINTF("等待: 提升失败，继续等待\n");
        }

        DelayThread(USB_POPS_WAIT_STEP_US);
    }

    M_PRINTF("等待: 超时仍未找到mass:/POPS\n");
    usb_log_bd_snapshot("wait-timeout");
    for (unit = 0; unit < USB_MASS_MAX; unit++) {
        M_PRINTF("等待: mass%d ready=%d pops=%d\n", unit, usb_mass_ready(unit), usb_mass_has_pops(unit));
    }
    assault_mc_log_flush();
    return -1;
}

int usbmass_bd_start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    assault_mc_log_init("USBHDFSD.LOG");
    M_PRINTF("USBD ASSAULT: starting USBMASS Side\n");
    M_PRINTF("日志: mc?:POPSTARTER/USBHDFSD.LOG（内存缓冲后刷写）\n");

    if (scsi_init() != 0) {
        M_PRINTF("ERROR: initializing SCSI driver!\n");
        assault_mc_log_flush();
        return MODULE_NO_RESIDENT_END;
    }

    if (usb_mass_init() != 0) {
        M_PRINTF("ERROR: initializing USB driver!\n");
        assault_mc_log_flush();
        return MODULE_NO_RESIDENT_END;
    }

    /* 驱动已注册；在此阻塞直到POPS可见，避免POPStarter过早探测失败 */
    if (usb_wait_pops_ready() != 0)
        M_PRINTF("警告: POPS未就绪，模块仍驻留，交由上层继续尝试\n");
    else
        M_PRINTF("就绪: 可向POPStarter返回\n");

    assault_mc_log_flush();
    return MODULE_RESIDENT_END;
}
