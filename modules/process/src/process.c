#include <process/process.h>

#include "process_backend.h"
#include "process_pipe.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <future/future.h>
#include <executor/executor.h>
#include <reactor/reactor.h>
#include <collection/list.h>
#include <log/log.h>

#include <assert.h>
#include <string.h>

#define MEL_PROCESS_REAP_INTERVAL_NS (2 * 1000 * 1000)

typedef struct
{
    Mel_Future       future;
    Mel_Process_Exit result;
    const Mel_Alloc* alloc;
    bool             owned;
} Wait_Op;

struct Mel_Process
{
    Mel_Process_Native native;
    Mel_Reactor*       reactor;
    const Mel_Alloc*   alloc;
    bool               detached;

    Mel_Stream* stdin_stream;
    Mel_Stream* stdout_stream;
    Mel_Stream* stderr_stream;

    bool             reaped;
    Mel_Process_Exit exit;

    Wait_Op*            pending_wait;
    u32                 wait_generation;
    Mel_Reactor_Source* reap_source;

    Wait_Op scratch;
    bool    scratch_busy;
};

static Mel_Future_Status wait_future_status_from(Mel_Process_Status status)
{
    Mel_Future_Status fs = status & MEL_FUTURE_SEVERITY_MASK;
    if (status & MEL_PROCESS_CANCELLED)
        fs |= MEL_FUTURE_CANCELLED;
    return fs;
}

static Wait_Op* wait_op_new(const Mel_Alloc* alloc)
{
    Wait_Op* op = mel_alloc_type(alloc, Wait_Op);
    if (!op)
        return NULL;
    memset(op, 0, sizeof *op);
    op->alloc = alloc;
    op->owned = true;
    mel_future_init(&op->future, NULL, alloc);
    op->future.value = &op->result;
    return op;
}

static Wait_Op* wait_op_sync(Mel_Process* p)
{
    assert(!p->scratch_busy);
    p->scratch_busy = true;
    Wait_Op* op = &p->scratch;
    memset(&op->result, 0, sizeof op->result);
    op->alloc = p->alloc;
    op->owned = false;
    mel_future_init(&op->future, NULL, p->alloc);
    op->future.value = &op->result;
    return op;
}

static Mel_Future* wait_op_resolve(Wait_Op* op, Mel_Process_Exit ex)
{
    op->result = ex;
    if (ex.status & MEL_PROCESS_CANCELLED)
        mel_future_cancel(&op->future);
    else
        mel_future_resolve(&op->future, &op->result, wait_future_status_from(ex.status));
    return &op->future;
}

static bool process_try_reap(Mel_Process* p)
{
    if (p->reaped)
        return true;
    i32                exit_code = 0;
    i32                term_signal = 0;
    Mel_Process_Status st = MEL_PROCESS_OK;
    if (!mel_process__backend_reap(&p->native, &exit_code, &term_signal, &st))
        return false;
    p->reaped = true;
    p->exit.exit_code = exit_code;
    p->exit.term_signal = term_signal;
    p->exit.status = st;
    return true;
}

static void process_resolve_pending(Mel_Process* p)
{
    Wait_Op* op = p->pending_wait;
    if (!op)
        return;
    p->pending_wait = NULL;
    wait_op_resolve(op, p->exit);
}

static bool reap_tick(void* user)
{
    Mel_Process* p = (Mel_Process*)user;
    if (process_try_reap(p))
    {
        if (p->reap_source)
        {
            Mel_Reactor_Source* src = p->reap_source;
            p->reap_source = NULL;
            mel_reactor_source_destroy(src);
        }
        process_resolve_pending(p);
        return false;
    }
    return true;
}

bool mel_process_available(void) { return mel_process__backend_available(); }

static void close_stream(Mel_Stream** s)
{
    if (*s)
    {
        mel_stream_destroy(*s);
        *s = NULL;
    }
}

Mel_Process_Spawn_Result mel_process_spawn_opt(Mel_Process_Spawn_Opt opt)
{
    Mel_Process_Spawn_Result out = { 0 };

    if (!opt.argv || opt.argc == 0 || !opt.argv[0])
    {
        mel_log_error("process", "spawn: argv with at least argv[0] is required");
        out.status = MEL_PROCESS_ERROR | MEL_PROCESS_SPAWN_FAILED;
        return out;
    }
    if (!mel_process__backend_available())
    {
        mel_log_warn("process", "spawn: no process backend on this platform");
        out.status = MEL_PROCESS_ERROR | MEL_PROCESS_UNAVAILABLE;
        return out;
    }

    bool wants_pipe = opt.stdin_cfg.disposition == MEL_PROCESS_STDIO_PIPE || opt.stdout_cfg.disposition == MEL_PROCESS_STDIO_PIPE || opt.stderr_cfg.disposition == MEL_PROCESS_STDIO_PIPE;
    if (opt.detached && wants_pipe)
    {
        mel_log_error("process", "spawn: detached mode cannot pipe stdio (no owner to drain it)");
        out.status = MEL_PROCESS_ERROR | MEL_PROCESS_SPAWN_FAILED;
        return out;
    }
    if (wants_pipe && !opt.reactor)
    {
        mel_log_error("process", "spawn: piped stdio requires a reactor for async byte streams");
        out.status = MEL_PROCESS_ERROR | MEL_PROCESS_SPAWN_FAILED;
        return out;
    }

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    Mel_Process* p = mel_alloc_type(alloc, Mel_Process);
    if (!p)
    {
        out.status = MEL_PROCESS_ERROR | MEL_PROCESS_NO_MEMORY;
        return out;
    }
    memset(p, 0, sizeof *p);
    p->reactor = opt.reactor;
    p->alloc = alloc;
    p->detached = opt.detached;

    i32 stdin_redirect = -1, stdout_redirect = -1, stderr_redirect = -1;
    if (opt.stdin_cfg.disposition == MEL_PROCESS_STDIO_REDIRECT && !mel_process__pipe_fd(opt.stdin_cfg.redirect, &stdin_redirect))
        (void)mel_stream_native_fd(opt.stdin_cfg.redirect, &stdin_redirect);
    if (opt.stdout_cfg.disposition == MEL_PROCESS_STDIO_REDIRECT && !mel_process__pipe_fd(opt.stdout_cfg.redirect, &stdout_redirect))
        (void)mel_stream_native_fd(opt.stdout_cfg.redirect, &stdout_redirect);
    if (opt.stderr_cfg.disposition == MEL_PROCESS_STDIO_REDIRECT && !mel_process__pipe_fd(opt.stderr_cfg.redirect, &stderr_redirect))
        (void)mel_stream_native_fd(opt.stderr_cfg.redirect, &stderr_redirect);

    if ((opt.stdin_cfg.disposition == MEL_PROCESS_STDIO_REDIRECT && stdin_redirect < 0) || (opt.stdout_cfg.disposition == MEL_PROCESS_STDIO_REDIRECT && stdout_redirect < 0) || (opt.stderr_cfg.disposition == MEL_PROCESS_STDIO_REDIRECT && stderr_redirect < 0))
    {
        mel_log_error("process", "spawn: redirect target stream has no native fd");
        mel_dealloc(alloc, p);
        out.status = MEL_PROCESS_ERROR | MEL_PROCESS_SPAWN_FAILED;
        return out;
    }

    Mel_Process_Spawn_Args args = {
        .argv = opt.argv,
        .argc = opt.argc,
        .env = opt.env,
        .env_count = opt.env_count,
        .env_clear = opt.env_clear,
        .cwd = opt.cwd,
        .stdin_disposition = opt.stdin_cfg.disposition,
        .stdout_disposition = opt.stdout_cfg.disposition,
        .stderr_disposition = opt.stderr_cfg.disposition,
        .stdin_redirect_fd = stdin_redirect,
        .stdout_redirect_fd = stdout_redirect,
        .stderr_redirect_fd = stderr_redirect,
        .merge_stderr = opt.merge_stderr,
        .detached = opt.detached,
        .alloc = alloc,
    };

    p->native = mel_process__backend_spawn(args);
    if (mel_process_status_failed(p->native.status))
    {
        out.status = p->native.status;
        out.os_error = p->native.os_error;
        mel_dealloc(alloc, p);
        return out;
    }

    if (opt.stdin_cfg.disposition == MEL_PROCESS_STDIO_PIPE)
        p->stdin_stream = mel_process__pipe_stream(p->native.child_stdin.fd, false, true, opt.reactor, alloc);
    if (opt.stdout_cfg.disposition == MEL_PROCESS_STDIO_PIPE)
        p->stdout_stream = mel_process__pipe_stream(p->native.child_stdout.fd, true, false, opt.reactor, alloc);
    if (opt.stderr_cfg.disposition == MEL_PROCESS_STDIO_PIPE && !opt.merge_stderr)
        p->stderr_stream = mel_process__pipe_stream(p->native.child_stderr.fd, true, false, opt.reactor, alloc);

    if (p->stdin_stream)
    {
        p->native.child_stdin.fd = -1;
        p->native.child_stdin.handle = NULL;
    }
    if (p->stdout_stream)
    {
        p->native.child_stdout.fd = -1;
        p->native.child_stdout.handle = NULL;
    }
    if (p->stderr_stream)
    {
        p->native.child_stderr.fd = -1;
        p->native.child_stderr.handle = NULL;
    }

    out.value = p;
    out.status = MEL_PROCESS_OK | (opt.detached ? MEL_PROCESS_DETACHED : 0u);
    return out;
}

i64 mel_process_pid(const Mel_Process* p) { return p ? p->native.pid : -1; }

bool mel_process_detached(const Mel_Process* p) { return p ? p->detached : false; }

bool mel_process_running(Mel_Process* p)
{
    if (!p)
        return false;
    if (p->reaped)
        return false;
    if (p->detached)
        return false;
    return !process_try_reap(p);
}

Mel_Stream* mel_process_stdin(Mel_Process* p) { return p ? p->stdin_stream : NULL; }
Mel_Stream* mel_process_stdout(Mel_Process* p) { return p ? p->stdout_stream : NULL; }
Mel_Stream* mel_process_stderr(Mel_Process* p) { return p ? p->stderr_stream : NULL; }

void mel_process_close_stdin(Mel_Process* p)
{
    if (!p)
        return;
    close_stream(&p->stdin_stream);
}

bool mel_process_poll(Mel_Process* p, Mel_Process_Exit* out)
{
    if (!p)
        return false;
    if (!p->reaped && !process_try_reap(p))
        return false;
    if (out)
        *out = p->exit;
    return true;
}

Mel_Future* mel_process_wait_opt(Mel_Process* p, Mel_Process_Wait_Opt opt)
{
    if (!p)
        return NULL;

    if (p->detached)
    {
        Wait_Op* op = wait_op_new(p->alloc);
        if (!op)
            return NULL;
        Mel_Process_Exit ex = { .status = MEL_PROCESS_ERROR | MEL_PROCESS_DETACHED };
        return wait_op_resolve(op, ex);
    }

    if (p->reaped)
    {
        Wait_Op* op = wait_op_new(p->alloc);
        if (!op)
            return NULL;
        if (opt.out_op)
            *opt.out_op = MEL_PROCESS_OP_NULL;
        return wait_op_resolve(op, p->exit);
    }

    if (process_try_reap(p))
    {
        Wait_Op* op = wait_op_new(p->alloc);
        if (!op)
            return NULL;
        if (opt.out_op)
            *opt.out_op = MEL_PROCESS_OP_NULL;
        return wait_op_resolve(op, p->exit);
    }

    if (!p->reactor)
    {
        mel_log_error("process", "async wait requires a reactor; use mel_process_wait_sync for a reactorless process");
        Wait_Op* op = wait_op_new(p->alloc);
        if (!op)
            return NULL;
        Mel_Process_Exit ex = { .status = MEL_PROCESS_ERROR | MEL_PROCESS_UNAVAILABLE };
        return wait_op_resolve(op, ex);
    }

    assert(mel_reactor_is_owner(p->reactor));

    if (p->pending_wait)
    {
        mel_log_error("process", "wait already pending; only one async wait per process");
        Wait_Op* op = wait_op_new(p->alloc);
        if (!op)
            return NULL;
        Mel_Process_Exit ex = { .status = MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE };
        return wait_op_resolve(op, ex);
    }

    Wait_Op* op = wait_op_new(p->alloc);
    if (!op)
        return NULL;

    p->pending_wait = op;
    p->wait_generation++;
    if (p->wait_generation == 0)
        p->wait_generation = 1;
    if (opt.out_op)
        *opt.out_op = (Mel_Process_Op){ .index = 1, .generation = p->wait_generation };

    if (!p->reap_source)
    {
        p->reap_source = mel_reactor_timer_new(MEL_PROCESS_REAP_INTERVAL_NS, reap_tick, p);
        mel_reactor_source_attach(p->reactor, p->reap_source);
    }

    (void)opt.deliver;
    return &op->future;
}

bool mel_process_cancel_wait(Mel_Process* p, Mel_Process_Op op)
{
    if (!p || !p->pending_wait)
        return false;
    if (op.index != 1 || op.generation != p->wait_generation)
        return false;
    assert(mel_reactor_is_owner(p->reactor));

    Wait_Op* w = p->pending_wait;
    p->pending_wait = NULL;
    if (p->reap_source)
    {
        Mel_Reactor_Source* src = p->reap_source;
        p->reap_source = NULL;
        mel_reactor_source_destroy(src);
    }
    Mel_Process_Exit ex = { .status = MEL_PROCESS_CANCELLED };
    wait_op_resolve(w, ex);
    return true;
}

Mel_Process_Exit mel_process_wait_sync(Mel_Process* p)
{
    Mel_Process_Exit ex = { 0 };
    if (!p)
    {
        ex.status = MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE;
        return ex;
    }
    if (p->detached)
    {
        ex.status = MEL_PROCESS_ERROR | MEL_PROCESS_DETACHED;
        return ex;
    }
    if (p->reaped)
        return p->exit;

    i32                exit_code = 0;
    i32                term_signal = 0;
    Mel_Process_Status st = MEL_PROCESS_OK;
    mel_process__backend_wait_blocking(&p->native, &exit_code, &term_signal, &st);
    p->reaped = true;
    p->exit.exit_code = exit_code;
    p->exit.term_signal = term_signal;
    p->exit.status = st;

    if (p->pending_wait)
    {
        if (p->reap_source)
        {
            Mel_Reactor_Source* src = p->reap_source;
            p->reap_source = NULL;
            mel_reactor_source_destroy(src);
        }
        process_resolve_pending(p);
    }
    return p->exit;
}

Mel_Process_Status mel_process_kill_opt(Mel_Process* p, Mel_Process_Kill_Opt opt)
{
    if (!p)
        return MEL_PROCESS_ERROR | MEL_PROCESS_BAD_HANDLE;
    if (p->reaped)
        return MEL_PROCESS_OK | MEL_PROCESS_EXITED;
    return mel_process__backend_signal(&p->native, opt.signal);
}

const Mel_Process_Exit* mel_process_wait_future_result(Mel_Future* f)
{
    if (!f)
        return NULL;
    Wait_Op* op = mel_container_of(f, Wait_Op, future);
    return &op->result;
}

void mel_process_wait_future_release(Mel_Future* f)
{
    if (!f)
        return;
    Wait_Op* op = mel_container_of(f, Wait_Op, future);
    if (!op->owned)
    {
        Mel_Process* p = mel_container_of(op, Mel_Process, scratch);
        p->scratch_busy = false;
        return;
    }
    mel_dealloc(op->alloc, op);
}

void mel_process_destroy(Mel_Process* p)
{
    if (!p)
        return;

    if (p->reap_source)
    {
        Mel_Reactor_Source* src = p->reap_source;
        p->reap_source = NULL;
        mel_reactor_source_destroy(src);
    }
    if (p->pending_wait)
    {
        Wait_Op*         op = p->pending_wait;
        p->pending_wait = NULL;
        Mel_Process_Exit ex = { .status = MEL_PROCESS_CANCELLED };
        wait_op_resolve(op, ex);
    }

    close_stream(&p->stdin_stream);
    close_stream(&p->stdout_stream);
    close_stream(&p->stderr_stream);

    if (!p->detached && !p->reaped)
    {
        mel_process__backend_signal(&p->native, MEL_PROCESS_SIGNAL_KILL);
        i32                ec = 0, sg = 0;
        Mel_Process_Status st = MEL_PROCESS_OK;
        mel_process__backend_wait_blocking(&p->native, &ec, &sg, &st);
        p->reaped = true;
    }

    mel_process__backend_close(&p->native);
    mel_dealloc(p->alloc, p);
}
