#include <irx.h>
#include <loadcore.h>
#include <stdio.h>
#include <ioman.h>
#include <io_common.h>
#include <thbase.h>
#include <bdm.h>

#define MODNAME "bdm_hdd"
IRX_ID(MODNAME, 1, 1);

/* FatFs挂载由BDM线程异步完成；等待ATA卷的mass0:/POPS出现或超时。 */
#define BDM_HDD_POPS_WAIT_TOTAL_US (30000000)
#define BDM_HDD_POPS_WAIT_STEP_US  (50000)
#define BDM_HDD_PROBE_RETRY_US     (500000)

extern int dev9_start(int argc, char *argv[]);
extern int atad_start(int argc, char *argv[]);
extern int atad_probe(void);

static int bdm_hdd_mass0_has_pops(void)
{
    int fd;

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

static int bdm_hdd_wait_expired(const iop_sys_clock_t *start)
{
    iop_sys_clock_t now;
    iop_sys_clock_t elapsed;
    u32 seconds;
    u32 useconds;

    GetSystemTime(&now);
    elapsed.lo = now.lo - start->lo;
    elapsed.hi = now.hi - start->hi - (now.lo < start->lo);
    SysClock2USec(&elapsed, &seconds, &useconds);
    return (u64)seconds * 1000000 + useconds >= BDM_HDD_POPS_WAIT_TOTAL_US;
}

/* 只观察挂载状态；慢硬盘只重试探测，不重启模块或改动BDM拓扑。 */
static int bdm_hdd_wait_pops_on_mass0(void)
{
    iop_sys_clock_t start;
    unsigned int probe_waited;
    int ata_ready;

    GetSystemTime(&start);
    probe_waited = 0;
    while (!bdm_hdd_wait_expired(&start)) {
        ata_ready = bdm_is_fatfs_ready("ata");
        if (ata_ready && bdm_hdd_mass0_has_pops())
            return 0;

        if (!ata_ready && probe_waited >= BDM_HDD_PROBE_RETRY_US) {
            atad_probe();
            probe_waited = 0;
        }

        DelayThread(BDM_HDD_POPS_WAIT_STEP_US);
        probe_waited += BDM_HDD_POPS_WAIT_STEP_US;
    }

    return -1;
}

int _start(int argc, char *argv[])
{
    int result;
    /* 不要把POPStarter传入的argv转给DEV9（未知-xxx参数会直接失败退出） */
    char *dev9_argv[1];

    (void)argc;
    (void)argv;

    /* 在ATA接入前设置过滤器；不要在此处主动断开其他设备。 */
    bdm_set_ata_only(1);

    dev9_argv[0] = MODNAME;
    result = dev9_start(1, dev9_argv);
    if (result == MODULE_NO_RESIDENT_END)
        return MODULE_NO_RESIDENT_END;

    /* ATAD模块只启动一次；慢硬盘由atad_probe()重试底层探测。 */
    result = atad_start(0, NULL);
    if (result == MODULE_NO_RESIDENT_END) {
        printf("bdm_hdd: ATAD start failed.\n");
        /* DEV9已经驻留，不能让装有其代码和回调的组合模块被卸载。 */
        return MODULE_RESIDENT_END;
    }

    if (bdm_hdd_wait_pops_on_mass0() < 0)
        printf("bdm_hdd: timed out waiting for ATA mass0:/POPS.\n");

    /* DEV9和ATAD已经注册回调及导出表，超时后仍必须保持模块驻留。 */
    return MODULE_RESIDENT_END;
}

