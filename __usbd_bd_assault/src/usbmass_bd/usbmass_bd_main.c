#define MAJOR_VER 1
#define MINOR_VER 1

#include "include/scsi.h"
#include <bdm.h>
#include <ioman.h>
#include <io_common.h>
#include <irx.h>
#include <loadcore.h>
#include <stdio.h>
#include <thbase.h>

// #define DEBUG  //comment out this line when not debugging
#include "include/module_debug.h"

//IRX_ID(MODNAME, MAJOR_VER, MINOR_VER);

/* 薄机USB枚举/挂载较慢；等mass上出现POPS再返回，避免POPStarter过早探测失败 */
#define USB_POPS_WAIT_TOTAL_US (16000000)
#define USB_POPS_WAIT_STEP_US  (200000)
#define USB_MASS_MAX           10

extern int usb_mass_init(void);

static void usb_make_mass_path(char *path, int unit, const char *name)
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

static int usb_mass_has_pops(int unit)
{
    char path[16];
    int fd;

    if (unit < 0 || unit >= USB_MASS_MAX)
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

    usb_make_mass_path(path, unit, "POPS");
    fd = dopen(path, FIO_O_RDONLY);
    if (fd >= 0) {
        dclose(fd);
        return 1;
    }
    return 0;
}

/* 只探测、不重挂；找到任意massN:/POPS即提前返回。 */
static void usb_wait_pops_ready(void)
{
    unsigned int waited;
    int unit;

    M_PRINTF("Waiting for mass:/POPS (max %u s)\n", USB_POPS_WAIT_TOTAL_US / 1000000);

    for (waited = 0; waited < USB_POPS_WAIT_TOTAL_US; waited += USB_POPS_WAIT_STEP_US) {
        for (unit = 0; unit < USB_MASS_MAX; unit++) {
            if (usb_mass_has_pops(unit)) {
                M_PRINTF("Found POPS on mass%d after %u ms\n", unit, waited / 1000);
                return;
            }
        }
        DelayThread(USB_POPS_WAIT_STEP_US);
    }

    M_PRINTF("Timeout: mass:/POPS not found\n");
}

int usbmass_bd_start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    M_PRINTF("USBD ASSAULT: starting USBMASS Side\n");

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

    usb_wait_pops_ready();

    // return resident
    return MODULE_RESIDENT_END;
}
