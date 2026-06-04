#pragma once

#include <fs/fs.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    str8        name;
    Mel_Fs_Kind kind;
    u64         size_bytes;
    i64         mtime_ns;
} Mel_Fs_Dir_Entry;

typedef void (*Mel_Fs_Dir_Stream_Cb)(const Mel_Fs_Dir_Entry* entries, u32 count, void* user);

typedef struct
{
    bool                 stat_entries;
    u32                  batch;
    Mel_Fs_Dir_Stream_Cb on_batch;
    void*                stream_user;
    Mel_Executor*        deliver;
    Mel_Fs_Op*           out_op;
} Mel_Fs_Enumerate_Opt;

typedef struct
{
    Mel_Fs_Dir_Entry* entries;
    u32               count;
    i32               os_error;
    Mel_Fs_Status     status;
} Mel_Fs_Dir_Result;

Mel_Future* mel_fs_enumerate_opt(Mel_Fs* fs, str8 path, Mel_Fs_Enumerate_Opt opt);
#define mel_fs_enumerate(fs, path, ...) mel_fs_enumerate_opt((fs), (path), (Mel_Fs_Enumerate_Opt){ .stat_entries = true, .batch = 64, __VA_ARGS__ })

const Mel_Fs_Dir_Result* mel_fs_future_dir(Mel_Future* f);

typedef struct
{
    bool          case_insensitive;
    bool          recursive;
    Mel_Executor* deliver;
    Mel_Fs_Op*    out_op;
} Mel_Fs_Glob_Opt;

Mel_Future* mel_fs_glob_opt(Mel_Fs* fs, str8 root, str8 pattern, Mel_Fs_Glob_Opt opt);
#define mel_fs_glob(fs, root, pattern, ...) mel_fs_glob_opt((fs), (root), (pattern), (Mel_Fs_Glob_Opt){ __VA_ARGS__ })

bool mel_fs_glob_match(str8 pattern, str8 name, bool case_insensitive);

#ifdef __cplusplus
}
#endif
