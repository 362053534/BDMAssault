/*
  Copyright 2009-2010, jimmikaelkael
  Licenced under Academic Free License version 3.0
*/

#include "types.h"
#include "defs.h"
#include "irx.h"
#include "intrman.h"
#include "iomanX.h"
#include "io_common.h"
#include "sifman.h"
#include "stdio.h"
#include "sysclib.h"
#include "thbase.h"
#include "thsemap.h"
#include "errno.h"
#include "ps2smb.h"

#include "smb_fio.h"
#include "smb.h"
#include "auth.h"
#include "debug.h"

int smbman_io_sema;

// driver ops func tab
static iop_device_ops_t smbman_ops = {
    &smb_init, // init
    &smb_deinit, // deinit
    NOT_SUPPORTED, // format
    &smb_open, // open
    &smb_close, // close
    &smb_read, // read
    &smb_write, // write
    &smb_lseek, // lseek
    NOT_SUPPORTED, // ioctl
    &smb_remove, // remove
    &smb_mkdir, // mkdir
    &smb_rmdir, // rmdir
    &smb_dopen, // dopen
    &smb_dclose, // dclose
    &smb_dread, // dread
    &smb_getstat, // getstat
    NOT_SUPPORTED, // chstat
    &smb_rename, // rename
    &smb_chdir, // chdir
    NOT_SUPPORTED, // sync
    NOT_SUPPORTED, // mount
    NOT_SUPPORTED, // umount
    &smb_lseek64, // lseek64
    &smb_devctl, // devctl
    NOT_SUPPORTED, // symlink
    NOT_SUPPORTED, // readlink
    NOT_SUPPORTED, // ioctl2
};

// driver descriptor
static iop_device_t smbdev = {
    "smb",
    IOP_DT_FS | IOP_DT_FSEXT,
    1,
    "SMB",
    &smbman_ops,
};

#define SMB_NAME_MAX 256

typedef struct
{
    iop_file_t *f;
    int smb_fid;
    s64 filesize;
    s64 position;
    u32 mode;
    char name[SMB_NAME_MAX];
} FHANDLE;

#define MAX_FDHANDLES 32
FHANDLE smbman_fdhandles[MAX_FDHANDLES];

#define SMB_SEARCH_BUF_MAX 4096
#define SMB_PATH_MAX       1024
#define SMB_KEEPALIVE_CHECK_US   2000000
#define SMB_KEEPALIVE_IDLE_TICKS 60
#define SMB_RECONNECT_DELAY_US   2000000
#define SMB_READ_AHEAD_SIZE      8192

static ShareEntry_t ShareList;
static u8 SearchBuf[SMB_SEARCH_BUF_MAX];
static char smb_curdir[SMB_PATH_MAX];
static char smb_curpath[SMB_PATH_MAX];
static char smb_secpath[SMB_PATH_MAX];

typedef struct
{
    FHANDLE *fh;
    s64 offset;
    int length;
    FHANDLE *sequence_fh;
    s64 sequence_next;
    u8 data[SMB_READ_AHEAD_SIZE];
} SMB_READ_AHEAD;

static SMB_READ_AHEAD smb_read_ahead __attribute__((aligned(16)));

static volatile int keepalive_locked = 1;
static int keepalive_tid;
static volatile unsigned int keepalive_idle_ticks;

static int UID = -1;
static int TID = -1;

static smbLogOn_in_t glogon_info;
static smbOpenShare_in_t gopenshare_info;
static int glogon_valid;
static int gopenshare_valid;

static int smb_LogOn(smbLogOn_in_t *logon);
static int smb_OpenShare(smbOpenShare_in_t *openshare);
static int smb_ReconnectUntilReady(void);

//-------------------------------------------------------------------------
static int smb_IsTransportError(int error)
{
    switch (error) {
        case -EPIPE:
        case -ECONNRESET:
        case -ECONNABORTED:
        case -ENOTCONN:
        case -ENETRESET:
        case -ENETUNREACH:
        case -ENETDOWN:
        case -EHOSTUNREACH:
        case -ETIMEDOUT:
            return 1;
        default:
            return 0;
    }
}

//-------------------------------------------------------------------------
static void smb_ReadAheadInvalidate(FHANDLE *fh)
{
    if (fh == NULL || smb_read_ahead.fh == fh) {
        smb_read_ahead.fh     = NULL;
        smb_read_ahead.offset = 0;
        smb_read_ahead.length = 0;
    }

    if (fh == NULL || smb_read_ahead.sequence_fh == fh) {
        smb_read_ahead.sequence_fh   = NULL;
        smb_read_ahead.sequence_next = 0;
    }
}

//-------------------------------------------------------------------------
static void keepalive_lock(void)
{
    keepalive_locked = 1;
    keepalive_idle_ticks = 0;
}

//-------------------------------------------------------------------------
static void keepalive_unlock(void)
{
    keepalive_locked = 0;
    keepalive_idle_ticks = 0;
}

//-------------------------------------------------------------------------
static void smb_io_lock(void)
{
    WaitSema(smbman_io_sema);
    keepalive_idle_ticks = 0;
}

//-------------------------------------------------------------------------
static void smb_io_unlock(void)
{
    keepalive_idle_ticks = 0;
    SignalSema(smbman_io_sema);
}

//-------------------------------------------------------------------------
static void keepalive_thread(void *args)
{
    (void)args;

    while (1) {
        int r;

        DelayThread(SMB_KEEPALIVE_CHECK_US);

        if (keepalive_locked) {
            keepalive_idle_ticks = 0;
            continue;
        }

        if (keepalive_idle_ticks < SMB_KEEPALIVE_IDLE_TICKS)
            keepalive_idle_ticks++;

        if (keepalive_idle_ticks < SMB_KEEPALIVE_IDLE_TICKS)
            continue;

        // 只在IO锁空闲时发送Echo，正常游戏读取不会被后台线程等待或打断。
        if (PollSema(smbman_io_sema) != 0)
            continue;

        // 获取IO锁后重新检查状态，避免与注销或重连竞争。
        if (!keepalive_locked && keepalive_idle_ticks >= SMB_KEEPALIVE_IDLE_TICKS) {
            keepalive_idle_ticks = 0;
            r = smb_Echo("PS2 KEEPALIVE ECHO", 18);
            if (smb_IsTransportError(r))
                smb_ReconnectUntilReady();
        }

        SignalSema(smbman_io_sema);
    }
}

//--------------------------------------------------------------
int smb_init(iop_device_t *dev)
{
    iop_thread_t thread;

    (void)dev;

    // create a mutex for IO ops
    smbman_io_sema = CreateMutex(IOP_MUTEX_UNLOCKED);

    // 启动固定周期的保活线程，读写热路径只需重置空闲计数。
    thread.attr      = TH_C;
    thread.option    = 0;
    thread.thread    = (void *)keepalive_thread;
    thread.stacksize = 0x600;
    thread.priority  = 0x64;

    keepalive_tid = CreateThread(&thread);
    StartThread(keepalive_tid, NULL);

    return 0;
}

//--------------------------------------------------------------
int smb_initdev(void)
{
    int i;

    smb_ReadAheadInvalidate(NULL);

    DelDrv(smbdev.name);
    if (AddDrv((iop_device_t *)&smbdev))
        return 1;

    for (i = 0; i < MAX_FDHANDLES; i++) {
        FHANDLE *fh;

        fh           = (FHANDLE *)&smbman_fdhandles[i];
        fh->f        = NULL;
        fh->smb_fid  = -1;
        fh->filesize = 0;
        fh->position = 0;
        fh->mode     = 0;
    }

    return 0;
}

//--------------------------------------------------------------
int smb_deinit(iop_device_t *dev)
{
    (void)dev;

    TerminateThread(keepalive_tid);
    DeleteThread(keepalive_tid);

    DeleteSema(smbman_io_sema);

    return 0;
}

//--------------------------------------------------------------
static FHANDLE *smbman_getfilefreeslot(void)
{
    int i;

    for (i = 0; i < MAX_FDHANDLES; i++) {
        FHANDLE *fh;

        fh = (FHANDLE *)&smbman_fdhandles[i];
        if (fh->f == NULL)
            return fh;
    }

    return 0;
}

//--------------------------------------------------------------
static char *prepare_path(const char *path, char *full_path, int max_path)
{
    const char *p, *p2;
    int i;

    // Reserve space for 2 backslashes and a NULL.
    strncpy(full_path, smb_curdir, max_path - 3);
    strcat(full_path, "\\");

    // Skip all leading slashes and backslashes.
    p = path;
    while ((*p == '\\') || (*p == '/'))
        p++;

    // Locate the end of the path, ignoring any trailing slashes and backslashes.
    p2 = &path[strlen(path)];
    while ((*p2 == '\\') || (*p2 == '/'))
        p2--;

    // Copy path. Reserve space for a backslash and a NULL
    for (i = strlen(full_path); (p <= p2) && (max_path - i - 2 > 0); p++, i++) { // Convert all slashes along the path to backslashes.
        full_path[i] = (*p == '/') ? '\\' : *p;
    }

    // Append a backslash and null-terminate.
    full_path[i]     = '\\';
    full_path[i + 1] = '\0';

    return full_path;
}

//--------------------------------------------------------------
static int smb_ReopenHandles(void)
{
    int i;

    for (i = 0; i < MAX_FDHANDLES; i++) {
        FHANDLE *fh = (FHANDLE *)&smbman_fdhandles[i];
        s64 filesize;
        int fid;
        int mode;

        if (fh->f == NULL)
            continue;

        if (fh->mode == O_DIROPEN) {
            // 搜索句柄不能跨SMB会话复用，下一次dread会从头重新建立搜索。
            fh->smb_fid = -1;
            continue;
        }

        // 重连时不能再次执行截断，否则会破坏已经打开的文件。
        mode = fh->mode & ~O_TRUNC;
        fid  = smb_OpenAndX(UID, TID, fh->name, &filesize, mode);
        if (fid < 0)
            return fid;

        fh->smb_fid  = fid;
        fh->filesize = filesize;
    }

    return 0;
}

//--------------------------------------------------------------
static int smb_RestoreConnection(void)
{
    u32 capabilities;
    int r;

    if (!glogon_valid)
        return -ENOTCONN;

    // 故障恢复不能等待对端优雅关闭，否则断网时可能永久阻塞。
    smb_AbortConnection();
    UID = -1;
    TID = -1;

    r = smb_Connect(glogon_info.serverIP, glogon_info.serverPort);
    if (r < 0)
        goto failed;

    r = smb_NegotiateProtocol(&capabilities);
    if (r < 0)
        goto failed;

    r = smb_SessionSetupAndX(glogon_info.User, glogon_info.Password, glogon_info.PasswordType, capabilities);
    if (r < 0)
        goto failed;
    UID = r;

    if (gopenshare_valid) {
        r = smb_OpenShare((smbOpenShare_in_t *)&gopenshare_info);
        if (r < 0)
            goto failed;

        r = smb_ReopenHandles();
        if (r < 0)
            goto failed;
    }

    return 0;

failed:
    smb_AbortConnection();
    UID = -1;
    TID = -1;
    return r;
}

//--------------------------------------------------------------
static int smb_ReconnectUntilReady(void)
{
    int r;

    // 重连期间完全停止Echo，成功后从新的120秒空闲周期开始。
    keepalive_lock();
    smb_ReadAheadInvalidate(NULL);

    do {
        r = smb_RestoreConnection();
        if (r < 0 && glogon_valid)
            DelayThread(SMB_RECONNECT_DELAY_US);
    } while (r < 0 && glogon_valid);

    if (r >= 0) {
        keepalive_unlock();
    }

    return r;
}

//--------------------------------------------------------------
static int smb_ReadFileWithReconnect(FHANDLE *fh, s64 position, void *buf, int size)
{
    int r;

    do {
        r = smb_ReadFile(UID, TID, fh->smb_fid, position, buf, size);
        if (!smb_IsTransportError(r))
            break;
        if (smb_ReconnectUntilReady() < 0)
            break;
    } while (1);

    return r;
}

//--------------------------------------------------------------
static int smb_WriteFileWithReconnect(FHANDLE *fh, s64 position, void *buf, int size)
{
    int r;

    do {
        r = smb_WriteFile(UID, TID, fh->smb_fid, position, buf, size);
        if (!smb_IsTransportError(r))
            break;
        if (smb_ReconnectUntilReady() < 0)
            break;
    } while (1);

    return r;
}

//--------------------------------------------------------------
static int smb_ReadCached(FHANDLE *fh, void *buf, int size, int allow_read_ahead)
{
    u8 *dest;
    s64 position;
    int completed, remaining;

    dest      = (u8 *)buf;
    position  = fh->position;
    completed = 0;
    remaining = size;

    while (remaining > 0) {
        int available, r;

        if (smb_read_ahead.fh == fh && position >= smb_read_ahead.offset && position < (smb_read_ahead.offset + smb_read_ahead.length)) {
            available = smb_read_ahead.length - (int)(position - smb_read_ahead.offset);
            if (available > remaining)
                available = remaining;

            memcpy(dest, &smb_read_ahead.data[position - smb_read_ahead.offset], available);
            dest += available;
            position += available;
            completed += available;
            remaining -= available;
            continue;
        }

        // 第一次或跳跃读取只读取请求范围；确认连续后才扩大为预读。
        if (!allow_read_ahead || remaining >= SMB_READ_AHEAD_SIZE) {
            r = smb_ReadFileWithReconnect(fh, position, dest, remaining);
            if (r <= 0)
                return completed > 0 ? completed : r;

            dest += r;
            position += r;
            completed += r;
            remaining -= r;
            continue;
        }

        available = ((fh->filesize - position) > SMB_READ_AHEAD_SIZE) ? SMB_READ_AHEAD_SIZE : (int)(fh->filesize - position);
        if (available <= 0)
            break;

        smb_ReadAheadInvalidate(NULL);
        r = smb_ReadFileWithReconnect(fh, position, smb_read_ahead.data, available);
        if (r <= 0)
            return completed > 0 ? completed : r;

        smb_read_ahead.fh     = fh;
        smb_read_ahead.offset = position;
        smb_read_ahead.length = r;
    }

    return completed;
}

//--------------------------------------------------------------
int smb_open(iop_file_t *f, const char *filename, int flags, int mode)
{
    int r = 0;
    FHANDLE *fh;
    s64 filesize;

    (void)mode;

    if (!filename)
        return -ENOENT;

    if ((UID == -1) || (TID == -1))
        return -ENOTCONN;

    char *path = prepare_path(filename, smb_curpath, SMB_PATH_MAX);

    smb_io_lock();

    fh = smbman_getfilefreeslot();
    if (fh) {
        smb_ReadAheadInvalidate(fh);
        r = smb_OpenAndX(UID, TID, path, &filesize, flags);
        if (r >= 0) {
            f->privdata  = fh;
            fh->f        = f;
            fh->smb_fid  = r;
            fh->mode     = flags;
            fh->filesize = filesize;
            fh->position = 0;
            if (fh->mode & O_TRUNC)
                fh->filesize = 0;
            else if (fh->mode & O_APPEND)
                fh->position = filesize;
            strncpy(fh->name, path, SMB_NAME_MAX);
            r = 0;
        }
    } else
        r = -EMFILE;

    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
int smb_close(iop_file_t *f)
{
    FHANDLE *fh = (FHANDLE *)f->privdata;
    int r       = 0;

    if (fh == NULL || (UID == -1) || (TID == -1) || (fh->smb_fid == -1))
        return -EBADF;

    smb_io_lock();

    if (fh) {
        if (fh->mode != O_DIROPEN) {
            r = smb_Close(UID, TID, fh->smb_fid);
            if (r != 0) {
                goto io_unlock;
            }
        }
        smb_ReadAheadInvalidate(fh);
        memset(fh, 0, sizeof(FHANDLE));
        fh->smb_fid = -1;
        r           = 0;
    }

io_unlock:
    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
void smb_closeAll(void)
{
    int i;

    smb_ReadAheadInvalidate(NULL);

    for (i = 0; i < MAX_FDHANDLES; i++) {
        FHANDLE *fh;

        fh = (FHANDLE *)&smbman_fdhandles[i];
        if (fh->smb_fid != -1)
            smb_Close(UID, TID, fh->smb_fid);
    }
}

//--------------------------------------------------------------
int smb_lseek(iop_file_t *f, int pos, int where)
{
    return (int)smb_lseek64(f, pos, where);
}

//--------------------------------------------------------------
int smb_read(iop_file_t *f, void *buf, int size)
{
    FHANDLE *fh = (FHANDLE *)f->privdata;
    int allow_read_ahead, r;

    if (fh == NULL)
        return -EBADF;

    smb_io_lock();

    if ((UID == -1) || (TID == -1) || (fh->smb_fid == -1)) {
        r = -EBADF;
        goto io_unlock;
    }

    if ((fh->position + size) > fh->filesize)
        size = fh->filesize - fh->position;

    allow_read_ahead = (smb_read_ahead.sequence_fh == fh && smb_read_ahead.sequence_next == fh->position);
    r                = smb_ReadCached(fh, buf, size, allow_read_ahead);

    if (r > 0) {
        fh->position += r;
        smb_read_ahead.sequence_fh   = fh;
        smb_read_ahead.sequence_next = fh->position;
    } else if (r < 0) {
        smb_ReadAheadInvalidate(fh);
    }

io_unlock:
    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
int smb_write(iop_file_t *f, void *buf, int size)
{
    FHANDLE *fh = (FHANDLE *)f->privdata;
    int r;

    if (fh == NULL)
        return -EBADF;

    if ((!(fh->mode & O_RDWR)) && (!(fh->mode & O_WRONLY)))
        return -EACCES;

    smb_io_lock();

    if ((UID == -1) || (TID == -1) || (fh->smb_fid == -1)) {
        r = -EBADF;
        goto io_unlock;
    }

    smb_ReadAheadInvalidate(NULL);
    r = smb_WriteFileWithReconnect(fh, fh->position, buf, size);

    if (r > 0) {
        fh->position += r;
        if (fh->position > fh->filesize)
            fh->filesize += fh->position - fh->filesize;
    }

io_unlock:
    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
int smb_remove(iop_file_t *f, const char *filename)
{
    int r;

    (void)f;

    if (!filename)
        return -ENOENT;

    if ((UID == -1) || (TID == -1))
        return -ENOTCONN;

    char *path = prepare_path(filename, smb_curpath, SMB_PATH_MAX);

    smb_io_lock();

    DPRINTF("smb_remove: filename=%s\n", filename);

    r = smb_Delete(UID, TID, path);
    if (r >= 0)
        smb_ReadAheadInvalidate(NULL);

    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
int smb_mkdir(iop_file_t *f, const char *dirname, int mode)
{
    int r;

    (void)f;
    (void)mode;

    if (!dirname)
        return -ENOENT;

    if ((UID == -1) || (TID == -1))
        return -ENOTCONN;

    char *path = prepare_path(dirname, smb_curpath, SMB_PATH_MAX);

    smb_io_lock();

    r = smb_ManageDirectory(UID, TID, path, SMB_COM_CREATE_DIRECTORY);

    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
int smb_rmdir(iop_file_t *f, const char *dirname)
{
    int r;
    PathInformation_t info;

    (void)f;

    if (!dirname)
        return -ENOENT;

    if ((UID == -1) || (TID == -1))
        return -ENOTCONN;

    char *path = prepare_path(dirname, smb_curpath, SMB_PATH_MAX);

    smb_io_lock();

    r = smb_QueryPathInformation(UID, TID, &info, path);
    if (r < 0) {
        goto io_unlock;
    }
    if (!(info.FileAttributes & EXT_ATTR_DIRECTORY)) {
        r = -ENOTDIR;
        goto io_unlock;
    }

    r = smb_ManageDirectory(UID, TID, path, SMB_COM_DELETE_DIRECTORY);

io_unlock:
    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
static void FileTimeToDate(u64 FileTime, u8 *datetime)
{
    u8 daysPerMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int i;
    u64 time;
    u16 years, days;
    u8 leapdays, months, hours, minutes, seconds;

    time = FileTime / 10000000; // convert to seconds from 100-nanosecond intervals

    years = (u16)(time / ((u64)60 * 60 * 24 * 365)); // hurray for interger division
    time -= years * ((u64)60 * 60 * 24 * 365);       // truncate off the years

    leapdays = (years / 4) - (years / 100) + (years / 400);
    years += 1601; // add base year from FILETIME struct;

    days = (u16)(time / (60 * 60 * 24));
    time -= (unsigned int)days * (60 * 60 * 24);
    days -= leapdays;

    if ((years % 4) == 0 && ((years % 100) != 0 || (years % 400) == 0))
        daysPerMonth[1]++;

    months = 0;
    for (i = 0; i < 12; i++) {
        if (days > daysPerMonth[i]) {
            days -= daysPerMonth[i];
            months++;
        } else
            break;
    }

    if (months >= 12) {
        months -= 12;
        years++;
    }
    hours = (u8)(time / (60 * 60));
    time -= (u16)hours * (60 * 60);

    minutes = (u8)(time / 60);
    time -= minutes * 60;

    seconds = (u8)(time);

    datetime[0] = 0;
    datetime[1] = seconds;
    datetime[2] = minutes;
    datetime[3] = hours;
    datetime[4] = days + 1;
    datetime[5] = months + 1;
    datetime[6] = (u8)(years & 0xFF);
    datetime[7] = (u8)((years >> 8) & 0xFF);
}

static void smb_statFiller(const PathInformation_t *info, iox_stat_t *stat)
{
    FileTimeToDate(info->Created, stat->ctime);
    FileTimeToDate(info->LastAccess, stat->atime);
    FileTimeToDate(info->Change, stat->mtime);

    stat->size   = (int)(info->EndOfFile & 0xffffffff);
    stat->hisize = (int)((info->EndOfFile >> 32) & 0xffffffff);

    stat->mode = (info->FileAttributes & EXT_ATTR_DIRECTORY) ? FIO_S_IFDIR : FIO_S_IFREG;
}

//--------------------------------------------------------------
int smb_dopen(iop_file_t *f, const char *dirname)
{
    int r = 0;
    PathInformation_t info;
    FHANDLE *fh;

    if (!dirname)
        return -ENOENT;

    if ((UID == -1) || (TID == -1))
        return -ENOTCONN;

    char *path = prepare_path(dirname, smb_curpath, SMB_PATH_MAX);

    smb_io_lock();

    // test if the dir exists
    r = smb_QueryPathInformation(UID, TID, &info, path);
    if (r < 0) {
        goto io_unlock;
    }

    if (!(info.FileAttributes & EXT_ATTR_DIRECTORY)) {
        r = -ENOTDIR;
        goto io_unlock;
    }

    fh = smbman_getfilefreeslot();
    if (fh) {
        f->privdata  = fh;
        fh->f        = f;
        fh->mode     = O_DIROPEN;
        fh->filesize = 0;
        fh->position = 0;

        strncpy(fh->name, path, 255);
        if (fh->name[strlen(fh->name) - 1] != '\\')
            strcat(fh->name, "\\");
        strcat(fh->name, "*");

        r = 0;
    } else
        r = -EMFILE;

io_unlock:
    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
int smb_dclose(iop_file_t *f)
{
    return smb_close(f);
}

//--------------------------------------------------------------
int smb_dread(iop_file_t *f, iox_dirent_t *dirent)
{
    FHANDLE *fh = (FHANDLE *)f->privdata;
    int r;

    if ((UID == -1) || (TID == -1))
        return -ENOTCONN;

    smb_io_lock();

    memset((void *)dirent, 0, sizeof(iox_dirent_t));

    SearchInfo_t *info = (SearchInfo_t *)SearchBuf;

    if (fh->smb_fid == -1) {
        r = smb_FindFirstNext2(UID, TID, fh->name, TRANS2_FIND_FIRST2, info);
        if (r < 0) {
            goto io_unlock;
        }
        fh->smb_fid = info->SID;
        r           = 1;
    } else {
        info->SID = fh->smb_fid;
        r         = smb_FindFirstNext2(UID, TID, NULL, TRANS2_FIND_NEXT2, info);
        if (r < 0) {
            goto io_unlock;
        }
        r = 1;
    }

    if (r == 1) {
        smb_statFiller(&info->fileInfo, &dirent->stat);
        strncpy(dirent->name, info->FileName, SMB_NAME_MAX);
    }

io_unlock:
    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
int smb_getstat(iop_file_t *f, const char *filename, iox_stat_t *stat)
{
    int r;
    PathInformation_t info;

    (void)f;

    if (!filename)
        return -ENOENT;

    if ((UID == -1) || (TID == -1))
        return -ENOTCONN;

    char *path = prepare_path(filename, smb_curpath, SMB_PATH_MAX);

    smb_io_lock();

    memset((void *)stat, 0, sizeof(iox_stat_t));

    r = smb_QueryPathInformation(UID, TID, &info, path);
    if (r < 0) {
        goto io_unlock;
    }

    smb_statFiller(&info, stat);

io_unlock:
    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
int smb_rename(iop_file_t *f, const char *oldname, const char *newname)
{
    int r;

    (void)f;

    if ((!oldname) || (!newname))
        return -ENOENT;

    if ((UID == -1) || (TID == -1))
        return -ENOTCONN;

    char *oldpath = prepare_path(oldname, smb_curpath, SMB_PATH_MAX);
    char *newpath = prepare_path(newname, smb_secpath, SMB_PATH_MAX);

    smb_io_lock();

    DPRINTF("smb_rename: oldname=%s newname=%s\n", oldname, newname);

    r = smb_Rename(UID, TID, oldpath, newpath);
    if (r >= 0)
        smb_ReadAheadInvalidate(NULL);

    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
int smb_chdir(iop_file_t *f, const char *dirname)
{
    int r = 0, i;
    PathInformation_t info;

    (void)f;

    if (!dirname)
        return -ENOENT;

    if ((UID == -1) || (TID == -1))
        return -ENOTCONN;

    char *path = prepare_path(dirname, smb_curpath, SMB_PATH_MAX);

    smb_io_lock();

    if ((path[strlen(path) - 2] == '.') && (path[strlen(path) - 1] == '.')) {
        char *p = (char *)smb_curdir;
        for (i = strlen(p) - 1; i >= 0; i--) {
            if (p[i] == '\\') {
                p[i] = 0;
                break;
            }
        }
    } else if (path[strlen(path) - 1] == '.') {
        smb_curdir[0] = 0;
    } else {
        r = smb_QueryPathInformation(UID, TID, &info, path);
        if (r < 0) {
            goto io_unlock;
        }

        if (!(info.FileAttributes & EXT_ATTR_DIRECTORY)) {
            r = -ENOTDIR;
            goto io_unlock;
        }

        strncpy(smb_curdir, path, sizeof(smb_curdir) - 1);
    }

io_unlock:
    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
s64 smb_lseek64(iop_file_t *f, s64 pos, int where)
{
    s64 r;
    FHANDLE *fh = (FHANDLE *)f->privdata;

    smb_io_lock();

    switch (where) {
        case SEEK_CUR:
            r = fh->position + pos;
            if (r > fh->filesize) {
                r = -EINVAL;
                goto io_unlock;
            }
            break;
        case SEEK_SET:
            r = pos;
            if (fh->filesize < pos) {
                r = -EINVAL;
                goto io_unlock;
            }
            break;
        case SEEK_END:
            r = fh->filesize;
            break;
        default:
            r = -EINVAL;
            goto io_unlock;
    }

    fh->position = r;

io_unlock:
    smb_io_unlock();

    return r;
}

//--------------------------------------------------------------
void DMA_sendEE(void *buf, int size, void *EE_addr)
{
    SifDmaTransfer_t dmat;
    int oldstate, id;

    dmat.dest = (void *)EE_addr;
    dmat.size = size;
    dmat.src  = (void *)buf;
    dmat.attr = 0;

    id = 0;
    while (!id) {
        CpuSuspendIntr(&oldstate);
        id = sceSifSetDma(&dmat, 1);
        CpuResumeIntr(oldstate);
    }
    while (sceSifDmaStat(id) >= 0)
        ;
}

//--------------------------------------------------------------
static void smb_GetPasswordHashes(smbGetPasswordHashes_in_t *in, smbGetPasswordHashes_out_t *out)
{
    LM_Password_Hash((const unsigned char *)in->password, (unsigned char *)out->LMhash);
    NTLM_Password_Hash((const unsigned char *)in->password, (unsigned char *)out->NTLMhash);
}

//--------------------------------------------------------------
static int smb_LogOff(void);

static int smb_LogOn(smbLogOn_in_t *logon)
{
    u32 capabilities;
    int r;

    if (UID != -1) {
        smb_LogOff();
    }

    r = smb_Connect(logon->serverIP, logon->serverPort);
    if (r < 0)
        return -SMB_DEVCTL_LOGON_ERR_CONN;

    r = smb_NegotiateProtocol(&capabilities);
    if (r < 0)
        return -SMB_DEVCTL_LOGON_ERR_PROT;

    r = smb_SessionSetupAndX(logon->User, logon->Password, logon->PasswordType, capabilities);
    if (r < 0)
        return -SMB_DEVCTL_LOGON_ERR_LOGON;

    UID = r;

    memcpy((void *)&glogon_info, (void *)logon, sizeof(smbLogOn_in_t));
    glogon_valid = 1;

    keepalive_unlock();

    return 0;
}

//--------------------------------------------------------------
static int smb_LogOff(void)
{
    int r;

    if (UID == -1)
        return -ENOTCONN;

    if (TID != -1) {
        smb_closeAll();
        smb_TreeDisconnect(UID, TID);
        TID = -1;
    }

    r = smb_LogOffAndX(UID);
    if (r < 0)
        return r;

    UID = -1;
    glogon_valid    = 0;
    gopenshare_valid = 0;

    keepalive_lock();

    smb_Disconnect();

    return 0;
}

//--------------------------------------------------------------
static int smb_GetShareList(smbGetShareList_in_t *getsharelist)
{
    int i, r, sharecount, shareindex;
    char tree_str[64];
    server_specs_t *specs;

    specs = (server_specs_t *)getServerSpecs();

    if (TID != -1) {
        smb_TreeDisconnect(UID, TID);
        TID = -1;
    }

    // Tree Connect on IPC slot
    sprintf(tree_str, "\\\\%s\\IPC$", specs->ServerIP);
    r = smb_TreeConnectAndX(UID, tree_str, NULL, 0);
    if (r < 0)
        return r;

    TID = r;

    if (UID == -1)
        return -ENOTCONN;

    // does a 1st enum to count shares (+IPC)
    r = smb_NetShareEnum(UID, TID, (ShareEntry_t *)&ShareList, 0, 0);
    if (r < 0)
        return r;

    sharecount = r;
    shareindex = 0;

    // now we list the following shares if any
    for (i = 0; i < sharecount; i++) {

        r = smb_NetShareEnum(UID, TID, (ShareEntry_t *)&ShareList, i, 1);
        if (r < 0)
            return r;

        // if the entry is not IPC, we send it on EE, and increment shareindex
        if ((strcmp(ShareList.ShareName, "IPC$")) && (shareindex < getsharelist->maxent)) {
            DMA_sendEE((void *)&ShareList, sizeof(ShareList), (void *)(getsharelist->EE_addr + (shareindex * sizeof(ShareEntry_t))));
            shareindex++;
        }
    }

    // disconnect the tree
    r = smb_TreeDisconnect(UID, TID);
    if (r < 0)
        return r;

    TID = -1;

    // return the number of shares
    return shareindex;
}

//--------------------------------------------------------------
static int split_share_path(const char *configured, char *share, int shareSize, char *root, int rootSize)
{
    int source, shareLength, rootLength;

    if ((configured == NULL) || (share == NULL) || (root == NULL) ||
        (shareSize < 2) || (rootSize < 1))
        return -EINVAL;

    source      = 0;
    shareLength = 0;
    while ((source < shareSize) && (configured[source] != '\0') &&
           (configured[source] != '/') && (configured[source] != '\\')) {
        if (shareLength >= shareSize - 1)
            return -EINVAL;
        share[shareLength++] = configured[source++];
    }

    if ((shareLength == 0) || (source >= shareSize))
        return -EINVAL;
    share[shareLength] = '\0';

    // 第一个分隔符之前是真实共享名，后续内容作为共享内的根目录。
    while ((source < shareSize) &&
           ((configured[source] == '/') || (configured[source] == '\\')))
        source++;

    rootLength = 0;
    while ((source < shareSize) && (configured[source] != '\0')) {
        if ((configured[source] == '/') || (configured[source] == '\\')) {
            while ((source + 1 < shareSize) &&
                   ((configured[source + 1] == '/') || (configured[source + 1] == '\\')))
                source++;

            if ((source + 1 < shareSize) && (configured[source + 1] != '\0') && (rootLength > 0)) {
                if (rootLength >= rootSize - 1)
                    return -EINVAL;
                root[rootLength++] = '\\';
            }
        } else {
            if (rootLength >= rootSize - 1)
                return -EINVAL;
            root[rootLength++] = configured[source];
        }
        source++;
    }

    if (source >= shareSize)
        return -EINVAL;

    root[rootLength] = '\0';
    return 0;
}

//--------------------------------------------------------------
static int smb_OpenShare(smbOpenShare_in_t *openshare)
{
    int r;
    char share_name[sizeof(openshare->ShareName)];
    char root_path[sizeof(openshare->ShareName)];
    char tree_str[274];
    server_specs_t *specs;

    r = split_share_path(openshare->ShareName, share_name, sizeof(share_name), root_path, sizeof(root_path));
    if (r < 0)
        return r;

    specs = (server_specs_t *)getServerSpecs();

    if (TID != -1) {
        smb_TreeDisconnect(UID, TID);
        TID = -1;
    }

    sprintf(tree_str, "\\\\%s\\%s", specs->ServerIP, share_name);
    r = smb_TreeConnectAndX(UID, tree_str, openshare->Password, openshare->PasswordType);
    if (r < 0)
        return r;

    TID = r;
    strcpy(smb_curdir, root_path);

    // 保留原始复合路径，断线重连时会再次恢复同一个根目录。
    memcpy((void *)&gopenshare_info, (void *)openshare, sizeof(smbOpenShare_in_t));
    gopenshare_valid = 1;

    return 0;
}

//--------------------------------------------------------------
static int smb_CloseShare(void)
{
    int r;

    if (TID == -1)
        return -ENOTCONN;

    smb_closeAll();

    r = smb_TreeDisconnect(UID, TID);
    if (r < 0)
        return r;

    TID = -1;
    gopenshare_valid = 0;

    return 0;
}

//--------------------------------------------------------------
static int smb_EchoServer(smbEcho_in_t *echo)
{
    return smb_Echo(echo->echo, echo->len);
}

//--------------------------------------------------------------
static int smb_QueryDiskInfo(smbQueryDiskInfo_out_t *querydiskinfo)
{
    if ((UID == -1) || (TID == -1))
        return -ENOTCONN;

    return smb_QueryInformationDisk(UID, TID, querydiskinfo);
}

//--------------------------------------------------------------
int smb_devctl(iop_file_t *f, const char *devname, int cmd, void *arg, unsigned int arglen, void *bufp, unsigned int buflen)
{
    int r = 0;

    (void)f;
    (void)devname;
    (void)arglen;
    (void)buflen;

    smb_io_lock();

    switch (cmd) {

        case SMB_DEVCTL_GETPASSWORDHASHES:
            smb_GetPasswordHashes((smbGetPasswordHashes_in_t *)arg, (smbGetPasswordHashes_out_t *)bufp);
            r = 0;
            break;

        case SMB_DEVCTL_LOGON:
            r = smb_LogOn((smbLogOn_in_t *)arg);
            break;

        case SMB_DEVCTL_LOGOFF:
            r = smb_LogOff();
            break;

        case SMB_DEVCTL_GETSHARELIST:
            r = smb_GetShareList((smbGetShareList_in_t *)arg);
            break;

        case SMB_DEVCTL_OPENSHARE:
            r = smb_OpenShare((smbOpenShare_in_t *)arg);
            break;

        case SMB_DEVCTL_CLOSESHARE:
            r = smb_CloseShare();
            break;

        case SMB_DEVCTL_ECHO:
            r = smb_EchoServer((smbEcho_in_t *)arg);
            break;

        case SMB_DEVCTL_QUERYDISKINFO:
            r = smb_QueryDiskInfo((smbQueryDiskInfo_out_t *)bufp);
            break;

        default:
            r = -EINVAL;
    }

    smb_io_unlock();

    return r;
}
