#include <bdm.h>
#include <io_common.h>
#include <ioman.h>
#include <irx.h>
#include <loadcore.h>
#include <sifcmd.h>
#include <stdio.h>
#include <string.h>

// #define DEBUG  //需要调试输出时取消注释
#include "include/module_debug.h"

#define MAJOR_VER 2
#define MINOR_VER 1

IRX_ID("bdm", MAJOR_VER, MINOR_VER);

extern struct irx_export_table _exp_bdm;
extern int bdm_init();
extern void part_init();
extern int bdmfs_fatfs_start(int argc, char *argv[]);

/* 此地址由PS2SDK的ELF加载器保留，必须与其常驻区起始地址一致。 */
#define POPS_BOOT_MAILBOX_ADDRESS  0x00094000
#define POPS_BOOT_MAILBOX_MAGIC    0x53504F50
#define POPS_BOOT_MAILBOX_VERSION  1
#define POPS_BOOT_MAILBOX_PATH_MAX 256

typedef struct
{
    u32 magic;
    u16 version;
    u16 size;
    u32 deviceType;
    char vcdPath[POPS_BOOT_MAILBOX_PATH_MAX];
    u32 checksum;
} pops_boot_mailbox_t;

typedef char pops_boot_mailbox_size_must_be_272[(sizeof(pops_boot_mailbox_t) == 272) ? 1 : -1];

static pops_boot_mailbox_t g_pops_boot_mailbox __attribute__((aligned(64)));
static int g_pops_boot_mailbox_valid;

static u32 pops_boot_mailbox_checksum(const pops_boot_mailbox_t *mailbox)
{
    const u8 *data = (const u8 *)mailbox;
    u32 checksum = 0x424F4F54;
    unsigned int i;

    for (i = 0; i < sizeof(*mailbox) - sizeof(mailbox->checksum); i++) {
        checksum = (checksum << 5) | (checksum >> 27);
        checksum ^= data[i];
    }

    return checksum;
}

static int pops_boot_mailbox_is_valid(const pops_boot_mailbox_t *mailbox)
{
    unsigned int i;

    if (mailbox->magic != POPS_BOOT_MAILBOX_MAGIC)
        return -1;
    if (mailbox->version != POPS_BOOT_MAILBOX_VERSION)
        return -1;
    if (mailbox->size != sizeof(*mailbox))
        return -1;
    if (mailbox->deviceType > 3)
        return -1;
    if (mailbox->checksum != pops_boot_mailbox_checksum(mailbox))
        return -1;
    if (mailbox->vcdPath[0] != '0' || mailbox->vcdPath[1] != ':' || mailbox->vcdPath[2] != '/')
        return -1;

    for (i = 3; i < sizeof(mailbox->vcdPath); i++) {
        if (mailbox->vcdPath[i] == '\0')
            return i > 3 ? 0 : -1;
    }

    return -1;
}

static int pops_boot_mailbox_read(void)
{
    SifRpcReceiveData_t receiveData;

    memset(&g_pops_boot_mailbox, 0, sizeof(g_pops_boot_mailbox));
    g_pops_boot_mailbox_valid = 0;

    sceSifInitRpc(0);
    if (sceSifGetOtherData(&receiveData, (void *)POPS_BOOT_MAILBOX_ADDRESS,
                           &g_pops_boot_mailbox, sizeof(g_pops_boot_mailbox), 0) < 0)
        return -1;
    if (pops_boot_mailbox_is_valid(&g_pops_boot_mailbox) < 0)
        return -1;

    g_pops_boot_mailbox_valid = 1;
    return 0;
}

int bdm_get_popstarter_target(unsigned int *deviceType, const char **vcdPath)
{
    if (!g_pops_boot_mailbox_valid || deviceType == NULL || vcdPath == NULL)
        return -1;

    *deviceType = g_pops_boot_mailbox.deviceType;
    *vcdPath = g_pops_boot_mailbox.vcdPath;
    return 0;
}

int _start(int argc, char *argv[])
{
    if (pops_boot_mailbox_read() < 0)
        return MODULE_NO_RESIDENT_END;

    printf("Block Device Manager (BDM) v%d.%d\n", MAJOR_VER, MINOR_VER);

    if (RegisterLibraryEntries(&_exp_bdm) != 0) {
        M_PRINTF("ERROR: Already registered!\n");
        return MODULE_NO_RESIDENT_END;
    }

    // 先初始化块设备管理器，文件系统模块随后会立即使用它。
    if (bdm_init() < 0) {
        M_PRINTF("ERROR: BDM init failed!\n");
        return MODULE_NO_RESIDENT_END;
    }

    // 分区驱动必须在FatFs开始枚举前就绪。
    part_init();

    return bdmfs_fatfs_start(argc, argv); // 两部分都就绪后，沿用文件系统模块的驻留结果。
}
