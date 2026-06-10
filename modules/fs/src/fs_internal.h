#pragma once

#include <fs/fs.h>
#include <fs/dir.h>
#include <fs/paths.h>

#include <allocator/allocator.fwd.h>
#include <collection/slotmap.h>
#include <collection/mpsc.h>
#include <executor/executor.h>
#include <future/future.h>
#include <thread/thread.h>
#include <thread/sem.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MEL_FS_JOB_STAT       0u
#define MEL_FS_JOB_EXISTS     1u
#define MEL_FS_JOB_MKDIR      2u
#define MEL_FS_JOB_REMOVE     3u
#define MEL_FS_JOB_RENAME     4u
#define MEL_FS_JOB_COPY       5u
#define MEL_FS_JOB_READ_FILE  6u
#define MEL_FS_JOB_WRITE_FILE 7u
#define MEL_FS_JOB_ENUMERATE  8u
#define MEL_FS_JOB_GLOB       9u

typedef struct Mel_Fs_Op_Record Mel_Fs_Op_Record;

typedef union
{
    Mel_Fs_Stat_Result  stat;
    Mel_Fs_Bool_Result  boolean;
    Mel_Fs_Void_Result  voided;
    Mel_Fs_Path_Result  path;
    Mel_Fs_Bytes_Result bytes;
    Mel_Fs_Dir_Result   dir;
} Mel_Fs_Result;

struct Mel_Fs_Op_Record
{
    Mel_Future         future;
    Mel_Fs*            fs;
    const Mel_Alloc*   alloc;
    Mel_Executor*      deliver;
    Mel_SlotMap_Handle self;

    Mel_Mpsc_Node queue_node;
    Mel_Task      completion_task;

    u32  kind;
    bool submitted;
    bool settled;
    bool detached;
    bool cancel_requested;

    str8 path_a;
    str8 path_b;

    bool follow_symlinks;
    bool parents;
    bool recursive;
    bool overwrite;
    bool atomic;
    bool create_parents;
    bool stat_entries;
    bool case_insensitive;
    u32  mode_bits;
    u32  batch;

    const u8* write_data;
    usize     write_len;

    str8                 glob_pattern;
    Mel_Fs_Dir_Stream_Cb on_batch;
    void*                stream_user;

    Mel_Fs_Result result;
};

struct Mel_Fs
{
    Mel_Vat*         vat;
    Mel_Executor*    executor;
    const Mel_Alloc* alloc;

    Mel_SlotMap ops;

    Mel_Thread*  workers;
    u32          worker_count;
    Mel_Mpsc     queue;
    Mel_Sem      queue_items;
    _Atomic(u32) running;
    bool         backend_ready;

    u32  pending_posts;
    bool destroying;
};

void mel_fs__op_settle(Mel_Fs_Op_Record* op, Mel_Fs_Status status, i32 os_error);

void mel_fs__job_run(Mel_Fs_Op_Record* op);

bool mel_fs__backend_available(void);

void mel_fs__do_stat(Mel_Fs_Op_Record* op);
void mel_fs__do_exists(Mel_Fs_Op_Record* op);
void mel_fs__do_mkdir(Mel_Fs_Op_Record* op);
void mel_fs__do_remove(Mel_Fs_Op_Record* op);
void mel_fs__do_rename(Mel_Fs_Op_Record* op);
void mel_fs__do_copy(Mel_Fs_Op_Record* op);
void mel_fs__do_read_file(Mel_Fs_Op_Record* op);
void mel_fs__do_write_file(Mel_Fs_Op_Record* op);
void mel_fs__do_enumerate(Mel_Fs_Op_Record* op);
void mel_fs__do_glob(Mel_Fs_Op_Record* op);

Mel_Fs_Path_Result mel_fs__backend_folder(Mel_Fs_Folder folder, const Mel_Alloc* alloc);
Mel_Fs_Path_Result mel_fs__backend_cwd(const Mel_Alloc* alloc);
Mel_Fs_Void_Result mel_fs__backend_chdir(str8 path);

Mel_Fs_Status mel_fs__status_from_errno(i32 err);

#ifdef __cplusplus
}
#endif
