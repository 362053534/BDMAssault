#include <irx_imports.h>
#include <irx.h>


IRX_ID("ata_assault", 2, 7);
int _start(int argc, char** argv) {
    printf("ATA BDM ASSAULT BY El_isra\tCompiled on" __DATE__" "__TIME__"\n");
    int r;
    r = dev9_start(argc, argv);
    if (r == MODULE_NO_RESIDENT_END) return r;
    r = atad_start(argc, argv)
    return r;
}