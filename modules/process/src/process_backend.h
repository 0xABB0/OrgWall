#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>

#include <process/process.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Reactor Mel_Reactor;

typedef struct
{
    i32   fd;
    void* handle;
} Mel_Process_Native_Pipe;

typedef struct
{
    i64   pid;
    void* handle;
    void* job;

    Mel_Process_Native_Pipe child_stdin;
    Mel_Process_Native_Pipe child_stdout;
    Mel_Process_Native_Pipe child_stderr;

    Mel_Process_Status status;
    i32                os_error;
} Mel_Process_Native;

typedef struct
{
    const char* const*         argv;
    usize                      argc;
    const Mel_Process_Env_Var* env;
    usize                      env_count;
    bool                       env_clear;
    const char*                cwd;

    u32         stdin_disposition;
    u32         stdout_disposition;
    u32         stderr_disposition;
    i32         stdin_redirect_fd;
    i32         stdout_redirect_fd;
    i32         stderr_redirect_fd;
    bool        merge_stderr;
    bool        detached;

    const Mel_Alloc* alloc;
} Mel_Process_Spawn_Args;

bool mel_process__backend_available(void);

Mel_Process_Native mel_process__backend_spawn(Mel_Process_Spawn_Args args);

bool mel_process__backend_reap(Mel_Process_Native* native, i32* out_exit, i32* out_signal, Mel_Process_Status* out_status);

void mel_process__backend_wait_blocking(Mel_Process_Native* native, i32* out_exit, i32* out_signal, Mel_Process_Status* out_status);

Mel_Process_Status mel_process__backend_signal(Mel_Process_Native* native, u32 signal);

void mel_process__backend_close(Mel_Process_Native* native);

#ifdef __cplusplus
}
#endif
