#include <irx.h>
#include <loadcore.h>
#include <stdio.h>
#include <thbase.h>
#include <bdm.h>
#include <atahw.h>

#define MODNAME "bdm_hdd"
IRX_ID(MODNAME, 1, 1);

/* FatFs挂载由BDM线程异步完成；目标VCD就绪前不能把控制权交给POPS。 */
#define BDM_HDD_POPS_WAIT_STEP_US  (200000)
#define BDM_HDD_PROBE_RETRY_US     (500000)
#define BDM_HDD_READY_SAMPLES      2

extern int dev9_start(int argc, char *argv[]);
extern int atad_start(int argc, char *argv[]);
extern int atad_probe(void);
extern int atad_get_probe_status(void);

/* 只观察挂载状态；慢硬盘只重试探测，不重启模块或改动BDM拓扑。 */
static void bdm_hdd_wait_pops_on_mass0(void)
{
    unsigned int probe_waited;
    int ata_detected;
    int ready_samples;
    int status;

    /* atad_start()已执行首次探测；后续完整探测必须先通过被动状态检查。 */
    ata_detected = 0;
    ready_samples = 0;
    probe_waited = BDM_HDD_PROBE_RETRY_US;
    while (!bdm_is_fatfs_ready("ata")) {
        if (!ata_detected) {
            status = atad_get_probe_status();
            if (status >= 0 && (status & ATA_STAT_BUSY) == 0 && (status & ATA_STAT_READY) != 0)
                ready_samples++;
            else
                ready_samples = 0;

            if (ready_samples >= BDM_HDD_READY_SAMPLES && probe_waited >= BDM_HDD_PROBE_RETRY_US) {
                ata_detected = atad_probe() == 0;
                ready_samples = 0;
                probe_waited = 0;
            }

            if (!ata_detected && probe_waited < BDM_HDD_PROBE_RETRY_US)
                probe_waited += BDM_HDD_POPS_WAIT_STEP_US;
        }

        DelayThread(BDM_HDD_POPS_WAIT_STEP_US);
    }
}

int _start(int argc, char *argv[])
{
    int result;
    int source_result;
    /* 不要把POPStarter传入的argv转给DEV9（未知-xxx参数会直接失败退出） */
    char *dev9_argv[1];

    source_result = bdm_set_popstarter_vcd(argc, argv);
    if (source_result == BDM_POPSTARTER_DRIVER_INERT)
        return MODULE_RESIDENT_END;
    if (source_result < 0) {
        printf("bdm_hdd: invalid POPStarter VCD argument.\n");
        return MODULE_NO_RESIDENT_END;
    }

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

    bdm_hdd_wait_pops_on_mass0();

    /* DEV9和ATAD已经注册回调及导出表，模块必须保持驻留。 */
    return MODULE_RESIDENT_END;
}

