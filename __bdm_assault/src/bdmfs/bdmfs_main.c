#include <bdm.h>
#include <intrman.h>
#include <irx.h>
#include <loadcore.h>
#include <stdio.h>
#include <sysmem.h>
#include <cdvdman.h>

#include "include/ff.h"
#include "include/fs_driver.h"

extern int bdm_get_popstarter_target(unsigned int *deviceType, const char **vcdPath);

//#define DEBUG  //非调试构建请保持此行注释
#include "include/module_debug.h"

#define MAJOR_VER 1
#define MINOR_VER 4

// 此组件与BDM块设备管理器共用同一个IRX。

static struct file_system g_fs = {
    .priv = NULL,
    .name = "fatfs",
    .connect_bd = connect_bd,
    .disconnect_bd = disconnect_bd,
};

int bdmfs_fatfs_start(int argc, char *argv[])
{
    const char *vcdPath;
    unsigned int deviceType;

    (void)argc;
    (void)argv;

    printf("BDM_ASSAULT: starting Fatfs side\n");

    // 初始化文件系统驱动。
    if (InitFS() != 0) {
        M_DEBUG("Error initializing FatFs driver!\n");
        return MODULE_NO_RESIDENT_END;
    }

    if (bdm_get_popstarter_target(&deviceType, &vcdPath) < 0 ||
        fatfs_fs_driver_set_popstarter_target(deviceType, vcdPath) < 0) {
        M_DEBUG("Error applying POPStarter EE mailbox!\n");
        return MODULE_NO_RESIDENT_END;
    }

    // 连接到块设备管理器。
    bdm_connect_fs(&g_fs);

    // 模块保持驻留。
    return MODULE_RESIDENT_END;
}

void *malloc(int size)
{
    void *result;
    int OldState;

    CpuSuspendIntr(&OldState);
    result = AllocSysMemory(ALLOC_FIRST, size, NULL);
    CpuResumeIntr(OldState);

    return result;
}

void free(void *ptr)
{
    int OldState;

    CpuSuspendIntr(&OldState);
    FreeSysMemory(ptr);
    CpuResumeIntr(OldState);
}
