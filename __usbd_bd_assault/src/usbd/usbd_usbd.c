/*
 * BDM HDD Assault entry (POPStarter usbhdfsd.irx slot).
 *
 * DEV9 and ATAD are linked into this IRX. Call their start functions directly
 * so ATA registers with BDM (bdm_connect_bd) and FatFs can mount mass:.
 *
 * Requires bdm_assault.irx already loaded as usbd.irx (provides BDM + mass).
 */

#include <irx.h>
#include <loadcore.h>
#include <stdio.h>
#include <sysclib.h>
#include <ioman.h>
#include <io_common.h>
#include <thbase.h>
#include <bdm.h>

#define MODNAME "bdm_hdd"
IRX_ID(MODNAME, 1, 1);

/* FatFs挂载在BDM线程异步完成；等到mass0出现POPS/或超时 */
#define BDM_HDD_POPS_WAIT_TOTAL_US (30000000)
#define BDM_HDD_POPS_WAIT_STEP_US  (50000)
#define BDM_HDD_MOUNT_SETTLE_US    (100000)
#define BDM_HDD_BD_MAX             20
#define BDM_HDD_MASS_MAX           10

extern int dev9_start(int argc, char *argv[]);
extern int atad_start(int argc, char *argv[]);

static void bdm_hdd_make_mass_path(char *path, int unit, const char *name)
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

/* 踢掉非ATA块设备（如usb），避免抢占mass；可在等待循环中反复调用 */
static void bdm_hdd_disconnect_non_ata(void)
{
    struct block_device *bds[BDM_HDD_BD_MAX];
    int i;

    memset(bds, 0, sizeof(bds));
    bdm_get_bd(bds, BDM_HDD_BD_MAX);
    for (i = 0; i < BDM_HDD_BD_MAX; i++) {
        if (bds[i] && bds[i]->name && strcmp(bds[i]->name, "ata") != 0)
            bdm_disconnect_bd(bds[i]);
    }
}

/* 重挂ATA整盘以再次触发MBR/GPT+FatFs（挂载失败时在超时前重试） */
static int bdm_hdd_remount_ata_wholes(void)
{
    struct block_device *bds[BDM_HDD_BD_MAX];
    struct block_device *wholes[BDM_HDD_BD_MAX];
    int n;
    int i;

    memset(bds, 0, sizeof(bds));
    bdm_get_bd(bds, BDM_HDD_BD_MAX);

    n = 0;
    for (i = 0; i < BDM_HDD_BD_MAX; i++) {
        if (bds[i] && bds[i]->name && strcmp(bds[i]->name, "ata") == 0 && bds[i]->parNr == 0)
            wholes[n++] = bds[i];
    }
    if (n <= 0)
        return -1;

    for (i = 0; i < n; i++)
        bdm_disconnect_bd(wholes[i]);
    DelayThread(BDM_HDD_MOUNT_SETTLE_US);

    for (i = 0; i < n; i++)
        bdm_connect_bd(wholes[i]);
    DelayThread(BDM_HDD_MOUNT_SETTLE_US);
    return 0;
}

/* 统计当前ata分区数量 */
static int bdm_hdd_count_ata_parts(void)
{
    struct block_device *bds[BDM_HDD_BD_MAX];
    int i;
    int parts;

    memset(bds, 0, sizeof(bds));
    bdm_get_bd(bds, BDM_HDD_BD_MAX);
    parts = 0;
    for (i = 0; i < BDM_HDD_BD_MAX; i++) {
        if (bds[i] && bds[i]->name && strcmp(bds[i]->name, "ata") == 0 && bds[i]->parNr != 0)
            parts++;
    }
    return parts;
}

/* 探测指定mass单元根目录是否可打开 */
static int bdm_hdd_mass_ready(int unit)
{
    char path[12];
    int fd;

    if (unit < 0 || unit >= BDM_HDD_MASS_MAX)
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

    bdm_hdd_make_mass_path(path, unit, "");
    fd = dopen(path, FIO_O_RDONLY);
    if (fd >= 0) {
        dclose(fd);
        return 1;
    }
    return 0;
}

/* 探测指定mass单元是否存在POPS目录 */
static int bdm_hdd_mass_has_pops(int unit)
{
    char path[16];
    int fd;

    if (unit < 0 || unit >= BDM_HDD_MASS_MAX)
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

    bdm_hdd_make_mass_path(path, unit, "POPS");
    fd = dopen(path, FIO_O_RDONLY);
    if (fd >= 0) {
        dclose(fd);
        return 1;
    }
    return 0;
}

/* 将含POPS的ATA卷提升到mass0 */
static int bdm_hdd_promote_ata_pops_to_mass0(void)
{
    struct block_device *bds[BDM_HDD_BD_MAX];
    struct block_device *ata_parts[BDM_HDD_BD_MAX];
    struct block_device *pops_bd;
    int n;
    int i;
    int j;

    memset(bds, 0, sizeof(bds));
    bdm_get_bd(bds, BDM_HDD_BD_MAX);

    n = 0;
    for (i = 0; i < BDM_HDD_BD_MAX; i++) {
        if (bds[i] && bds[i]->name && strcmp(bds[i]->name, "ata") == 0 && bds[i]->parNr != 0)
            ata_parts[n++] = bds[i];
    }

    if (n <= 0)
        return -1;
    if (n == 1)
        return bdm_hdd_mass_has_pops(0) ? 0 : -1;

    for (i = 0; i < n; i++)
        bdm_disconnect_bd(ata_parts[i]);
    DelayThread(BDM_HDD_MOUNT_SETTLE_US);

    pops_bd = NULL;
    for (i = 0; i < n; i++) {
        bdm_connect_bd(ata_parts[i]);
        for (j = 0; j < 40; j++) {
            DelayThread(BDM_HDD_POPS_WAIT_STEP_US);
            if (bdm_hdd_mass_has_pops(0)) {
                pops_bd = ata_parts[i];
                break;
            }
        }
        if (pops_bd)
            break;
        bdm_disconnect_bd(ata_parts[i]);
        DelayThread(BDM_HDD_MOUNT_SETTLE_US);
    }

    if (!pops_bd) {
        for (i = 0; i < n; i++)
            bdm_connect_bd(ata_parts[i]);
        DelayThread(BDM_HDD_MOUNT_SETTLE_US);
        return -1;
    }

    for (i = 0; i < n; i++) {
        if (ata_parts[i] != pops_bd)
            bdm_connect_bd(ata_parts[i]);
    }
    DelayThread(BDM_HDD_MOUNT_SETTLE_US);
    return 0;
}

/* 等待mass0:/POPS；ATA未挂上则在30秒内不断重试重挂 */
static int bdm_hdd_wait_pops_on_mass0(void)
{
    unsigned int waited;
    int unit;
    int found;
    int promoted;
    int parts;
    int remount_cd;
    int mass_any;

    DelayThread(BDM_HDD_MOUNT_SETTLE_US * 2);

    promoted = 0;
    remount_cd = 0;
    for (waited = 0; waited < BDM_HDD_POPS_WAIT_TOTAL_US; waited += BDM_HDD_POPS_WAIT_STEP_US) {
        bdm_hdd_disconnect_non_ata();

        parts = bdm_hdd_count_ata_parts();
        mass_any = 0;
        for (unit = 0; unit < BDM_HDD_MASS_MAX; unit++) {
            if (bdm_hdd_mass_ready(unit)) {
                mass_any = 1;
                break;
            }
        }

        /* ATA分区或FatFs未就绪：约每1秒重挂整盘，直到30秒超时 */
        if (parts <= 0 || !mass_any) {
            if (remount_cd <= 0) {
                bdm_hdd_remount_ata_wholes();
                remount_cd = 1000000 / BDM_HDD_POPS_WAIT_STEP_US;
            } else {
                remount_cd--;
            }
            DelayThread(BDM_HDD_POPS_WAIT_STEP_US);
            continue;
        }
        remount_cd = 0;

        if (bdm_hdd_mass_has_pops(0))
            return 0;

        found = -1;
        for (unit = 1; unit < BDM_HDD_MASS_MAX; unit++) {
            if (bdm_hdd_mass_has_pops(unit)) {
                found = unit;
                break;
            }
        }

        if (found > 0 && !promoted) {
            if (bdm_hdd_promote_ata_pops_to_mass0() == 0 && bdm_hdd_mass_has_pops(0))
                return 0;
            promoted = 1;
        }

        DelayThread(BDM_HDD_POPS_WAIT_STEP_US);
    }

    return -1;
}

int _start(int argc, char *argv[])
{
    int result;
    unsigned int waited;
    /* 不要把POPStarter传入的argv转给DEV9（未知-xxx参数会直接失败退出） */
    char *dev9_argv[1];

    (void)argc;
    (void)argv;

    /* usbd入口拦截 + 本模块再清一次已挂设备 */
    bdm_set_ata_only(1);
    bdm_hdd_disconnect_non_ata();

    dev9_argv[0] = MODNAME;
    result = dev9_start(1, dev9_argv);
    if (result == MODULE_NO_RESIDENT_END)
        return MODULE_NO_RESIDENT_END;

    /* 先试一次；仅失败时才在30秒内重试 */
    bdm_set_ata_only(1);
    bdm_hdd_disconnect_non_ata();
    result = atad_start(0, NULL);
    if (result == MODULE_NO_RESIDENT_END) {
        for (waited = BDM_HDD_POPS_WAIT_STEP_US; waited < BDM_HDD_POPS_WAIT_TOTAL_US;
             waited += BDM_HDD_POPS_WAIT_STEP_US) {
            DelayThread(BDM_HDD_POPS_WAIT_STEP_US);
            bdm_set_ata_only(1);
            bdm_hdd_disconnect_non_ata();
            result = atad_start(0, NULL);
            if (result != MODULE_NO_RESIDENT_END)
                break;
        }
        if (result == MODULE_NO_RESIDENT_END)
            return MODULE_NO_RESIDENT_END;
    }

    bdm_hdd_wait_pops_on_mass0();
    return MODULE_RESIDENT_END;
}
