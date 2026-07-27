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

extern int dev9_start(int argc, char *argv[]);
extern int atad_start(int argc, char *argv[]);

/* 累积调试日志；用ioman写MC（与USBD_DBG同一路径，避免依赖iomanX） */
static char g_ata_dbg[512];

static void ata_dbg_flush(void)
{
    static const char *const paths[] = {
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

    for (i = 0; i < 4; i++) {
        fd = open(paths[i], FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
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

/* 用ioman探测mass:/POPS；提升仍由bdm_assault的connect_bd完成 */
static int bdm_hdd_wait_pops_on_mass0(void)
{
    unsigned int waited;
    int fd;
    static const char *const pops_paths[] = {
        "mass:/POPS",
        "mass0:/POPS",
        "mass:POPS",
        "mass0:POPS",
    };
    int i;

    for (waited = 0; waited < BDM_HDD_POPS_WAIT_TOTAL_US; waited += BDM_HDD_POPS_WAIT_STEP_US) {
        for (i = 0; i < 4; i++) {
            fd = dopen(pops_paths[i], FIO_O_RDONLY);
            if (fd >= 0) {
                dclose(fd);
                ata_dbg_line("POPS_OK\n");
                printf("BDM HDD Assault: mass POPS ready (%u ms)\n", waited / 1000);
                return 0;
            }
        }
        DelayThread(BDM_HDD_POPS_WAIT_STEP_US);
    }

    ata_dbg_line("POPS_TIMEOUT\n");
    printf("BDM HDD Assault: mass POPS not ready after %u ms\n",
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
    ata_dbg_line("START\n");

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
