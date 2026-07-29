#define MAJOR_VER 1
#define MINOR_VER 1

#include "include/scsi.h"
#include <irx.h>
#include <loadcore.h>
#include <stdio.h>
#include <thbase.h>

// #define DEBUG  //comment out this line when not debugging
#include "include/module_debug.h"

/* 仅延时，不在_start里dopen/重挂/写MC——瘦机上那些操作可能永久卡住导致黑屏 */
#define USB_POPS_WAIT_TOTAL_US (16000000)
#define USB_POPS_WAIT_STEP_US  (1000000)

extern int usb_mass_init(void);

static void usb_wait_for_async_mount(void)
{
    unsigned int waited;

    M_PRINTF("wait %u sec for USB/FatFs async mount\n",
             USB_POPS_WAIT_TOTAL_US / 1000000);

    for (waited = 0; waited < USB_POPS_WAIT_TOTAL_US; waited += USB_POPS_WAIT_STEP_US)
        DelayThread(USB_POPS_WAIT_STEP_US);

    M_PRINTF("wait done, return to POPStarter\n");
}

int usbmass_bd_start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    M_PRINTF("USBD ASSAULT: starting USBMASS Side\n");

    if (scsi_init() != 0) {
        M_PRINTF("ERROR: initializing SCSI driver!\n");
        return MODULE_NO_RESIDENT_END;
    }

    if (usb_mass_init() != 0) {
        M_PRINTF("ERROR: initializing USB driver!\n");
        return MODULE_NO_RESIDENT_END;
    }

    /* 给枚举+scsi_warmup+分区+FatFs时间；探测交给POPStarter */
    usb_wait_for_async_mount();

    return MODULE_RESIDENT_END;
}
