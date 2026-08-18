#define MAJOR_VER 1
#define MINOR_VER 1

#include "include/scsi.h"
#include <bdm.h>
#include <irx.h>
#include <loadcore.h>
#include <stdio.h>
#include <thbase.h>

// #define DEBUG  //comment out this line when not debugging
#include "include/module_debug.h"

//IRX_ID(MODNAME, MAJOR_VER, MINOR_VER);

extern int usb_mass_init(void);

int usbmass_bd_start(int argc, char *argv[])
{
    M_PRINTF("USBD ASSAULT: starting USBMASS Side\n");

    if (bdm_set_popstarter_vcd(argc, argv) < 0) {
        M_PRINTF("ERROR: invalid POPStarter VCD argument!\n");
        return MODULE_NO_RESIDENT_END;
    }

    // initialize the SCSI driver
    if (scsi_init() != 0) {
        M_PRINTF("ERROR: initializing SCSI driver!\n");
        return MODULE_NO_RESIDENT_END;
    }

    // initialize the USB driver
    if (usb_mass_init() != 0) {
        M_PRINTF("ERROR: initializing USB driver!\n");
        return MODULE_NO_RESIDENT_END;
    }

    /* 只等待内存中的目标VCD就绪状态，避免在当前线程读盘。 */
    while (!bdm_is_usb_fatfs_ready())
        DelayThread(200000);

    // return resident
    return MODULE_RESIDENT_END;
}
