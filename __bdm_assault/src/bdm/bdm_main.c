#include <bdm.h>
#include <io_common.h>
#include <ioman.h>
#include <irx.h>
#include <loadcore.h>
#include <sifcmd.h>
#include <stdio.h>
#include <string.h>

// #define DEBUG  //comment out this line when not debugging
#include "include/module_debug.h"

#define MAJOR_VER 2
#define MINOR_VER 1

IRX_ID("bdm", MAJOR_VER, MINOR_VER);

extern struct irx_export_table _exp_bdm;
extern int bdm_init();
extern void part_init();
extern int bdmfs_fatfs_start(int argc, char *argv[]);

#define VCD_ARGV_TRACE_MAX_ARGS 16
#define VCD_ARGV_TRACE_MAX_TEXT 255

#define POPS_BOOT_MAILBOX_ADDRESS  0x01FF0000
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

static int g_vcd_argv_trace_initialized;
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

    if (mailbox->magic != POPS_BOOT_MAILBOX_MAGIC ||
        mailbox->version != POPS_BOOT_MAILBOX_VERSION ||
        mailbox->size != sizeof(*mailbox) ||
        mailbox->deviceType > 3 ||
        mailbox->checksum != pops_boot_mailbox_checksum(mailbox))
        return 0;

    if (mailbox->vcdPath[0] != '0' || mailbox->vcdPath[1] != ':' || mailbox->vcdPath[2] != '/')
        return 0;

    for (i = 3; i < sizeof(mailbox->vcdPath); i++) {
        if (mailbox->vcdPath[i] == '\0')
            return i > 3;
    }

    return 0;
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

    if (!pops_boot_mailbox_is_valid(&g_pops_boot_mailbox))
        return -2;

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

static int vcd_argv_trace_strlen(const char *text, int limit)
{
    int length;

    if (text == NULL)
        return 0;

    for (length = 0; length < limit && text[length] != '\0'; length++)
        ;

    return length;
}

static void vcd_argv_trace_write(int fd, const char *text, int length)
{
    int result;
    int total;

    for (total = 0; total < length; total += result) {
        result = write(fd, (void *)&text[total], length - total);
        if (result <= 0)
            break;
    }
}

static void vcd_argv_trace_write_text(int fd, const char *text)
{
    vcd_argv_trace_write(fd, text, vcd_argv_trace_strlen(text, VCD_ARGV_TRACE_MAX_TEXT));
}

static void vcd_argv_trace_write_number(int fd, int value)
{
    char digits[12];
    unsigned int number;
    int length;

    if (value < 0) {
        vcd_argv_trace_write(fd, "-", 1);
        number = 0U - (unsigned int)value;
    } else {
        number = (unsigned int)value;
    }

    length = 0;
    do {
        digits[length++] = '0' + number % 10;
        number /= 10;
    } while (number != 0 && length < (int)sizeof(digits));

    while (length > 0) {
        length--;
        vcd_argv_trace_write(fd, &digits[length], 1);
    }
}

static int vcd_argv_trace_open(const char *path, int truncate)
{
    int fd;

    if (truncate) {
        fd = open(path, FIO_O_WRONLY | FIO_O_TRUNC);
        if (fd < 0)
            fd = open(path, FIO_O_WRONLY | FIO_O_CREAT);
        return fd;
    }

    fd = open(path, FIO_O_WRONLY);
    if (fd < 0) {
        /* mcman会把已存在文件上的FIO_O_CREAT当作重新创建，因此只在首次缺失时使用。 */
        fd = open(path, FIO_O_WRONLY | FIO_O_CREAT);
        if (fd < 0)
            return fd;
    }

    /* rom0:ioman下的记忆卡设备不会可靠执行FIO_O_APPEND，必须显式定位到末尾。 */
    if (lseek(fd, 0, FIO_SEEK_END) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static int vcd_argv_trace_args_to_slot(const char *path, const char *module, int argc, char *argv[], int truncate)
{
    int count;
    int fd;
    int i;

    fd = vcd_argv_trace_open(path, truncate);
    if (fd < 0)
        return 0;

    vcd_argv_trace_write_text(fd, "[DEBUG-VCD-ARGV] module=");
    vcd_argv_trace_write_text(fd, module);
    vcd_argv_trace_write_text(fd, " event=entry argc=");
    vcd_argv_trace_write_number(fd, argc);
    vcd_argv_trace_write(fd, "\n", 1);

    count = argc;
    if (count < 0)
        count = -count;
    if (count > VCD_ARGV_TRACE_MAX_ARGS)
        count = VCD_ARGV_TRACE_MAX_ARGS;

    for (i = 0; i < count; i++) {
        const char *argument = argv != NULL ? argv[i] : NULL;
        int length = vcd_argv_trace_strlen(argument, VCD_ARGV_TRACE_MAX_TEXT);

        vcd_argv_trace_write_text(fd, "[DEBUG-VCD-ARGV] module=");
        vcd_argv_trace_write_text(fd, module);
        vcd_argv_trace_write_text(fd, " argv[");
        vcd_argv_trace_write_number(fd, i);
        vcd_argv_trace_write_text(fd, "]=");
        if (argument == NULL) {
            vcd_argv_trace_write_text(fd, "<null>");
        } else {
            vcd_argv_trace_write(fd, argument, length);
            if (length == VCD_ARGV_TRACE_MAX_TEXT)
                vcd_argv_trace_write_text(fd, "<truncated>");
        }
        vcd_argv_trace_write(fd, "\n", 1);
    }

    if (argc > VCD_ARGV_TRACE_MAX_ARGS || argc < -VCD_ARGV_TRACE_MAX_ARGS)
        vcd_argv_trace_write_text(fd, "[DEBUG-VCD-ARGV] argv-list=<truncated>\n");

    close(fd);
    return 1;
}

void bdm_trace_popstarter_vcd_args(const char *module, int argc, char *argv[])
{
    int truncate;
    int wrote;

    truncate = !g_vcd_argv_trace_initialized;
    wrote = vcd_argv_trace_args_to_slot("mc0:/POPSTARTER/IRX-ARGV.TXT", module, argc, argv, truncate);
    wrote |= vcd_argv_trace_args_to_slot("mc1:/POPSTARTER/IRX-ARGV.TXT", module, argc, argv, truncate);
    if (wrote)
        g_vcd_argv_trace_initialized = 1;
}

static void vcd_argv_trace_mailbox_to_slot(const char *path, int result)
{
    int fd;

    fd = vcd_argv_trace_open(path, 0);
    if (fd < 0)
        return;

    vcd_argv_trace_write_text(fd, "[DEBUG-VCD-ARGV] module=ee-mailbox result=");
    if (result == 0) {
        vcd_argv_trace_write_text(fd, "valid type=");
        vcd_argv_trace_write_number(fd, g_pops_boot_mailbox.deviceType);
        vcd_argv_trace_write_text(fd, " target=");
        vcd_argv_trace_write_text(fd, g_pops_boot_mailbox.vcdPath);
        vcd_argv_trace_write(fd, "\n", 1);
    } else if (result == -1) {
        vcd_argv_trace_write_text(fd, "transfer-failed\n");
    } else {
        vcd_argv_trace_write_text(fd, "invalid\n");
    }

    close(fd);
}

static void vcd_argv_trace_mailbox(int result)
{
    vcd_argv_trace_mailbox_to_slot("mc0:/POPSTARTER/IRX-ARGV.TXT", result);
    vcd_argv_trace_mailbox_to_slot("mc1:/POPSTARTER/IRX-ARGV.TXT", result);
}

int _start(int argc, char *argv[])
{
    int mailboxResult;

    bdm_trace_popstarter_vcd_args("usbd", argc, argv);

    mailboxResult = pops_boot_mailbox_read();
    vcd_argv_trace_mailbox(mailboxResult);
    if (mailboxResult < 0)
        return MODULE_NO_RESIDENT_END;

    printf("Block Device Manager (BDM) v%d.%d\n", MAJOR_VER, MINOR_VER);

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
