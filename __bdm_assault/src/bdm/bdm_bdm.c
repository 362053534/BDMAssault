#include <bdm.h>
#include <ioman.h>
#include <io_common.h>
#include <stdarg.h>
#include <stdio.h>
#include <sysclib.h>
#include <thbase.h>
#include <thevent.h>
#include <thsemap.h>

#include <bd_cache.h>

// #define DEBUG  //comment out this line when not debugging
#include "include/module_debug.h"

struct bdm_mounts
{
    struct block_device *bd; // real block device
    struct block_device *cbd; // cached block device
    struct file_system *fs;
};

#define MAX_CONNECTIONS 20
static struct bdm_mounts g_mount[MAX_CONNECTIONS];
static struct file_system *g_fs[MAX_CONNECTIONS];
static bdm_cb g_cb       = NULL;
static int bdm_event     = -1;
static int bdm_filter_event = -1;
static int bdm_thread_id = -1;
/* BDM HDD POPS：只允许ata进BDM，从入口屏蔽USB */
static int g_ata_only    = 0;

/* Event flag bits */
#define BDM_EVENT_CB_MOUNT  0x01
#define BDM_EVENT_CB_UMOUNT 0x02
#define BDM_EVENT_MOUNT     0x04
#define BDM_EVENT_FILTER    0x08

#define BDM_TRACE_PATH "mc0:/POPSTARTER/BDMHDD-TRACE.TXT"
#define BDM_TRACE_BUFFER_SIZE 512

static int g_trace_lock = -1;
static int g_trace_enabled = 0;
static int g_trace_ready = 0;
static int g_trace_needs_truncate = 1;
static unsigned int g_trace_sequence = 0;

struct bdm_trace_format_context
{
    char *buffer;
    int capacity;
    int length;
};

static void bdm_trace_format_char(void *userdata, int character)
{
    struct bdm_trace_format_context *context;

    context = (struct bdm_trace_format_context *)userdata;
    if (character == 512) {
        context->length = 0;
        return;
    }
    if (character == 513)
        return;

    if (context->length < context->capacity - 1)
        context->buffer[context->length++] = (char)character;
}

static int bdm_trace_open_log(void)
{
    int fd;

    if (g_trace_needs_truncate) {
        mkdir("mc0:/POPSTARTER");
        fd = open(BDM_TRACE_PATH, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC);
        if (fd >= 0) {
            g_trace_needs_truncate = 0;
            return fd;
        }
    }

    /* MCMAN会在已存在文件上收到O_CREAT时重新创建文件，因此追加时不能携带O_CREAT。 */
    fd = open(BDM_TRACE_PATH, FIO_O_WRONLY);
    if (fd < 0) {
        fd = open(BDM_TRACE_PATH, FIO_O_WRONLY | FIO_O_CREAT);
    }
    if (fd >= 0) {
        lseek(fd, 0, FIO_SEEK_END);
        g_trace_needs_truncate = 0;
    }
    return fd;
}

void bdm_trace_init(void)
{
    iop_sema_t sema;
    int fd;

    if (g_trace_lock < 0) {
        sema.initial = 1;
        sema.max = 1;
        sema.option = 0;
        sema.attr = 0;
        g_trace_lock = CreateSema(&sema);
        if (g_trace_lock < 0)
            return;
    }

    WaitSema(g_trace_lock);
    g_trace_enabled = 1;
    g_trace_ready = 0;
    g_trace_needs_truncate = 1;
    g_trace_sequence = 0;
    fd = bdm_trace_open_log();
    if (fd >= 0) {
        close(fd);
        g_trace_ready = 1;
    }
    SignalSema(g_trace_lock);

    bdm_trace_log("TRACE_INIT path=%s ready=%d\n", BDM_TRACE_PATH, g_trace_ready);
}

void bdm_trace_log(const char *format, ...)
{
    char buffer[BDM_TRACE_BUFFER_SIZE];
    struct bdm_trace_format_context format_context;
    va_list args;
    int prefix_length;
    int message_length;
    int total_length;
    int written;
    int result;
    int fd;

    if (!format || !g_trace_enabled)
        return;

    if (g_trace_lock >= 0)
        WaitSema(g_trace_lock);

    prefix_length = sprintf(buffer, "[DEBUG-BDH1 %05u] ", g_trace_sequence++);
    if (prefix_length < 0)
        prefix_length = 0;
    if (prefix_length >= BDM_TRACE_BUFFER_SIZE)
        prefix_length = BDM_TRACE_BUFFER_SIZE - 1;

    format_context.buffer = buffer + prefix_length;
    format_context.capacity = BDM_TRACE_BUFFER_SIZE - prefix_length;
    format_context.length = 0;
    va_start(args, format);
    prnt(&bdm_trace_format_char, &format_context, format, args);
    va_end(args);

    message_length = format_context.length;
    total_length = prefix_length + message_length;
    buffer[total_length] = '\0';

    printf("%s", buffer);

    if (g_trace_enabled) {
        fd = bdm_trace_open_log();
        if (fd >= 0) {
            g_trace_ready = 1;
            written = 0;
            while (written < total_length) {
                result = write(fd, buffer + written, total_length - written);
                if (result <= 0)
                    break;
                written += result;
            }
            close(fd);
        }
    }

    if (g_trace_lock >= 0)
        SignalSema(g_trace_lock);
}

void bdm_RegisterCallback(bdm_cb cb)
{
    int i;

    M_DEBUG("%s\n", __func__);

    g_cb = cb;

    if (g_cb == NULL)
        return;

    // Trigger a mount callback if we already have mounts
    for (i = 0; i < MAX_CONNECTIONS; ++i) {
        if ((g_mount[i].bd != NULL) && (g_mount[i].fs != NULL)) {
            SetEventFlag(bdm_event, BDM_EVENT_CB_MOUNT);
            break;
        }
    }
}

void bdm_set_ata_only(int enable)
{
    u32 bits;

    bdm_trace_log("BDM_FILTER request enable=%d\n", enable ? 1 : 0);
    g_ata_only = enable ? 1 : 0;
    if (bdm_event < 0 || bdm_filter_event < 0)
        return;

    /* 设备筛选交给BDM线程串行执行，避免与异步挂载同时改动g_mount。 */
    SetEventFlag(bdm_event, BDM_EVENT_FILTER);
    WaitEventFlag(bdm_filter_event, 1, WEF_OR | WEF_CLEAR, &bits);
    bdm_trace_log("BDM_FILTER complete enable=%d\n", g_ata_only);
}

void bdm_connect_bd(struct block_device *bd)
{
    int i;

    if (g_ata_only && bd && bd->name && strcmp(bd->name, "ata") != 0) {
        bdm_trace_log("BDM_CONNECT rejected name=%s dev=%u par=%u\n", bd->name, bd->devNr, bd->parNr);
        M_PRINTF("ignoring non-ata device %s%dp%d\n", bd->name, bd->devNr, bd->parNr);
        return;
    }

    bdm_trace_log("BDM_CONNECT name=%s dev=%u par=%u type=0x%02x sector_size=%u offset=%08x%08x count=%08x%08x\n",
                  bd->name, bd->devNr, bd->parNr, bd->parId, bd->sectorSize,
                  ((u32 *)&bd->sectorOffset)[1], ((u32 *)&bd->sectorOffset)[0],
                  ((u32 *)&bd->sectorCount)[1], ((u32 *)&bd->sectorCount)[0]);
    M_PRINTF("connecting device %s%dp%d id=0x%x\n", bd->name, bd->devNr, bd->parNr, bd->parId);

    for (i = 0; i < MAX_CONNECTIONS; ++i) {
        if (g_mount[i].bd == NULL) {
            g_mount[i].bd = bd;
            // Create cache for entire device only (not for the partitions on it)
            g_mount[i].cbd = (bd->parNr == 0) ? bd_cache_create(bd) : NULL;
            // New block device, try to mount it to a filesystem
            SetEventFlag(bdm_event, BDM_EVENT_MOUNT);
            bdm_trace_log("BDM_CONNECT queued slot=%d name=%s dev=%u par=%u\n", i, bd->name, bd->devNr, bd->parNr);
            break;
        }
    }

    if (i >= MAX_CONNECTIONS)
        bdm_trace_log("BDM_CONNECT failed_no_slot name=%s dev=%u par=%u\n", bd->name, bd->devNr, bd->parNr);
}

void bdm_disconnect_bd(struct block_device *bd)
{
    int i;

    bdm_trace_log("BDM_DISCONNECT name=%s dev=%u par=%u\n", bd->name, bd->devNr, bd->parNr);
    M_PRINTF("disconnecting device %s%dp%d id=0x%x\n", bd->name, bd->devNr, bd->parNr, bd->parId);

    for (i = 0; i < MAX_CONNECTIONS; ++i) {
        if (g_mount[i].bd == bd) {
            if (g_mount[i].fs != NULL) {
                // Unmount filesystem
                g_mount[i].fs->disconnect_bd(g_mount[i].cbd != NULL ? g_mount[i].cbd : g_mount[i].bd);
                M_PRINTF("%s%dp%d unmounted from %s\n", bd->name, bd->devNr, bd->parNr, g_mount[i].fs->name);
                g_mount[i].fs = NULL;
            }

            if (g_mount[i].cbd != NULL) {
                bd_cache_destroy(g_mount[i].cbd);
                g_mount[i].cbd = NULL;
            }

            g_mount[i].bd = NULL;

            if (g_cb != NULL)
                SetEventFlag(bdm_event, BDM_EVENT_CB_UMOUNT);
        }
    }
}

void bdm_connect_fs(struct file_system *fs)
{
    int i;

    M_PRINTF("connecting fs %s\n", fs->name);

    for (i = 0; i < MAX_CONNECTIONS; ++i) {
        if (g_fs[i] == NULL) {
            g_fs[i] = fs;
            break;
        }
    }

    // New filesystem, try to mount it to the block devices
    SetEventFlag(bdm_event, BDM_EVENT_MOUNT);
}

void bdm_disconnect_fs(struct file_system *fs)
{
    int i;

    M_PRINTF("disconnecting fs %s\n", fs->name);

    // Unmount fs from block devices
    for (i = 0; i < MAX_CONNECTIONS; ++i) {
        if (g_mount[i].fs == fs) {
            g_mount[i].fs = NULL;
            if (g_cb != NULL)
                SetEventFlag(bdm_event, BDM_EVENT_CB_UMOUNT);
        }
    }

    // Remove fs from list
    for (i = 0; i < MAX_CONNECTIONS; ++i) {
        if (g_fs[i] == fs) {
            g_fs[i] = NULL;
            break;
        }
    }
}

void bdm_get_bd(struct block_device **pbd, unsigned int count)
{
    int i;

    M_DEBUG("%s\n", __func__);

    // Fill pointer array with block device pointers
    for (i = 0; (unsigned int)i < count && i < MAX_CONNECTIONS; i++)
        pbd[i] = g_mount[i].bd;
}

static void bdm_try_mount(struct bdm_mounts *mount)
{
    int i;
    int result;

    M_DEBUG("%s(%s%dp%d)\n", __func__, mount->bd->name, mount->bd->devNr, mount->bd->parNr);

    for (i = 0; i < MAX_CONNECTIONS; ++i) {
        if (g_fs[i] != NULL) {
            bdm_trace_log("BDM_MOUNT try fs=%s name=%s dev=%u par=%u\n",
                          g_fs[i]->name, mount->bd->name, mount->bd->devNr, mount->bd->parNr);
            result = g_fs[i]->connect_bd(mount->cbd != NULL ? mount->cbd : mount->bd);
            bdm_trace_log("BDM_MOUNT result fs=%s name=%s dev=%u par=%u ret=%d\n",
                          g_fs[i]->name, mount->bd->name, mount->bd->devNr, mount->bd->parNr, result);
            if (result == 0) {
                M_PRINTF("%s%dp%d mounted to %s\n", mount->bd->name, mount->bd->devNr, mount->bd->parNr, g_fs[i]->name);
                mount->fs = g_fs[i];
                if (g_cb != NULL)
                    SetEventFlag(bdm_event, BDM_EVENT_CB_MOUNT);
                break;
            }
        }
    }
}

static void bdm_thread(void *arg)
{
    u32 EFBits;
    int i;
    struct block_device *bd;

    (void)arg;

    M_PRINTF("BDM event thread running\n");

    while (1) {
        WaitEventFlag(bdm_event, BDM_EVENT_CB_MOUNT | BDM_EVENT_CB_UMOUNT | BDM_EVENT_MOUNT | BDM_EVENT_FILTER,
                      WEF_OR | WEF_CLEAR, &EFBits);

        if (EFBits & BDM_EVENT_FILTER) {
            int disconnected;

            disconnected = 0;
            if (g_ata_only) {
                for (i = 0; i < MAX_CONNECTIONS; ++i) {
                    bd = g_mount[i].bd;

                    if (bd && bd->name && strcmp(bd->name, "ata") != 0) {
                        bdm_trace_log("BDM_FILTER disconnect name=%s dev=%u par=%u slot=%d\n",
                                      bd->name, bd->devNr, bd->parNr, i);
                        bdm_disconnect_bd(bd);
                        disconnected++;
                    }
                }
            }
            bdm_trace_log("BDM_FILTER worker_done disconnected=%d\n", disconnected);
            SetEventFlag(bdm_filter_event, 1);
        }

        if (EFBits & BDM_EVENT_MOUNT) {
            // Try to mount any unmounted block devices
            for (i = 0; i < MAX_CONNECTIONS; ++i) {
                if ((g_mount[i].bd != NULL) && (g_mount[i].fs == NULL))
                    bdm_try_mount(&g_mount[i]);
            }
        }

        if (EFBits & BDM_EVENT_CB_MOUNT) {
            // Notify callback about changes
            if (g_cb != NULL)
                g_cb(1);
        }

        if (EFBits & BDM_EVENT_CB_UMOUNT) {
            // Notify callback about changes
            if (g_cb != NULL)
                g_cb(0);
        }
    }
}

int bdm_init()
{
    int i, result;
    iop_event_t EventFlagData;
    iop_thread_t ThreadData;

    M_DEBUG("%s\n", __func__);

    for (i = 0; i < MAX_CONNECTIONS; ++i) {
        g_mount[i].bd  = NULL;
        g_mount[i].cbd = NULL;
        g_mount[i].fs  = NULL;
        g_fs[i]        = NULL;
    }

    EventFlagData.attr   = 0;
    EventFlagData.option = 0;
    EventFlagData.bits   = 0;
    result = bdm_event = CreateEventFlag(&EventFlagData);
    if (result < 0) {
        M_DEBUG("ERROR: CreateEventFlag %d\n", result);
        return result;
    }

    result = bdm_filter_event = CreateEventFlag(&EventFlagData);
    if (result < 0) {
        M_DEBUG("ERROR: CreateEventFlag %d\n", result);
        DeleteEventFlag(bdm_event);
        return result;
    }

    ThreadData.attr      = TH_C;
    ThreadData.thread    = bdm_thread;
    ThreadData.option    = 0;
    ThreadData.priority  = 0x30;   // Low priority
    ThreadData.stacksize = 0x4000; // 16KiB：FatFs LFN在栈上，原4KiB易溢出
    result = bdm_thread_id = CreateThread(&ThreadData);
    if (result < 0) {
        M_DEBUG("ERROR: CreateThread %d\n", result);
        DeleteEventFlag(bdm_filter_event);
        DeleteEventFlag(bdm_event);
        return result;
    }

    result = StartThread(bdm_thread_id, NULL);
    if (result < 0) {
        M_DEBUG("ERROR: StartThread %d\n", result);
        DeleteThread(bdm_thread_id);
        DeleteEventFlag(bdm_filter_event);
        DeleteEventFlag(bdm_event);
        return result;
    }

    return 0;
}
