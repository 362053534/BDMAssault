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
#define BDM_HDD_PATH_RETRY_US      (1000000)

extern int dev9_start(int argc, char *argv[]);
extern int atad_start(int argc, char *argv[]);
extern int atad_probe(void);

static int bdm_hdd_mass0_has_pops(void)
{
    int fd;

    fd = dopen("mass:/POPS", FIO_O_RDONLY);
    bdm_trace_log("BDH_PATH dopen path=mass:/POPS ret=%d\n", fd);
    if (fd >= 0) {
        dclose(fd);
        return 1;
    }
    fd = dopen("mass0:/POPS", FIO_O_RDONLY);
    bdm_trace_log("BDH_PATH dopen path=mass0:/POPS ret=%d\n", fd);
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
    unsigned int path_waited;
    int last_ata_ready;
    int probe_result;
    int ata_ready;

    GetSystemTime(&start);
    probe_waited = 0;
    path_waited = BDM_HDD_PATH_RETRY_US;
    last_ata_ready = -1;
    bdm_trace_log("BDH_WAIT begin timeout_us=%u poll_us=%u probe_us=%u path_us=%u\n",
                  BDM_HDD_POPS_WAIT_TOTAL_US, BDM_HDD_POPS_WAIT_STEP_US,
                  BDM_HDD_PROBE_RETRY_US, BDM_HDD_PATH_RETRY_US);
    while (!bdm_hdd_wait_expired(&start)) {
        ata_ready = bdm_is_fatfs_ready("ata");
        if (ata_ready != last_ata_ready) {
            bdm_trace_log("BDH_WAIT fatfs_ready=%d\n", ata_ready);
            last_ata_ready = ata_ready;
        }

        if (ata_ready && path_waited >= BDM_HDD_PATH_RETRY_US) {
            if (bdm_hdd_mass0_has_pops()) {
                bdm_trace_log("BDH_WAIT ready mass0:/POPS\n");
                return 0;
            }
            path_waited = 0;
        }

        if (!ata_ready && probe_waited >= BDM_HDD_PROBE_RETRY_US) {
            probe_result = atad_probe();
            bdm_trace_log("BDH_WAIT reprobe ret=%d\n", probe_result);
            probe_waited = 0;
        }

        DelayThread(BDM_HDD_POPS_WAIT_STEP_US);
        probe_waited += BDM_HDD_POPS_WAIT_STEP_US;
        path_waited += BDM_HDD_POPS_WAIT_STEP_US;
    }

    bdm_trace_log("BDH_WAIT timeout fatfs_ready=%d\n", last_ata_ready);
    return -1;
}

int _start(int argc, char *argv[])
{
    int i;
    int result;
    /* 不要把POPStarter传入的argv转给DEV9（未知-xxx参数会直接失败退出） */
    char *dev9_argv[1];

    bdm_trace_init();
    bdm_trace_log("BDH_ENTRY argc=%d argv=0x%08x\n", argc, (u32)argv);
    if (argv) {
        for (i = 0; i < argc && i < 8; i++)
            bdm_trace_log("BDH_ARG index=%d value=%.160s\n", i, argv[i] ? argv[i] : "(null)");
    }

    /* 在ATA接入前设置过滤器；不要在此处主动断开其他设备。 */
    bdm_trace_log("BDH_FILTER begin\n");
    bdm_set_ata_only(1);
    bdm_trace_log("BDH_FILTER complete\n");

    dev9_argv[0] = MODNAME;
    bdm_trace_log("BDH_DEV9 begin\n");
    result = dev9_start(1, dev9_argv);
    bdm_trace_log("BDH_DEV9 result=%d\n", result);
    if (result == MODULE_NO_RESIDENT_END) {
        bdm_trace_log("BDH_RETURN no_resident reason=dev9\n");
        return MODULE_NO_RESIDENT_END;
    }

    /* ATAD模块只启动一次；慢硬盘由atad_probe()重试底层探测。 */
    bdm_trace_log("BDH_ATAD begin\n");
    result = atad_start(0, NULL);
    bdm_trace_log("BDH_ATAD result=%d\n", result);
    if (result == MODULE_NO_RESIDENT_END) {
        printf("bdm_hdd: ATAD start failed.\n");
        /* DEV9已经驻留，不能让装有其代码和回调的组合模块被卸载。 */
        bdm_trace_log("BDH_RETURN resident reason=atad_failed\n");
        return MODULE_RESIDENT_END;
    }

    result = bdm_hdd_wait_pops_on_mass0();
    if (result < 0)
        printf("bdm_hdd: timed out waiting for ATA mass0:/POPS.\n");
    bdm_trace_log("BDH_WAIT result=%d\n", result);

    /* DEV9和ATAD已经注册回调及导出表，超时后仍必须保持模块驻留。 */
    bdm_trace_log("BDH_RETURN resident\n");
    return MODULE_RESIDENT_END;
}

