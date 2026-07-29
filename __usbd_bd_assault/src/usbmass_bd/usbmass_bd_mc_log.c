#include "include/assault_mc_log.h"

#include <ioman.h>
#include <intrman.h>
#include <io_common.h>
#include <stdarg.h>
#include <stdio.h>
#include <sysclib.h>

#define ASSAULT_MC_LINE_BUF 256
#define ASSAULT_MC_RING_SIZE 12288

static char g_log_name[16];
static int g_log_ready;
static char g_ring[ASSAULT_MC_RING_SIZE];
static int g_ring_len;
static char g_flush_copy[ASSAULT_MC_RING_SIZE];
static int g_flushed;

static void assault_mc_ring_append(const char *data, int len)
{
    int oldstat;
    int copy;
    int remain;

    if (len <= 0)
        return;

    CpuSuspendIntr(&oldstat);
    remain = ASSAULT_MC_RING_SIZE - 1 - g_ring_len;
    if (remain > 0) {
        copy = (len < remain) ? len : remain;
        memcpy(g_ring + g_ring_len, data, copy);
        g_ring_len += copy;
        g_ring[g_ring_len] = '\0';
    }
    CpuResumeIntr(oldstat);
}

void assault_mc_log_init(const char *filename)
{
    int i;

    g_log_ready = 0;
    g_ring_len = 0;
    g_ring[0] = '\0';
    g_flushed = 0;
    if (!filename)
        filename = "USBHDFSD.LOG";

    for (i = 0; i < 15 && filename[i]; i++)
        g_log_name[i] = filename[i];
    g_log_name[i] = '\0';
    if (g_log_name[0] == '\0') {
        g_log_name[0] = 'U';
        g_log_name[1] = '\0';
    }
    /* 启动阶段绝不访问MC，避免与usbd/MCMAN死锁 */
    g_log_ready = 1;
}

void assault_mc_log(const char *format, ...)
{
    char buf[ASSAULT_MC_LINE_BUF];
    va_list args;
    int len;

    va_start(args, format);
    len = vsprintf(buf, format, args);
    va_end(args);

    if (len <= 0)
        return;
    if (len >= ASSAULT_MC_LINE_BUF)
        len = ASSAULT_MC_LINE_BUF - 1;
    buf[len] = '\0';

    printf("%s", buf);
    assault_mc_ring_append(buf, len);
}

void assault_mc_log_flush(void)
{
    char path[40];
    int slot;
    int fd;
    int len;
    int oldstat;

    /* 整个生命周期只落盘一次 */
    if (g_flushed || !g_log_ready)
        return;

    CpuSuspendIntr(&oldstat);
    len = g_ring_len;
    if (len > 0)
        memcpy(g_flush_copy, g_ring, len);
    g_flushed = 1;
    CpuResumeIntr(oldstat);

    if (len <= 0)
        return;

    for (slot = 0; slot < 2; slot++) {
        sprintf(path, "mc%d:POPSTARTER", slot);
        mkdir(path);
        sprintf(path, "mc%d:POPSTARTER/%s", slot, g_log_name);
        fd = open(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
        if (fd >= 0) {
            write(fd, g_flush_copy, len);
            close(fd);
            return;
        }
    }
}
