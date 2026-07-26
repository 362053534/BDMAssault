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
#include <thbase.h>

#define MODNAME "bdm_hdd"
IRX_ID(MODNAME, 1, 1);

/* FatFs挂载在BDM线程异步完成，需短暂等待后再让POPS访问mass: */
#define BDM_HDD_MOUNT_SETTLE_US (300000)

extern int dev9_start(int argc, char *argv[]);
extern int atad_start(int argc, char *argv[]);

int _start(int argc, char *argv[])
{
    int result;
    /* 不要把POPStarter传入的argv转给DEV9（未知-xxx参数会直接失败退出） */
    char *dev9_argv[1];

    (void)argc;
    (void)argv;

    printf("BDM HDD Assault: starting DEV9\n");
    dev9_argv[0] = MODNAME;
    result = dev9_start(1, dev9_argv);
    if (result == MODULE_NO_RESIDENT_END) {
        printf("BDM HDD Assault: DEV9 start failed\n");
        return MODULE_NO_RESIDENT_END;
    }

    printf("BDM HDD Assault: starting ATAD (BDM)\n");
    result = atad_start(0, NULL);
    if (result == MODULE_NO_RESIDENT_END) {
        printf("BDM HDD Assault: ATAD start failed (no HDD / init error)\n");
        return MODULE_NO_RESIDENT_END;
    }

    /* 等待MBR/GPT+FatFs完成mass挂载 */
    DelayThread(BDM_HDD_MOUNT_SETTLE_US);

    printf("BDM HDD Assault: ready\n");
    return MODULE_RESIDENT_END;
}
