#include <bdm.h>
#include <intrman.h>
#include <irx.h>
#include <loadcore.h>
#include <stdio.h>
#include <sysmem.h>
#include <sysclib.h>
#include <ioman.h>
#include <io_common.h>
#include <cdvdman.h>

#include "include/ff.h"
#include "include/fs_driver.h"

//#define DEBUG  //comment out this line when not debugging
#include "include/module_debug.h"

#define MAJOR_VER 1
#define MINOR_VER 4

// This component shares an IRX with the BDM block-device manager.

static struct file_system g_fs = {
    .priv = NULL,
    .name = "fatfs",
    .connect_bd = connect_bd,
    .disconnect_bd = disconnect_bd,
};

/* usbd.irx加载时先打点，用于确认POPStarter是否加载了bdm_assault */
static void fatfs_dbg_mark_usbd_loaded(void)
{
    static const char *const paths[] = {
        "mc0:POPSTARTER/USBD_DBG.TXT",
        "mc0:/POPSTARTER/USBD_DBG.TXT",
        "mc1:POPSTARTER/USBD_DBG.TXT",
        "mc1:/POPSTARTER/USBD_DBG.TXT",
    };
    static const char msg[] = "USBD_OK\nbdm_assault loaded\n";
    int i;
    int fd;

    for (i = 0; i < 4; i++) {
        fd = open(paths[i], FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
        if (fd >= 0) {
            write(fd, (void *)msg, sizeof(msg) - 1);
            close(fd);
        }
    }
}

int bdmfs_fatfs_start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("BDM_ASSAULT: starting Fatfs side\n");

    // initialize the file system driver
    if (InitFS() != 0) {
        M_DEBUG("Error initializing FatFs driver!\n");
        return MODULE_NO_RESIDENT_END;
    }

    // Connect to block device manager
    bdm_connect_fs(&g_fs);

    fatfs_dbg_mark_usbd_loaded();

    // return resident
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
