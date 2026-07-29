#include "include/assault_mc_log.h"

#include <ioman.h>
#include <io_common.h>
#include <stdarg.h>
#include <stdio.h>
#include <sysclib.h>

#define ASSAULT_MC_LOG_BUF 256

static char g_log_name[16];
static int g_log_ready;

void assault_mc_log_init(const char *filename)
{
    char path[40];
    int slot;
    int fd;
    int i;

    g_log_ready = 0;
    if (!filename)
        filename = "USBD.LOG";

    for (i = 0; i < 15 && filename[i]; i++)
        g_log_name[i] = filename[i];
    g_log_name[i] = '\0';
    if (g_log_name[0] == '\0') {
        g_log_name[0] = 'U';
        g_log_name[1] = '\0';
    }
    g_log_ready = 1;

    /* 每次启动清空旧日志，便于实机对照本轮结果 */
    for (slot = 0; slot < 2; slot++) {
        sprintf(path, "mc%d:POPSTARTER", slot);
        mkdir(path);
        sprintf(path, "mc%d:POPSTARTER/%s", slot, g_log_name);
        fd = open(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
        if (fd >= 0) {
            close(fd);
            break;
        }
    }
}

static int assault_mc_open_log(void)
{
    char path[40];
    int slot;
    int fd;

    if (!g_log_ready)
        assault_mc_log_init("USBD.LOG");

    for (slot = 0; slot < 2; slot++) {
        sprintf(path, "mc%d:POPSTARTER", slot);
        mkdir(path);

        sprintf(path, "mc%d:POPSTARTER/%s", slot, g_log_name);
        /* 使用FIO_*标志，兼容经典MC驱动 */
        fd = open(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_APPEND);
        if (fd < 0) {
            fd = open(path, FIO_O_WRONLY | FIO_O_CREAT);
            if (fd >= 0)
                lseek(fd, 0, FIO_SEEK_END);
        }
        if (fd >= 0)
            return fd;
    }
    return -1;
}

void assault_mc_log(const char *format, ...)
{
    char buf[ASSAULT_MC_LOG_BUF];
    va_list args;
    int len;
    int fd;

    va_start(args, format);
    len = vsprintf(buf, format, args);
    va_end(args);

    if (len <= 0)
        return;
    if (len >= ASSAULT_MC_LOG_BUF)
        len = ASSAULT_MC_LOG_BUF - 1;
    buf[len] = '\0';

    printf("%s", buf);

    fd = assault_mc_open_log();
    if (fd < 0)
        return;
    write(fd, buf, len);
    close(fd);
}
