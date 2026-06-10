#pragma once

#include <core/types.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <future/future.h>

#include <storage/status.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Vat      Mel_Vat;
typedef struct Mel_Executor Mel_Executor;

typedef struct Mel_Storage Mel_Storage;

typedef u32 Mel_Storage_Kind;

#define MEL_STORAGE_KIND_NONE 0u
#define MEL_STORAGE_KIND_FILE (1u << 0)
#define MEL_STORAGE_KIND_DIR  (1u << 1)

typedef struct
{
    bool             exists;
    Mel_Storage_Kind kind;
    u64              size_bytes;
    i64              mtime_ns;
    bool             read_only;
} Mel_Storage_Meta;

typedef struct
{
    u64 total_bytes;
    u64 free_bytes;
    u64 available_bytes;
} Mel_Storage_Space;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Storage_Op;

#define MEL_STORAGE_OP_NULL ((Mel_Storage_Op){ 0, 0 })

static inline bool mel_storage_op_valid(Mel_Storage_Op op) { return op.index != 0 || op.generation != 0; }
static inline bool mel_storage_op_equal(Mel_Storage_Op a, Mel_Storage_Op b) { return a.index == b.index && a.generation == b.generation; }

typedef struct
{
    str8             name;
    Mel_Storage_Kind kind;
    u64              size_bytes;
    i64              mtime_ns;
} Mel_Storage_Entry;

typedef void (*Mel_Storage_Enum_Cb)(const Mel_Storage_Entry* entries, u32 count, void* user);

struct Mel_Storage_Interface;

typedef struct
{
    Mel_Vat*                            vat;
    const Mel_Alloc*                    alloc;
    bool                                writable;
    const struct Mel_Storage_Interface* iface;
    void*                               backend_user;
} Mel_Storage_Opt;

Mel_Storage* mel_storage_create_opt(Mel_Storage_Opt opt);
#define mel_storage_create(...) mel_storage_create_opt((Mel_Storage_Opt){ .writable = true, __VA_ARGS__ })

Mel_Storage* mel_storage_open_title_opt(Mel_Storage_Opt opt);
#define mel_storage_open_title(...) mel_storage_open_title_opt((Mel_Storage_Opt){ __VA_ARGS__ })

Mel_Storage* mel_storage_open_user_opt(Mel_Storage_Opt opt);
#define mel_storage_open_user(...) mel_storage_open_user_opt((Mel_Storage_Opt){ .writable = true, __VA_ARGS__ })

typedef struct
{
    str8             root;
    Mel_Vat*         vat;
    const Mel_Alloc* alloc;
    bool             writable;
    bool             create_root;
} Mel_Storage_Fs_Opt;

Mel_Storage* mel_storage_open_fs_opt(Mel_Storage_Fs_Opt opt);
#define mel_storage_open_fs(...) mel_storage_open_fs_opt((Mel_Storage_Fs_Opt){ .writable = true, .create_root = true, __VA_ARGS__ })

void mel_storage_destroy(Mel_Storage* st);

bool          mel_storage_ready(const Mel_Storage* st);
bool          mel_storage_writable(const Mel_Storage* st);
Mel_Vat*      mel_storage_vat(const Mel_Storage* st);
Mel_Executor* mel_storage_executor(const Mel_Storage* st);
u32           mel_storage_pending(const Mel_Storage* st);

bool mel_storage_path_valid(str8 rel);

bool mel_storage_cancel(Mel_Storage* st, Mel_Storage_Op op);

typedef struct
{
    u8*                data;
    usize              len;
    Mel_Storage_Status status;
} Mel_Storage_Bytes;

typedef struct
{
    u64                value;
    Mel_Storage_Status status;
} Mel_Storage_Size_Result;

typedef struct
{
    Mel_Storage_Status status;
} Mel_Storage_Void_Result;

typedef struct
{
    Mel_Storage_Meta   value;
    Mel_Storage_Status status;
} Mel_Storage_Meta_Result;

typedef struct
{
    Mel_Storage_Space  value;
    Mel_Storage_Status status;
} Mel_Storage_Space_Result;

typedef struct
{
    Mel_Storage_Entry* entries;
    u32                count;
    Mel_Storage_Status status;
} Mel_Storage_List_Result;

typedef struct
{
    usize           expect;
    Mel_Executor*   deliver;
    Mel_Storage_Op* out_op;
} Mel_Storage_Read_Opt;

Mel_Future* mel_storage_read_opt(Mel_Storage* st, str8 rel, Mel_Storage_Read_Opt opt);
#define mel_storage_read(st, rel, ...) mel_storage_read_opt((st), (rel), (Mel_Storage_Read_Opt){ __VA_ARGS__ })

typedef struct
{
    const u8*       data;
    usize           len;
    bool            create_parents;
    bool            atomic;
    Mel_Executor*   deliver;
    Mel_Storage_Op* out_op;
} Mel_Storage_Write_Opt;

Mel_Future* mel_storage_write_opt(Mel_Storage* st, str8 rel, Mel_Storage_Write_Opt opt);
#define mel_storage_write(st, rel, ...) mel_storage_write_opt((st), (rel), (Mel_Storage_Write_Opt){ .create_parents = true, .atomic = true, __VA_ARGS__ })

typedef struct
{
    Mel_Executor*   deliver;
    Mel_Storage_Op* out_op;
} Mel_Storage_Size_Opt;

Mel_Future* mel_storage_size_opt(Mel_Storage* st, str8 rel, Mel_Storage_Size_Opt opt);
#define mel_storage_size(st, rel, ...) mel_storage_size_opt((st), (rel), (Mel_Storage_Size_Opt){ __VA_ARGS__ })

typedef struct
{
    Mel_Executor*   deliver;
    Mel_Storage_Op* out_op;
} Mel_Storage_Meta_Opt;

Mel_Future* mel_storage_meta_opt(Mel_Storage* st, str8 rel, Mel_Storage_Meta_Opt opt);
#define mel_storage_meta(st, rel, ...) mel_storage_meta_opt((st), (rel), (Mel_Storage_Meta_Opt){ __VA_ARGS__ })

typedef struct
{
    bool                stat_entries;
    u32                 batch;
    Mel_Storage_Enum_Cb on_batch;
    void*               stream_user;
    Mel_Executor*       deliver;
    Mel_Storage_Op*     out_op;
} Mel_Storage_Enum_Opt;

Mel_Future* mel_storage_enumerate_opt(Mel_Storage* st, str8 rel, Mel_Storage_Enum_Opt opt);
#define mel_storage_enumerate(st, rel, ...) mel_storage_enumerate_opt((st), (rel), (Mel_Storage_Enum_Opt){ .stat_entries = true, .batch = 64, __VA_ARGS__ })

typedef struct
{
    bool            case_insensitive;
    bool            recursive;
    Mel_Executor*   deliver;
    Mel_Storage_Op* out_op;
} Mel_Storage_Glob_Opt;

Mel_Future* mel_storage_glob_opt(Mel_Storage* st, str8 rel, str8 pattern, Mel_Storage_Glob_Opt opt);
#define mel_storage_glob(st, rel, pattern, ...) mel_storage_glob_opt((st), (rel), (pattern), (Mel_Storage_Glob_Opt){ __VA_ARGS__ })

typedef struct
{
    bool            parents;
    Mel_Executor*   deliver;
    Mel_Storage_Op* out_op;
} Mel_Storage_Mkdir_Opt;

Mel_Future* mel_storage_mkdir_opt(Mel_Storage* st, str8 rel, Mel_Storage_Mkdir_Opt opt);
#define mel_storage_mkdir(st, rel, ...) mel_storage_mkdir_opt((st), (rel), (Mel_Storage_Mkdir_Opt){ .parents = true, __VA_ARGS__ })

typedef struct
{
    bool            recursive;
    Mel_Executor*   deliver;
    Mel_Storage_Op* out_op;
} Mel_Storage_Remove_Opt;

Mel_Future* mel_storage_remove_opt(Mel_Storage* st, str8 rel, Mel_Storage_Remove_Opt opt);
#define mel_storage_remove(st, rel, ...) mel_storage_remove_opt((st), (rel), (Mel_Storage_Remove_Opt){ __VA_ARGS__ })

typedef struct
{
    bool            overwrite;
    Mel_Executor*   deliver;
    Mel_Storage_Op* out_op;
} Mel_Storage_Rename_Opt;

Mel_Future* mel_storage_rename_opt(Mel_Storage* st, str8 from, str8 to, Mel_Storage_Rename_Opt opt);
#define mel_storage_rename(st, from, to, ...) mel_storage_rename_opt((st), (from), (to), (Mel_Storage_Rename_Opt){ .overwrite = true, __VA_ARGS__ })

typedef struct
{
    bool            overwrite;
    bool            atomic;
    Mel_Executor*   deliver;
    Mel_Storage_Op* out_op;
} Mel_Storage_Copy_Opt;

Mel_Future* mel_storage_copy_opt(Mel_Storage* st, str8 from, str8 to, Mel_Storage_Copy_Opt opt);
#define mel_storage_copy(st, from, to, ...) mel_storage_copy_opt((st), (from), (to), (Mel_Storage_Copy_Opt){ .overwrite = true, .atomic = true, __VA_ARGS__ })

typedef struct
{
    Mel_Executor*   deliver;
    Mel_Storage_Op* out_op;
} Mel_Storage_Space_Opt;

Mel_Future* mel_storage_space_opt(Mel_Storage* st, Mel_Storage_Space_Opt opt);
#define mel_storage_space(st, ...) mel_storage_space_opt((st), (Mel_Storage_Space_Opt){ __VA_ARGS__ })

const Mel_Storage_Bytes*        mel_storage_future_bytes(Mel_Future* f);
const Mel_Storage_Size_Result*  mel_storage_future_size(Mel_Future* f);
const Mel_Storage_Void_Result*  mel_storage_future_void(Mel_Future* f);
const Mel_Storage_Meta_Result*  mel_storage_future_meta(Mel_Future* f);
const Mel_Storage_Space_Result* mel_storage_future_space(Mel_Future* f);
const Mel_Storage_List_Result*  mel_storage_future_list(Mel_Future* f);
void                            mel_storage_future_release(Mel_Future* f);

#ifdef __cplusplus
}
#endif
