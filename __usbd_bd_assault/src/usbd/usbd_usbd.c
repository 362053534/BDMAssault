/*
 * BDM HDD driver entry point.
 *
 * DEV9 and the PS2SDK's BDM-enabled ATAD are embedded into this module so
 * POPStarter can load one driver file.
 */

#include <irx.h>
#include <loadcore.h>
#include <modload.h>
#include <stdio.h>

#define MODNAME "bdm_hdd"
IRX_ID(MODNAME, 1, 1);

extern unsigned char bdm_hdd_dev9_irx[];
extern unsigned char bdm_hdd_atad_irx[];

int _start(int argc, char *argv[])
{
    int result;

    printf("BDM HDD: loading embedded DEV9 driver\n");
    result = LoadModuleBuffer(bdm_hdd_dev9_irx);
    if (result < 0) {
        printf("BDM HDD: DEV9 load failed: %d\n", result);
        return MODULE_NO_RESIDENT_END;
    }

    printf("BDM HDD: loading embedded ATA BDM driver\n");
    result = LoadModuleBuffer(bdm_hdd_atad_irx);
    if (result < 0) {
        printf("BDM HDD: ATA BDM load failed: %d\n", result);
        return MODULE_NO_RESIDENT_END;
    }

    return MODULE_RESIDENT_END;
}

