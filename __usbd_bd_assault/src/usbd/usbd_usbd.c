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

#define MODNAME "bdm_hdd"
IRX_ID(MODNAME, 1, 1);

extern int dev9_start(int argc, char *argv[]);
extern int atad_start(int argc, char *argv[]);

int _start(int argc, char *argv[])
{
    int result;

    printf("BDM HDD Assault: starting DEV9\n");
    result = dev9_start(argc, argv);
    if (result == MODULE_NO_RESIDENT_END) {
        printf("BDM HDD Assault: DEV9 start failed\n");
        return MODULE_NO_RESIDENT_END;
    }

    printf("BDM HDD Assault: starting ATAD (BDM)\n");
    result = atad_start(argc, argv);
    if (result == MODULE_NO_RESIDENT_END) {
        printf("BDM HDD Assault: ATAD start failed (no HDD / init error)\n");
        return MODULE_NO_RESIDENT_END;
    }

    printf("BDM HDD Assault: ready\n");
    return MODULE_RESIDENT_END;
}
