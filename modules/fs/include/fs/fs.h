#pragma once

#include <core/types.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <future/future.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Reactor  Mel_Reactor;
typedef struct Mel_Executor Mel_Executor;

typedef struct Mel_Fs Mel_Fs;

typedef u32 Mel_Fs_Status;

#define MEL_FS_SEVERITY_MASK   0x3u
#define MEL_FS_OK              0u
#define MEL_FS_WARNED          1u
#define MEL_FS_ERROR           2u

#define MEL_FS_CANCELLED       (1u << 2)
#define MEL_FS_NOT_FOUND       (1u << 3)
#define MEL_FS_EXISTS          (1u << 4)
#define MEL_FS_PERMISSION      (1u << 5)
#define MEL_FS_NOT_A_DIRECTORY (1u << 6)
#define MEL_FS_IS_A_DIRECTORY  (1u << 7)
#define MEL_FS_NOT_EMPTY       (1u << 8)
#define MEL_FS_NO_SPACE        (1u << 9)
#define MEL_FS_LOOP            (1u << 10)
#define MEL_FS_NAME_TOO_LONG   (1u << 11)
#define MEL_FS_PARTIAL         (1u << 12)
#define MEL_FS_CROSS_DEVICE    (1u << 13)
#define MEL_FS_READ_ONLY       (1u << 14)
#define MEL_FS_UNAVAILABLE     (1u << 15)

static inline bool mel_fs_failed(Mel_Fs_Status s) { return (s & MEL_FS_SEVERITY_MASK) == MEL_FS_ERROR; }
static inline bool mel_fs_warned(Mel_Fs_Status s) { return (s & MEL_FS_SEVERITY_MASK) == MEL_FS_WARNED; }
static inline bool mel_fs_cancelled(Mel_Fs_Status s) { return (s & MEL_FS_CANCELLED) != 0u; }
static inline bool mel_fs_not_found(Mel_Fs_Status s) { return (s & MEL_FS_NOT_FOUND) != 0u; }

typedef u32 Mel_Fs_Kind;

#define MEL_FS_KIND_NONE    0u
#define MEL_FS_KIND_FILE    (1u << 0)
#define MEL_FS_KIND_DIR     (1u << 1)
#define MEL_FS_KIND_SYMLINK (1u << 2)
#define MEL_FS_KIND_OTHER   (1u << 3)

typedef struct
{
    bool        exists;
    Mel_Fs_Kind kind;
    u64         size_bytes;
    i64         ctime_ns;
    i64         mtime_ns;
    i64         atime_ns;
    u32         mode_bits;
    u64         device_id;
    u64         inode;
    bool        read_only;
} Mel_Fs_Stat;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Fs_Op;

#define MEL_FS_OP_NULL ((Mel_Fs_Op){ 0, 0 })

static inline bool mel_fs_op_valid(Mel_Fs_Op op) { return op.index != 0 || op.generation != 0; }

typedef struct
{
    Mel_Reactor*     reactor;
    const Mel_Alloc* alloc;
    u32              worker_count;
} Mel_Fs_Opt;

Mel_Fs* mel_fs_create_opt(Mel_Fs_Opt opt);
#define mel_fs_create(...) mel_fs_create_opt((Mel_Fs_Opt){ .worker_count = 1, __VA_ARGS__ })

void mel_fs_destroy(Mel_Fs* fs);

bool          mel_fs_available(const Mel_Fs* fs);
Mel_Reactor*  mel_fs_reactor(const Mel_Fs* fs);
Mel_Executor* mel_fs_executor(const Mel_Fs* fs);
u32           mel_fs_pending(const Mel_Fs* fs);

bool mel_fs_cancel(Mel_Fs* fs, Mel_Fs_Op op);

typedef struct
{
    Mel_Fs_Stat   value;
    i32           os_error;
    Mel_Fs_Status status;
} Mel_Fs_Stat_Result;

typedef struct
{
    bool          existed;
    i32           os_error;
    Mel_Fs_Status status;
} Mel_Fs_Bool_Result;

typedef struct
{
    i32           os_error;
    Mel_Fs_Status status;
} Mel_Fs_Void_Result;

typedef struct
{
    str8          value;
    i32           os_error;
    Mel_Fs_Status status;
} Mel_Fs_Path_Result;

typedef struct
{
    u8*           data;
    usize         len;
    i32           os_error;
    Mel_Fs_Status status;
} Mel_Fs_Bytes_Result;

typedef struct
{
    bool          follow_symlinks;
    Mel_Executor* deliver;
    Mel_Fs_Op*    out_op;
} Mel_Fs_Stat_Opt;

Mel_Future* mel_fs_stat_opt(Mel_Fs* fs, str8 path, Mel_Fs_Stat_Opt opt);
#define mel_fs_stat(fs, path, ...) mel_fs_stat_opt((fs), (path), (Mel_Fs_Stat_Opt){ .follow_symlinks = true, __VA_ARGS__ })

typedef struct
{
    Mel_Executor* deliver;
    Mel_Fs_Op*    out_op;
} Mel_Fs_Exists_Opt;

Mel_Future* mel_fs_exists_opt(Mel_Fs* fs, str8 path, Mel_Fs_Exists_Opt opt);
#define mel_fs_exists(fs, path, ...) mel_fs_exists_opt((fs), (path), (Mel_Fs_Exists_Opt){ __VA_ARGS__ })

typedef struct
{
    u32           mode_bits;
    bool          parents;
    Mel_Executor* deliver;
    Mel_Fs_Op*    out_op;
} Mel_Fs_Mkdir_Opt;

#define MEL_FS_MODE_DEFAULT_DIR  0777u
#define MEL_FS_MODE_DEFAULT_FILE 0666u

Mel_Future* mel_fs_mkdir_opt(Mel_Fs* fs, str8 path, Mel_Fs_Mkdir_Opt opt);
#define mel_fs_mkdir(fs, path, ...) mel_fs_mkdir_opt((fs), (path), (Mel_Fs_Mkdir_Opt){ .mode_bits = MEL_FS_MODE_DEFAULT_DIR, .parents = true, __VA_ARGS__ })

typedef struct
{
    bool          recursive;
    Mel_Executor* deliver;
    Mel_Fs_Op*    out_op;
} Mel_Fs_Remove_Opt;

Mel_Future* mel_fs_remove_opt(Mel_Fs* fs, str8 path, Mel_Fs_Remove_Opt opt);
#define mel_fs_remove(fs, path, ...) mel_fs_remove_opt((fs), (path), (Mel_Fs_Remove_Opt){ __VA_ARGS__ })

typedef struct
{
    bool          overwrite;
    Mel_Executor* deliver;
    Mel_Fs_Op*    out_op;
} Mel_Fs_Rename_Opt;

Mel_Future* mel_fs_rename_opt(Mel_Fs* fs, str8 from, str8 to, Mel_Fs_Rename_Opt opt);
#define mel_fs_rename(fs, from, to, ...) mel_fs_rename_opt((fs), (from), (to), (Mel_Fs_Rename_Opt){ .overwrite = true, __VA_ARGS__ })

typedef struct
{
    bool          overwrite;
    bool          atomic;
    Mel_Executor* deliver;
    Mel_Fs_Op*    out_op;
} Mel_Fs_Copy_Opt;

Mel_Future* mel_fs_copy_opt(Mel_Fs* fs, str8 from, str8 to, Mel_Fs_Copy_Opt opt);
#define mel_fs_copy(fs, from, to, ...) mel_fs_copy_opt((fs), (from), (to), (Mel_Fs_Copy_Opt){ .overwrite = true, .atomic = true, __VA_ARGS__ })

typedef struct
{
    Mel_Executor* deliver;
    Mel_Fs_Op*    out_op;
} Mel_Fs_Read_File_Opt;

Mel_Future* mel_fs_read_file_opt(Mel_Fs* fs, str8 path, Mel_Fs_Read_File_Opt opt);
#define mel_fs_read_file(fs, path, ...) mel_fs_read_file_opt((fs), (path), (Mel_Fs_Read_File_Opt){ __VA_ARGS__ })

typedef struct
{
    const u8*     data;
    usize         len;
    bool          create_parents;
    bool          atomic;
    u32           mode_bits;
    Mel_Executor* deliver;
    Mel_Fs_Op*    out_op;
} Mel_Fs_Write_File_Opt;

Mel_Future* mel_fs_write_file_opt(Mel_Fs* fs, str8 path, Mel_Fs_Write_File_Opt opt);
#define mel_fs_write_file(fs, path, ...) mel_fs_write_file_opt((fs), (path), (Mel_Fs_Write_File_Opt){ .atomic = true, .mode_bits = MEL_FS_MODE_DEFAULT_FILE, __VA_ARGS__ })

const Mel_Fs_Stat_Result*  mel_fs_future_stat(Mel_Future* f);
const Mel_Fs_Bool_Result*  mel_fs_future_bool(Mel_Future* f);
const Mel_Fs_Void_Result*  mel_fs_future_void(Mel_Future* f);
const Mel_Fs_Path_Result*  mel_fs_future_path(Mel_Future* f);
const Mel_Fs_Bytes_Result* mel_fs_future_bytes(Mel_Future* f);
void                       mel_fs_future_release(Mel_Future* f);

#ifdef __cplusplus
}
#endif
