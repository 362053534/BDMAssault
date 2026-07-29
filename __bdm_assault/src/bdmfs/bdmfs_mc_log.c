#include "include/assault_mc_log.h"

#include <ioman.h>
#include <intrman.h>
#include <io_common.h>
#include <stdarg.h>
#include <stdio.h>
#include <sysclib.h>
#include <thbase.h>

#define ASSAULT_MC_LINE_BUF 256
#define ASSAULT_MC_RING_SIZE 12288

static char g_log_name[16];
static int g_log_ready;
static char g_ring[ASSAULT_MC_RING_SIZE];
static int g_ring_len;
static int g_flush_thread_started;
static char g_flush_copy[ASSAULT_MC_RING_SIZE];
static int g_flush_busy;

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

static int assault_mc_open_log(int trunc)
{
    char path[40];
    int slot;
    int fd;
    int flags;

    if (!g_log_ready)
        return -1;

    flags = trunc ? (FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC) :
                    (FIO_O_WRONLY | FIO_O_CREAT | FIO_O_APPEND);

    for (slot = 0; slot < 2; slot++) {
        sprintf(path, "mc%d:POPSTARTER", slot);
        mkdir(path);
        sprintf(path, "mc%d:POPSTARTER/%s", slot, g_log_name);
        fd = open(path, flags);
        if (fd < 0 && !trunc) {
            fd = open(path, FIO_O_WRONLY | FIO_O_CREAT);
            if (fd >= 0)
                lseek(fd, 0, FIO_SEEK_END);
        }
        if (fd >= 0)
            return fd;
    }
    return -1;
}

void assault_mc_log_flush(void)
{
    int len;
    int oldstat;
    int fd;

    CpuSuspendIntr(&oldstat);
    if (g_flush_busy || g_ring_len <= 0) {
        CpuResumeIntr(oldstat);
        return;
    }
    g_flush_busy = 1;
    len = g_ring_len;
    memcpy(g_flush_copy, g_ring, len);
    g_ring_len = 0;
    g_ring[0] = '\0';
    CpuResumeIntr(oldstat);

    fd = assault_mc_open_log(0);
    if (fd >= 0) {
        write(fd, g_flush_copy, len);
        close(fd);
    }

    CpuSuspendIntr(&oldstat);
    g_flush_busy = 0;
    CpuResumeIntr(oldstat);
}

static void assault_mc_flush_thread(void *arg)
{
    (void)arg;
    while (1) {
        DelayThread(500000);
        assault_mc_log_flush();
    }
}

static void assault_mc_start_flush_thread(void)
{
    iop_thread_t thread;
    int tid;

    if (g_flush_thread_started)
        return;

    thread.attr = TH_C;
    thread.option = 0;
    thread.thread = assault_mc_flush_thread;
    thread.stacksize = 0x800;
    thread.priority = 0x40;
    tid = CreateThread(&thread);
    if (tid > 0 && StartThread(tid, NULL) >= 0)
        g_flush_thread_started = 1;
}

void assault_mc_log_init(const char *filename)
{
    char path[40];
    int slot;
    int fd;
    int i;

    g_log_ready = 0;
    g_ring_len = 0;
    g_ring[0] = '\0';
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

    assault_mc_start_flush_thread();
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
