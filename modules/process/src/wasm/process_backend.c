#include "../process_backend.h"

#include <log/log.h>

bool mel_process__backend_available(void) { return false; }

Mel_Process_Native mel_process__backend_spawn(Mel_Process_Spawn_Args args)
{
    (void)args;
    mel_log_warn("process", "subprocess spawn is unavailable in the wasm/browser sandbox");
    Mel_Process_Native out = { .pid = -1 };
    out.child_stdin.fd = -1;
    out.child_stdout.fd = -1;
    out.child_stderr.fd = -1;
    out.status = MEL_PROCESS_ERROR | MEL_PROCESS_UNAVAILABLE;
    return out;
}

bool mel_process__backend_reap(Mel_Process_Native* native, i32* out_exit, i32* out_signal, Mel_Process_Status* out_status)
{
    (void)native;
    *out_exit = -1;
    *out_signal = 0;
    *out_status = MEL_PROCESS_ERROR | MEL_PROCESS_UNAVAILABLE;
    return true;
}

void mel_process__backend_wait_blocking(Mel_Process_Native* native, i32* out_exit, i32* out_signal, Mel_Process_Status* out_status)
{
    (void)native;
    *out_exit = -1;
    *out_signal = 0;
    *out_status = MEL_PROCESS_ERROR | MEL_PROCESS_UNAVAILABLE;
}

Mel_Process_Status mel_process__backend_signal(Mel_Process_Native* native, u32 signal)
{
    (void)native;
    (void)signal;
    return MEL_PROCESS_ERROR | MEL_PROCESS_UNAVAILABLE;
}

void mel_process__backend_close(Mel_Process_Native* native) { (void)native; }
