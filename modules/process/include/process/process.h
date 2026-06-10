#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <future/future.h>

#include <io/stream.h>

#include <process/status.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Vat      Mel_Vat;
typedef struct Mel_Executor Mel_Executor;

typedef struct Mel_Process Mel_Process;

#define MEL_PROCESS_STDIO_INHERIT  0u
#define MEL_PROCESS_STDIO_NULL     1u
#define MEL_PROCESS_STDIO_PIPE     2u
#define MEL_PROCESS_STDIO_REDIRECT 3u

typedef struct
{
    u32         disposition;
    Mel_Stream* redirect;
} Mel_Process_Stdio;

typedef struct
{
    const char* key;
    const char* value;
} Mel_Process_Env_Var;

typedef struct
{
    const char* const*         argv;
    usize                      argc;
    const Mel_Process_Env_Var* env;
    usize                      env_count;
    bool                       env_clear;
    const char*                cwd;

    Mel_Process_Stdio stdin_cfg;
    Mel_Process_Stdio stdout_cfg;
    Mel_Process_Stdio stderr_cfg;
    bool              merge_stderr;

    bool detached;

    Mel_Vat*         vat;
    const Mel_Alloc* alloc;
} Mel_Process_Spawn_Opt;

typedef struct
{
    Mel_Process*       value;
    Mel_Process_Status status;
    i32                os_error;
} Mel_Process_Spawn_Result;

Mel_Process_Spawn_Result mel_process_spawn_opt(Mel_Process_Spawn_Opt opt);
#define mel_process_spawn(...) mel_process_spawn_opt((Mel_Process_Spawn_Opt){ __VA_ARGS__ })

bool mel_process_available(void);

void mel_process_destroy(Mel_Process* p);

i64  mel_process_pid(const Mel_Process* p);
bool mel_process_running(Mel_Process* p);
bool mel_process_detached(const Mel_Process* p);

Mel_Stream* mel_process_stdin(Mel_Process* p);
void        mel_process_close_stdin(Mel_Process* p);
Mel_Stream* mel_process_stdout(Mel_Process* p);
Mel_Stream* mel_process_stderr(Mel_Process* p);

typedef struct
{
    i32                exit_code;
    i32                term_signal;
    Mel_Process_Status status;
} Mel_Process_Exit;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Process_Op;

#define MEL_PROCESS_OP_NULL ((Mel_Process_Op){ 0, 0 })

static inline bool mel_process_op_valid(Mel_Process_Op op) { return op.index != 0 || op.generation != 0; }

typedef struct
{
    Mel_Executor*   deliver;
    Mel_Process_Op* out_op;
} Mel_Process_Wait_Opt;

Mel_Future* mel_process_wait_opt(Mel_Process* p, Mel_Process_Wait_Opt opt);
#define mel_process_wait(p, ...) mel_process_wait_opt((p), (Mel_Process_Wait_Opt){ __VA_ARGS__ })

Mel_Process_Exit mel_process_wait_sync(Mel_Process* p);

bool mel_process_poll(Mel_Process* p, Mel_Process_Exit* out);

bool mel_process_cancel_wait(Mel_Process* p, Mel_Process_Op op);

const Mel_Process_Exit* mel_process_wait_future_result(Mel_Future* f);
void                    mel_process_wait_future_release(Mel_Future* f);

#define MEL_PROCESS_SIGNAL_TERM 0u
#define MEL_PROCESS_SIGNAL_KILL 1u

typedef struct
{
    u32 signal;
} Mel_Process_Kill_Opt;

Mel_Process_Status mel_process_kill_opt(Mel_Process* p, Mel_Process_Kill_Opt opt);
#define mel_process_kill(p, ...) mel_process_kill_opt((p), (Mel_Process_Kill_Opt){ __VA_ARGS__ })

typedef struct
{
    u8*                stdout_data;
    usize              stdout_len;
    u8*                stderr_data;
    usize              stderr_len;
    i32                exit_code;
    i32                term_signal;
    Mel_Process_Status status;
    const Mel_Alloc*   alloc;
} Mel_Process_Output;

typedef struct
{
    const char* const*         argv;
    usize                      argc;
    const Mel_Process_Env_Var* env;
    usize                      env_count;
    bool                       env_clear;
    const char*                cwd;
    const void*                stdin_data;
    usize                      stdin_len;
    bool                       merge_stderr;
    Mel_Executor*              deliver;
    Mel_Vat*                   vat;
    const Mel_Alloc*           alloc;
} Mel_Process_Run_Opt;

Mel_Future* mel_process_run_opt(Mel_Process_Run_Opt opt);
#define mel_process_run(...) mel_process_run_opt((Mel_Process_Run_Opt){ __VA_ARGS__ })

const Mel_Process_Output* mel_process_run_future_result(Mel_Future* f);
void                      mel_process_run_future_release(Mel_Future* f);

#ifdef __cplusplus
}
#endif
