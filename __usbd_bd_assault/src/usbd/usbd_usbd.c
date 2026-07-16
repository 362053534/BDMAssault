/*
 * BDM HDD driver entry point.
 *
 * DEV9 is embedded into this module so POPStarter can load one driver file.
 * After DEV9 is available, ATAD registers the ATA disk as a BDM block device.
 */

#include <irx.h>
#include <modload.h>
#include <stdio.h>

extern unsigned char bdm_hdd_dev9_irx[];
extern int bdm_hdd_atad_start(int argc, char *argv[]);

int _start(int argc, char *argv[])
{
    int result;

    printf("BDM HDD: loading embedded DEV9 driver\n");
    result = LoadModuleBuffer(bdm_hdd_dev9_irx);
    if (result < 0) {
        printf("BDM HDD: DEV9 load failed: %d\n", result);
        return MODULE_NO_RESIDENT_END;
    }

    return bdm_hdd_atad_start(argc, argv);
}

