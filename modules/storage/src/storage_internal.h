#pragma once

#include <storage/storage.h>
#include <storage/backend.h>

#include <allocator/allocator.fwd.h>
#include <collection.slotmap/slotmap.h>
#include <future/future.h>
#include <string/str8.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef union
{
    Mel_Storage_Bytes        bytes;
    Mel_Storage_Size_Result  size;
    Mel_Storage_Void_Result  voided;
    Mel_Storage_Meta_Result  meta;
    Mel_Storage_Space_Result space;
    Mel_Storage_List_Result  list;
} Mel_Storage_Result;

struct Mel_Storage_Job
{
    Mel_Future         future;
    Mel_Storage*       st;
    const Mel_Alloc*   alloc;
    Mel_Executor*      deliver;
    Mel_SlotMap_Handle self;

    Mel_Storage_Job_Kind kind;
    bool                 submitted;
    bool                 settled;
    bool                 cancel_requested;

    str8 path_a;
    str8 path_b;
    str8 pattern;

    usize read_expect;

    const u8* write_data;
    usize     write_len;

    bool create_parents;
    bool atomic;
    bool parents;
    bool recursive;
    bool overwrite;
    bool case_insensitive;
    bool stat_entries;
    u32  batch;

    Mel_Storage_Enum_Cb on_batch;
    void*               stream_user;

    void*       backend_state;
    Mel_Task    backend_task;
    Mel_Future* backend_pending;

    Mel_Storage_Result result;
};

struct Mel_Storage
{
    Mel_Reactor*     reactor;
    Mel_Executor*    executor;
    const Mel_Alloc* alloc;

    const Mel_Storage_Interface* iface;
    void*                        backend_user;

    bool writable;

    Mel_SlotMap ops;
};

void mel_storage__job_settle(Mel_Storage_Job* job, Mel_Storage_Status status);

Mel_Storage* mel_storage__open_fs_folder(u32 folder, Mel_Reactor* reactor, const Mel_Alloc* alloc, bool writable, bool create_root);

#ifdef __cplusplus
}
#endif
