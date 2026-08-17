#ifndef IRX_ARGV_TRACE_H
#define IRX_ARGV_TRACE_H

#include <io_common.h>
#include <ioman.h>

#define IRX_ARGV_TRACE_MAX_ARGS 16
#define IRX_ARGV_TRACE_MAX_TEXT 255

static int irx_argv_trace_strlen(const char *text, int limit)
{
    int length;

    if (text == NULL)
        return 0;

    for (length = 0; length < limit && text[length] != '\0'; length++)
        ;

    return length;
}

static void irx_argv_trace_write(int fd, const char *text, int length)
{
    int total;

    for (total = 0; total < length;) {
        int result = write(fd, (void *)&text[total], length - total);

        if (result <= 0)
            break;
        total += result;
    }
}

static void irx_argv_trace_write_text(int fd, const char *text)
{
    irx_argv_trace_write(fd, text, irx_argv_trace_strlen(text, IRX_ARGV_TRACE_MAX_TEXT));
}

static void irx_argv_trace_write_number(int fd, int value)
{
    char digits[12];
    unsigned int number;
    int length;

    if (value < 0) {
        irx_argv_trace_write(fd, "-", 1);
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
        irx_argv_trace_write(fd, &digits[length], 1);
    }
}

static int irx_argv_trace_open(const char *path)
{
    int fd;

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

static void irx_argv_trace_event_to_slot(const char *path, const char *module, const char *event)
{
    int fd;

    fd = irx_argv_trace_open(path);
    if (fd < 0)
        return;

    irx_argv_trace_write_text(fd, "[DEBUG-IRX-ARGV] module=");
    irx_argv_trace_write_text(fd, module);
    irx_argv_trace_write_text(fd, " event=");
    irx_argv_trace_write_text(fd, event);
    irx_argv_trace_write(fd, "\n", 1);
    close(fd);
}

static void irx_argv_trace_event(const char *module, const char *event)
{
    irx_argv_trace_event_to_slot("mc0:/POPSTARTER/IRX-ARGV.TXT", module, event);
    irx_argv_trace_event_to_slot("mc1:/POPSTARTER/IRX-ARGV.TXT", module, event);
}

static void irx_argv_trace_args_to_slot(const char *path, const char *module, int argc, char *argv[])
{
    int count;
    int fd;
    int i;

    fd = irx_argv_trace_open(path);
    if (fd < 0)
        return;

    irx_argv_trace_write_text(fd, "[DEBUG-IRX-ARGV] module=");
    irx_argv_trace_write_text(fd, module);
    irx_argv_trace_write_text(fd, " event=entry argc=");
    irx_argv_trace_write_number(fd, argc);
    irx_argv_trace_write(fd, "\n", 1);

    count = argc;
    if (count < 0)
        count = -count;
    if (count > IRX_ARGV_TRACE_MAX_ARGS)
        count = IRX_ARGV_TRACE_MAX_ARGS;

    for (i = 0; i < count; i++) {
        const char *argument = argv != NULL ? argv[i] : NULL;
        int length = irx_argv_trace_strlen(argument, IRX_ARGV_TRACE_MAX_TEXT);

        irx_argv_trace_write_text(fd, "[DEBUG-IRX-ARGV] module=");
        irx_argv_trace_write_text(fd, module);
        irx_argv_trace_write_text(fd, " argv[");
        irx_argv_trace_write_number(fd, i);
        irx_argv_trace_write_text(fd, "]=");
        if (argument == NULL) {
            irx_argv_trace_write_text(fd, "<null>");
        } else {
            irx_argv_trace_write(fd, argument, length);
            if (length == IRX_ARGV_TRACE_MAX_TEXT)
                irx_argv_trace_write_text(fd, "<truncated>");
        }
        irx_argv_trace_write(fd, "\n", 1);
    }

    if (argc > IRX_ARGV_TRACE_MAX_ARGS || argc < -IRX_ARGV_TRACE_MAX_ARGS)
        irx_argv_trace_write_text(fd, "[DEBUG-IRX-ARGV] argv-list=<truncated>\n");

    close(fd);
}

static void irx_argv_trace_args(const char *module, int argc, char *argv[])
{
    irx_argv_trace_args_to_slot("mc0:/POPSTARTER/IRX-ARGV.TXT", module, argc, argv);
    irx_argv_trace_args_to_slot("mc1:/POPSTARTER/IRX-ARGV.TXT", module, argc, argv);
}

#endif
