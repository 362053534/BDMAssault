#define MAJOR_VER 1
#define MINOR_VER 1

#include "include/scsi.h"
#include <ioman.h>
#include <io_common.h>
#include <irx.h>
#include <loadcore.h>
#include <stdio.h>
#include <thbase.h>

// #define DEBUG  //comment out this line when not debugging
#include "include/module_debug.h"

/* FatFs/USB 枚举异步完成；低频探测，找到即返回，避免固定空等满超时 */
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

/* 只探测、不重挂：找到任意 massN:/POPS 就提前返回 */
static int usb_wait_pops_ready(void)
{
    unsigned int waited;
    int unit;

    M_PRINTF("Waiting for mass:/POPS (max %u s)\n", USB_POPS_WAIT_TOTAL_US / 1000000);

    /* 先空等一会，减少枚举未完成时 dopen 卡住的概率 */
    DelayThread(USB_POPS_WAIT_STEP_US);

    for (waited = USB_POPS_WAIT_STEP_US; waited < USB_POPS_WAIT_TOTAL_US;
         waited += USB_POPS_WAIT_STEP_US) {
        for (unit = 0; unit < USB_MASS_MAX; unit++) {
            if (usb_mass_has_pops(unit)) {
                M_PRINTF("Found POPS on mass%d after %u ms\n", unit, waited / 1000);
                return 0;
            }
        }
        if ((waited % 1000000) == 0)
            M_PRINTF("Still waiting... %u s\n", waited / 1000000);
        DelayThread(USB_POPS_WAIT_STEP_US);
    }

    M_PRINTF("Timeout: mass:/POPS not found\n");
    return -1;
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

    /* 找到POPS就提前返回，避免每次固定空等满16秒 */
    if (usb_wait_pops_ready() != 0)
        M_PRINTF("POPS not ready, continue anyway\n");

    return MODULE_RESIDENT_END;
}
