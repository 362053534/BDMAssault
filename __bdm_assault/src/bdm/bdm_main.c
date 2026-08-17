#include <bdm.h>
#include <irx.h>
#include <io_common.h>
#include <ioman.h>
#include <loadcore.h>
#include <stdio.h>
#include <sysclib.h>
#include <irx_argv_trace.h>

// #define DEBUG  //comment out this line when not debugging
#include "include/module_debug.h"

#define MAJOR_VER 2
#define MINOR_VER 1

IRX_ID("bdm", MAJOR_VER, MINOR_VER);

extern struct irx_export_table _exp_bdm;
extern int bdm_init();
extern void part_init();
extern int bdmfs_fatfs_start(int argc, char *argv[]);
extern void fatfs_fs_driver_set_source_selection(const char *driver, int unit, u64 lba, const char *path);

#define POPSTARTER_BDM_SOURCE_MAGIC    0x314d4442
#define POPSTARTER_BDM_SOURCE_VERSION  1
#define POPSTARTER_BDM_SOURCE_PATH_MAX 256
#define POPSTARTER_BDM_SOURCE_UNIT_MAX 5
#define POPSTARTER_BDM_SOURCE_FILE     "POPSTARTER/BDM-SOURCE.BIN"

typedef struct
{
    u32 magic;
    u32 version;
    char driver[4];
    u32 device;
    u32 unit;
    u32 lbaLow;
    u32 lbaHigh;
    char path[POPSTARTER_BDM_SOURCE_PATH_MAX];
    u32 checksum;
} popstarter_bdm_source_t;

static u32 bdm_source_checksum(const popstarter_bdm_source_t *source)
{
    const u8 *data = (const u8 *)source;
    u32 checksum = 0x6d617373;
    int size = sizeof(*source) - sizeof(source->checksum);
    int i;

    for (i = 0; i < size; i++) {
        checksum = (checksum << 5) | (checksum >> 27);
        checksum ^= data[i];
    }

    return checksum;
}

static int bdm_source_is_valid(const popstarter_bdm_source_t *source)
{
    int i;

    if (source->magic != POPSTARTER_BDM_SOURCE_MAGIC || source->version != POPSTARTER_BDM_SOURCE_VERSION || source->driver[0] == '\0' || source->driver[3] != '\0' || source->unit >= POPSTARTER_BDM_SOURCE_UNIT_MAX)
        return 0;
    if (source->checksum != bdm_source_checksum(source))
        return 0;

    for (i = 0; i < (int)sizeof(source->path); i++) {
        if (source->path[i] == '\0')
            return i > 0;
    }

    return 0;
}

static int bdm_load_source_selection(void)
{
    popstarter_bdm_source_t source;
    popstarter_bdm_source_t selected;
    char path[40];
    int selected_valid = 0;
    int slot;

    for (slot = 0; slot < 2; slot++) {
        int fd;
        int total;

        sprintf(path, "mc%d:%s", slot, POPSTARTER_BDM_SOURCE_FILE);
        fd = open(path, FIO_O_RDONLY);
        if (fd < 0)
            continue;

        total = 0;
        while (total < (int)sizeof(source)) {
            int result = read(fd, (u8 *)&source + total, (int)sizeof(source) - total);

            if (result <= 0)
                break;
            total += result;
        }
        close(fd);

        if (!selected_valid && total == (int)sizeof(source) && bdm_source_is_valid(&source)) {
            memcpy(&selected, &source, sizeof(selected));
            selected_valid = 1;
        }
    }

    if (selected_valid) {
        u64 lba = ((u64)selected.lbaHigh << 32) | selected.lbaLow;

        fatfs_fs_driver_set_source_selection(selected.driver, selected.unit, lba, selected.path);
        printf("BDM_ASSAULT: source %s%d mass%u lba=%08x%08x path=%s\n", selected.driver, selected.device, selected.unit, selected.lbaHigh, selected.lbaLow, selected.path);
        return 1;
    }

    return 0;
}

static void bdm_remove_source_selection(void)
{
    char path[40];
    int slot;

    for (slot = 0; slot < 2; slot++) {
        sprintf(path, "mc%d:%s", slot, POPSTARTER_BDM_SOURCE_FILE);
        remove(path);
    }
}

int _start(int argc, char *argv[])
{
    int result;
    int source_selected;

    irx_argv_trace_args("usbd", argc, argv);

    printf("Block Device Manager (BDM) v%d.%d\n", MAJOR_VER, MINOR_VER);

    /* 来源描述必须在块设备接入前生效，才能恢复OPL传入的mass编号。 */
    source_selected = bdm_load_source_selection();
    irx_argv_trace_event("usbd", source_selected ? "source=selected" : "source=missing");

    if (RegisterLibraryEntries(&_exp_bdm) != 0) {
        irx_argv_trace_event("usbd", "register=duplicate");
        M_PRINTF("ERROR: Already registered!\n");
        return MODULE_NO_RESIDENT_END;
    }
    irx_argv_trace_event("usbd", "register=fresh");

    // initialize the block device manager
    if (bdm_init() < 0) {
        irx_argv_trace_event("usbd", "bdm-init=failed");
        M_PRINTF("ERROR: BDM init failed!\n");
        return MODULE_NO_RESIDENT_END;
    }
    irx_argv_trace_event("usbd", "bdm-init=ready");

    // initialize the partition driver
    part_init();

    result = bdmfs_fatfs_start(argc, argv);
    irx_argv_trace_event("usbd", result == MODULE_NO_RESIDENT_END ? "fatfs-start=failed" : "fatfs-start=ready");
    /* 只有新IOP环境完整接管BDM后才消费描述，失败或重复加载不能提前删掉它。 */
    if (source_selected && result != MODULE_NO_RESIDENT_END)
        bdm_remove_source_selection();

    return result;
}
