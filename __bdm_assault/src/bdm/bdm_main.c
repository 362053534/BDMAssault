#include <bdm.h>
#include <irx.h>
#include <loadcore.h>
#include <sifcmd.h>
#include <stdio.h>
#include <string.h>
#include <thbase.h>

// #define DEBUG  //comment out this line when not debugging
#include "include/module_debug.h"

#define MAJOR_VER 2
#define MINOR_VER 1

#define POPS_BOOT_MAILBOX_ADDRESS  0x00094000
#define POPS_BOOT_MAILBOX_MAGIC    0x53504F50
#define POPS_BOOT_MAILBOX_VERSION  1
#define POPS_BOOT_MAILBOX_PATH_MAX 256
#define POPS_BOOT_VCD_PREFIX       "0:/POPS/"
#define POPS_BOOT_VCD_SUFFIX       ".VCD"
#define POPS_BOOT_WAIT_DELAY_US    200000

typedef struct
{
    u32 magic;
    u32 version;
    s32 deviceType;
    char vcdPath[POPS_BOOT_MAILBOX_PATH_MAX];
    u32 checksum;
} pops_boot_mailbox_t;

typedef char pops_boot_mailbox_size_must_be_272[(sizeof(pops_boot_mailbox_t) == 272) ? 1 : -1];

static pops_boot_mailbox_t g_pops_boot_mailbox __attribute__((aligned(64)));

IRX_ID("bdm", MAJOR_VER, MINOR_VER);

extern struct irx_export_table _exp_bdm;
extern int bdm_init();
extern void part_init();
extern int bdm_set_popstarter_vcd_path(const char *vcdPath);
extern int bdmfs_fatfs_start(int argc, char *argv[]);

static int pops_boot_vcd_suffix_matches(const char *suffix)
{
    return suffix[0] == '.' &&
           (suffix[1] == 'V' || suffix[1] == 'v') &&
           (suffix[2] == 'C' || suffix[2] == 'c') &&
           (suffix[3] == 'D' || suffix[3] == 'd');
}

static u32 pops_boot_mailbox_checksum(const pops_boot_mailbox_t *mailbox)
{
    const u8 *data = (const u8 *)mailbox;
    u32 checksum = 2166136261u;
    unsigned int i;

    for (i = 0; i < sizeof(*mailbox) - sizeof(mailbox->checksum); i++) {
        checksum ^= data[i];
        checksum *= 16777619u;
    }

    return checksum;
}

static int pops_boot_mailbox_is_valid(const pops_boot_mailbox_t *mailbox)
{
    unsigned int pathLength;

    if (mailbox->magic != POPS_BOOT_MAILBOX_MAGIC ||
        mailbox->version != POPS_BOOT_MAILBOX_VERSION)
        return 0;
    if (mailbox->deviceType < 0 || mailbox->deviceType > 3)
        return 0;
    if (mailbox->vcdPath[POPS_BOOT_MAILBOX_PATH_MAX - 1] != '\0')
        return 0;
    if (mailbox->checksum != pops_boot_mailbox_checksum(mailbox))
        return 0;

    pathLength = strlen(mailbox->vcdPath);
    if (pathLength <= sizeof(POPS_BOOT_VCD_PREFIX) - 1 + sizeof(POPS_BOOT_VCD_SUFFIX) - 1)
        return 0;
    if (memcmp(mailbox->vcdPath, POPS_BOOT_VCD_PREFIX, sizeof(POPS_BOOT_VCD_PREFIX) - 1) != 0)
        return 0;
    if (!pops_boot_vcd_suffix_matches(mailbox->vcdPath + pathLength - (sizeof(POPS_BOOT_VCD_SUFFIX) - 1)))
        return 0;

    return 1;
}

static int pops_boot_mailbox_read(void)
{
    SifRpcReceiveData_t receiveData;

    memset(&g_pops_boot_mailbox, 0, sizeof(g_pops_boot_mailbox));
    if (sceSifGetOtherData(&receiveData, (void *)POPS_BOOT_MAILBOX_ADDRESS,
                           &g_pops_boot_mailbox, sizeof(g_pops_boot_mailbox), 0) < 0)
        return -1;

    return pops_boot_mailbox_is_valid(&g_pops_boot_mailbox) ? 0 : -1;
}

static void pops_boot_wait_forever(void)
{
    while (1)
        DelayThread(POPS_BOOT_WAIT_DELAY_US);
}

int _start(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("Block Device Manager (BDM) v%d.%d\n", MAJOR_VER, MINOR_VER);

    /* 验证版不允许回退到PAK，未收到合法VCD路径时永久等待。 */
    if (pops_boot_mailbox_read() < 0)
        pops_boot_wait_forever();

    if (bdm_set_popstarter_vcd_path(g_pops_boot_mailbox.vcdPath) < 0)
        pops_boot_wait_forever();

    if (RegisterLibraryEntries(&_exp_bdm) != 0) {
        M_PRINTF("ERROR: Already registered!\n");
        return MODULE_NO_RESIDENT_END;
    }

    // initialize the block device manager
    if (bdm_init() < 0) {
        M_PRINTF("ERROR: BDM init failed!\n");
        return MODULE_NO_RESIDENT_END;
    }

    // initialize the partition driver
    part_init();

    return bdmfs_fatfs_start(argc, argv); //we return this func because both modules must be ready to work
}
