#include <bdm.h>
#include <io_common.h>
#include <ioman.h>
#include <irx.h>
#include <loadcore.h>
#include <stdio.h>

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

static int g_vcd_argv_trace_initialized;

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

static void vcd_argv_trace_result_to_slot(const char *path, int index, const char *prefix, const char *target)
{
    int fd;

    fd = vcd_argv_trace_open(path, 0);
    if (fd < 0)
        return;

    vcd_argv_trace_write_text(fd, "[DEBUG-VCD-ARGV] module=vcd-parser result=");
    if (index < 0) {
        vcd_argv_trace_write_text(fd, "no-match\n");
    } else {
        vcd_argv_trace_write_text(fd, "matched argv[");
        vcd_argv_trace_write_number(fd, index);
        vcd_argv_trace_write_text(fd, "] prefix=");
        vcd_argv_trace_write_text(fd, prefix);
        vcd_argv_trace_write_text(fd, " target=");
        vcd_argv_trace_write_text(fd, target);
        vcd_argv_trace_write(fd, "\n", 1);
    }

    close(fd);
}

void bdm_trace_popstarter_vcd_result(int index, const char *prefix, const char *target)
{
    vcd_argv_trace_result_to_slot("mc0:/POPSTARTER/IRX-ARGV.TXT", index, prefix, target);
    vcd_argv_trace_result_to_slot("mc1:/POPSTARTER/IRX-ARGV.TXT", index, prefix, target);
}

int _start(int argc, char *argv[])
{
    bdm_trace_popstarter_vcd_args("usbd", argc, argv);

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
