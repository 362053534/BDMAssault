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

/* 累积调试日志；双写记忆卡与mass根目录，便于mass未挂上时仍能定位 */
static char g_ata_dbg[768];
static unsigned char g_mass_seen[BDM_HDD_MASS_MAX];

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

static void ata_dbg_flush(void)
{
    static const char *const mc_paths[] = {
        "mc0:POPSTARTER/ATA_DBG.TXT",
        "mc0:/POPSTARTER/ATA_DBG.TXT",
        "mc1:POPSTARTER/ATA_DBG.TXT",
        "mc1:/POPSTARTER/ATA_DBG.TXT",
    };
    int i;
    int fd;
    int len;

    len = strlen(g_ata_dbg);
    if (len <= 0)
        return;

    /* 只写MC：BDM挂载未完成时open(mass:)可能与FatFs锁死锁，表现为停在ATAW=1 */
    for (i = 0; i < 4; i++) {
        fd = open(mc_paths[i], FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
        if (fd >= 0) {
            write(fd, g_ata_dbg, len);
            close(fd);
        }
    }
}

static void ata_dbg_line(const char *line)
{
    int used;
    int add;

    if (!line)
        return;

    used = strlen(g_ata_dbg);
    add = strlen(line);
    if (used + add + 1 >= (int)sizeof(g_ata_dbg))
        return;

    memcpy(g_ata_dbg + used, line, add + 1);
    ata_dbg_flush();
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

/* 记录当前BDM中ata整盘/分区数量，区分ATAD与分区/FatFs失败 */
static void ata_dbg_log_ata_bds(void)
{
    struct block_device *bds[BDM_HDD_BD_MAX];
    char line[10];
    int i;
    int whole;
    int parts;

    memset(bds, 0, sizeof(bds));
    bdm_get_bd(bds, BDM_HDD_BD_MAX);

    whole = 0;
    parts = 0;
    for (i = 0; i < BDM_HDD_BD_MAX; i++) {
        if (bds[i] && bds[i]->name && strcmp(bds[i]->name, "ata") == 0) {
            if (bds[i]->parNr == 0)
                whole++;
            else
                parts++;
        }
    }

    line[0] = 'A';
    line[1] = 'T';
    line[2] = 'A';
    line[3] = 'W';
    line[4] = '=';
    line[5] = '0' + (whole > 9 ? 9 : whole);
    line[6] = '\n';
    line[7] = '\0';
    ata_dbg_line(line);

    line[3] = 'P';
    line[5] = '0' + (parts > 9 ? 9 : parts);
    ata_dbg_line(line);
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
    /* path 形如 massN:/ */
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

/*
 * 仅重挂载ATA分区：把含POPS的卷接到最低空闲mass槽（通常为mass0）。
 * 不改bdm_assault，避免影响其它设备上的POPS路径。
 */
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

/* 等待mass0:/POPS；若POPS在其它ATA卷则在本模块内提升 */
static int bdm_hdd_wait_pops_on_mass0(void)
{
    unsigned int waited;
    int unit;
    int found;
    int promoted;
    int parts;
    char seen_line[8];
    struct block_device *bds[BDM_HDD_BD_MAX];
    int i;

    /* 给BDM异步挂载一点时间后再采样块设备 */
    DelayThread(BDM_HDD_MOUNT_SETTLE_US * 2);
    ata_dbg_log_ata_bds();

    promoted = 0;
    for (waited = 0; waited < BDM_HDD_POPS_WAIT_TOTAL_US; waited += BDM_HDD_POPS_WAIT_STEP_US) {
        /* 等待期间若USB又连上，继续踢掉 */
        bdm_hdd_disconnect_non_ata();

        /* 先数分区；ATAP=0时不要dopen(mass:)，避免踩到卡住的挂载路径 */
        parts = 0;
        memset(bds, 0, sizeof(bds));
        bdm_get_bd(bds, BDM_HDD_BD_MAX);
        for (i = 0; i < BDM_HDD_BD_MAX; i++) {
            if (bds[i] && bds[i]->name && strcmp(bds[i]->name, "ata") == 0 && bds[i]->parNr != 0)
                parts++;
        }

        if (parts <= 0) {
            DelayThread(BDM_HDD_POPS_WAIT_STEP_US);
            continue;
        }

        for (unit = 0; unit < BDM_HDD_MASS_MAX; unit++) {
            if (!g_mass_seen[unit] && bdm_hdd_mass_ready(unit)) {
                g_mass_seen[unit] = 1;
                seen_line[0] = 'M';
                seen_line[1] = 'A';
                seen_line[2] = 'S';
                seen_line[3] = 'S';
                seen_line[4] = '0' + unit;
                seen_line[5] = '\n';
                seen_line[6] = '\0';
                ata_dbg_line(seen_line);
            }
        }

        if (bdm_hdd_mass_has_pops(0)) {
            ata_dbg_line("POPS_OK\n");
            printf("BDM HDD Assault: mass0 POPS ready (%u ms)\n", waited / 1000);
            return 0;
        }

        found = -1;
        for (unit = 1; unit < BDM_HDD_MASS_MAX; unit++) {
            if (bdm_hdd_mass_has_pops(unit)) {
                found = unit;
                break;
            }
        }

        if (found > 0 && !promoted) {
            ata_dbg_line("PROMOTE\n");
            printf("BDM HDD Assault: POPS on mass%d, promoting ATA volume\n", found);
            if (bdm_hdd_promote_ata_pops_to_mass0() == 0 && bdm_hdd_mass_has_pops(0)) {
                ata_dbg_line("POPS_OK\n");
                printf("BDM HDD Assault: mass0 POPS ready after promote (%u ms)\n",
                       waited / 1000);
                return 0;
            }
            ata_dbg_line("PROMOTE_FAIL\n");
            promoted = 1;
        }

        DelayThread(BDM_HDD_POPS_WAIT_STEP_US);
    }

    ata_dbg_log_ata_bds();
    ata_dbg_line("POPS_TIMEOUT\n");
    printf("BDM HDD Assault: mass0 POPS not ready after %u ms\n",
           BDM_HDD_POPS_WAIT_TOTAL_US / 1000);
    return -1;
}

int _start(int argc, char *argv[])
{
    int result;
    /* 不要把POPStarter传入的argv转给DEV9（未知-xxx参数会直接失败退出） */
    char *dev9_argv[1];

    (void)argc;
    (void)argv;

    g_ata_dbg[0] = '\0';
    memset(g_mass_seen, 0, sizeof(g_mass_seen));
    ata_dbg_line("START\n");

    /* 屏蔽已挂上的USB等非ATA设备，避免抢占mass0 */
    bdm_hdd_disconnect_non_ata();
    ata_dbg_line("NO_USB\n");

    printf("BDM HDD Assault: starting DEV9\n");
    dev9_argv[0] = MODNAME;
    result = dev9_start(1, dev9_argv);
    if (result == MODULE_NO_RESIDENT_END) {
        ata_dbg_line("DEV9_FAIL\n");
        printf("BDM HDD Assault: DEV9 start failed\n");
        return MODULE_NO_RESIDENT_END;
    }
    ata_dbg_line("DEV9_OK\n");

    printf("BDM HDD Assault: starting ATAD (BDM)\n");
    result = atad_start(0, NULL);
    if (result == MODULE_NO_RESIDENT_END) {
        ata_dbg_line("ATAD_FAIL\n");
        printf("BDM HDD Assault: ATAD start failed (no HDD / init error)\n");
        return MODULE_NO_RESIDENT_END;
    }
    ata_dbg_line("ATAD_OK\n");
    ata_dbg_line("WAIT_POPS\n");

    /* 等待含POPS/的ATA卷挂上并提升到mass0（总超时30秒） */
    bdm_hdd_wait_pops_on_mass0();

    ata_dbg_line("READY\n");
    printf("BDM HDD Assault: ready\n");
    return MODULE_RESIDENT_END;
}

