#pragma once

#include "vfs.cfg.h"
#include "vfs.fwd.h"
#include "async.io.fwd.h"
#include "string.str8.fwd.h"
#include "allocator.fwd.h"

#define MEL_VFS_OP_OPEN           1
#define MEL_VFS_OP_CLOSE          2
#define MEL_VFS_OP_READ           3
#define MEL_VFS_OP_WRITE          4
#define MEL_VFS_OP_READV          5
#define MEL_VFS_OP_WRITEV         6
#define MEL_VFS_OP_MAP            7
#define MEL_VFS_OP_UNMAP          8
#define MEL_VFS_OP_STAT           9
#define MEL_VFS_OP_DIR_OPEN       10
#define MEL_VFS_OP_DIR_NEXT       11
#define MEL_VFS_OP_DIR_NEXT_BATCH 12
#define MEL_VFS_OP_DIR_CLOSE      13
#define MEL_VFS_OP_WATCH_OPEN     14
#define MEL_VFS_OP_WATCH_NEXT     15
#define MEL_VFS_OP_WATCH_CLOSE    16
#define MEL_VFS_OP_SYNC           17
#define MEL_VFS_OP_CANCEL         18
#define MEL_VFS_OP_RENAME         19
#define MEL_VFS_OP_DELETE         20
#define MEL_VFS_OP_MKDIR          21

#define MEL_VFS_STATUS_OK               0
#define MEL_VFS_STATUS_PENDING          1
#define MEL_VFS_STATUS_NOT_FOUND        2
#define MEL_VFS_STATUS_EOF              3
#define MEL_VFS_STATUS_PERMISSION       4
#define MEL_VFS_STATUS_UNSUPPORTED      5
#define MEL_VFS_STATUS_CANCELLED        6
#define MEL_VFS_STATUS_IO_ERROR         7
#define MEL_VFS_STATUS_INVALID_ARGUMENT 8
#define MEL_VFS_STATUS_BUFFER_TOO_SMALL 9
#define MEL_VFS_STATUS_TIMEOUT          10
#define MEL_VFS_STATUS_ALREADY_EXISTS   11

#define MEL_VFS_ERRCAT_NONE    0
#define MEL_VFS_ERRCAT_GENERIC 1
#define MEL_VFS_ERRCAT_OS      2
#define MEL_VFS_ERRCAT_BACKEND 3

#define MEL_VFS_OPEN_READ     (1u << 0)
#define MEL_VFS_OPEN_WRITE    (1u << 1)
#define MEL_VFS_OPEN_CREATE   (1u << 2)
#define MEL_VFS_OPEN_TRUNCATE (1u << 3)
#define MEL_VFS_OPEN_DIRECT   (1u << 4)

#define MEL_VFS_MAP_READ  (1u << 0)
#define MEL_VFS_MAP_WRITE (1u << 1)

#define MEL_VFS_WATCH_ADDED    1
#define MEL_VFS_WATCH_MODIFIED 2
#define MEL_VFS_WATCH_REMOVED  3
#define MEL_VFS_WATCH_RENAMED  4

#define MEL_VFS_PRIORITY_LOW    0
#define MEL_VFS_PRIORITY_NORMAL 128
#define MEL_VFS_PRIORITY_HIGH   255

#define MEL_VFS_QOS_LATENCY_CRITICAL 0
#define MEL_VFS_QOS_STREAMING        1
#define MEL_VFS_QOS_BULK             2
#define MEL_VFS_QOS_COUNT            3

#define MEL_VFS_SQE_F_FENCE      (1u << 0)
#define MEL_VFS_SQE_F_LINK_NEXT  (1u << 1)
#define MEL_VFS_SQE_F_DONT_BLOCK (1u << 2)

struct Mel_Vfs_Sqe {
    u64   ticket;
    u32   op;
    u32   flags;
    u8    priority;
    u8    qos_class;
    u64   deadline_ns;
    void* user_data;

    union {
        struct { str8 path; u32 open_flags; }                                       open;
        struct { Mel_Vfs_File file; }                                               close;
        struct { Mel_Vfs_File file; u64 offset; void* dst; usize size; }            read;
        struct { Mel_Vfs_File file; u64 offset; const void* src; usize size; }      write;
        struct { Mel_Vfs_File file; u64 offset; Mel_IoVec* iov; usize iov_cnt; }    readv;
        struct { Mel_Vfs_File file; u64 offset; const Mel_IoVec* iov; usize iov_cnt; } writev;
        struct { Mel_Vfs_File file; u64 offset; usize size; u32 flags; }            map;
        struct { Mel_Vfs_Map map; }                                                 unmap;
        struct { str8 path; }                                                       stat;
        struct { str8 path; }                                                       dir_open;
        struct { Mel_Vfs_Dir dir; Mel_Vfs_Dir_Entry* entry; u8* name_buf; usize name_cap; } dir_next;
        struct { Mel_Vfs_Dir dir; Mel_Vfs_Dir_Entry* entries; usize entry_cap; void* str_blob; usize str_blob_cap; } dir_next_batch;
        struct { Mel_Vfs_Dir dir; }                                                 dir_close;
        struct { str8 path; bool recursive; u32 flags; }                            watch_open;
        struct { Mel_Vfs_Watch watch; u8* path_buf; usize path_cap; }               watch_next;
        struct { Mel_Vfs_Watch watch; }                                             watch_close;
        struct { Mel_Vfs_File file; }                                               sync;
        struct { u64 ticket_to_cancel; }                                            cancel;
        struct { str8 src_path; str8 dst_path; }                                    rename;
        struct { str8 path; }                                                       del;
        struct { str8 path; }                                                       mkdir;
    };
};

struct Mel_Vfs_Cqe {
    u64           ticket;
    u32           op;
    u32           status;
    i64           result;
    Mel_Vfs_Error error;
    void*         user_data;

    union {
        Mel_Vfs_File  file;
        Mel_Vfs_Dir   dir;
        Mel_Vfs_Map   map;
        Mel_Vfs_Watch watch;
        Mel_Vfs_Stat  stat;
        str8          path_str;
    };
};

typedef struct {
    const Mel_Alloc* allocator;
    Mel_Io*          io;
} Mel_Vfs_Desc;

bool  mel_vfs_init(Mel_Vfs* vfs, const Mel_Vfs_Desc* desc);
void  mel_vfs_shutdown(Mel_Vfs* vfs);
u64   mel_vfs_next_ticket(Mel_Vfs* vfs);
i32   mel_vfs_submit(Mel_Vfs* vfs, const Mel_Vfs_Sqe* sqes, i32 count);
i32   mel_vfs_poll(Mel_Vfs* vfs, Mel_Vfs_Cqe* out_cqes, i32 max_count);
bool  mel_vfs_wait(Mel_Vfs* vfs, i32 min_count, u32 timeout_ms);
bool  mel_vfs_poll_ticket(Mel_Vfs* vfs, u64 ticket, Mel_Vfs_Cqe* out_cqe);
bool  mel_vfs_wait_ticket(Mel_Vfs* vfs, u64 ticket, u32 timeout_ms, Mel_Vfs_Cqe* out_cqe);
void* mel_vfs_map_ptr(Mel_Vfs* vfs, Mel_Vfs_Map map, usize* out_size);

#include "vfs.async.inl"
