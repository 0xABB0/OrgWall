#include <process/process.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <future/future.h>
#include <executor/executor.h>
#include <vat/vat.h>
#include <io/stream.h>
#include <collection/list.h>
#include <log/log.h>

#include <assert.h>
#include <string.h>

#define MEL_PROCESS_RUN_CHUNK 4096

typedef struct
{
    Mel_Future         future;
    Mel_Process_Output result;
    const Mel_Alloc*   alloc;
} Run_Op;

typedef struct
{
    u8*              data;
    usize            len;
    usize            cap;
    const Mel_Alloc* alloc;
    u8               chunk[MEL_PROCESS_RUN_CHUNK];
} Drain_Buf;

typedef struct Run_Ctx Run_Ctx;

typedef struct
{
    Mel_Task    task;
    Run_Ctx*    ctx;
    Drain_Buf   buf;
    Mel_Stream* stream;
    Mel_Future* pending;
    bool        is_stdout;
} Drain;

struct Run_Ctx
{
    Mel_Process*     proc;
    Run_Op*          op;
    Mel_Vat*         vat;
    Mel_Executor*    deliver;
    const Mel_Alloc* alloc;

    Drain stdout_drain;
    Drain stderr_drain;

    Mel_Task    stdin_task;
    Mel_Future* stdin_pending;
    bool        has_stdin;
    bool        has_stderr;

    Mel_Task    wait_task;
    Mel_Future* wait_pending;

    bool stdout_done;
    bool stderr_done;
    bool stdin_done;
    bool wait_done;

    Mel_Process_Exit exit;
    bool             finalized;
};

static Mel_Future_Status run_future_status_from(Mel_Process_Status status)
{
    Mel_Future_Status fs = status & MEL_FUTURE_SEVERITY_MASK;
    if (status & MEL_PROCESS_CANCELLED)
        fs |= MEL_FUTURE_CANCELLED;
    return fs;
}

static bool drain_buf_reserve(Drain_Buf* b, usize extra)
{
    if (b->len + extra <= b->cap)
        return true;
    usize ncap = b->cap ? b->cap : MEL_PROCESS_RUN_CHUNK;
    while (ncap < b->len + extra)
        ncap *= 2;
    u8* nd = b->data ? (u8*)mel_realloc(b->alloc, b->data, ncap) : (u8*)mel_alloc(b->alloc, ncap);
    if (!nd)
        return false;
    b->data = nd;
    b->cap = ncap;
    return true;
}

static void run_finalize(Run_Ctx* c)
{
    if (c->finalized)
        return;
    if (!(c->stdout_done && (c->stderr_done || !c->has_stderr) && (c->stdin_done || !c->has_stdin) && c->wait_done))
        return;
    c->finalized = true;

    Run_Op* op = c->op;
    op->result.stdout_data = c->stdout_drain.buf.data;
    op->result.stdout_len = c->stdout_drain.buf.len;
    op->result.stderr_data = c->stderr_drain.buf.data;
    op->result.stderr_len = c->stderr_drain.buf.len;
    op->result.exit_code = c->exit.exit_code;
    op->result.term_signal = c->exit.term_signal;
    op->result.status = c->exit.status;
    op->result.alloc = c->alloc;

    mel_process_destroy(c->proc);

    Mel_Future* f = &op->future;
    if (c->exit.status & MEL_PROCESS_CANCELLED)
        mel_future_cancel(f);
    else
        mel_future_resolve(f, &op->result, run_future_status_from(c->exit.status));

    Mel_Vat*         vat = c->vat;
    const Mel_Alloc* alloc = c->alloc;
    mel_dealloc(alloc, c);
    mel_vat_release(vat);
}

static void drain_kick(Drain* d);

static void drain_on_read(Mel_Task* self)
{
    Drain*               d = mel_container_of(self, Drain, task);
    const Mel_IO_Result* r = mel_stream_future_result(d->pending);
    usize                n = r ? r->bytes_transferred : 0;
    Mel_IO_Status        st = r ? r->status : (MEL_IO_ERROR | MEL_IO_UNAVAILABLE);
    mel_stream_future_release(d->pending);
    d->pending = NULL;

    if (n > 0)
    {
        if (drain_buf_reserve(&d->buf, n))
        {
            memcpy(d->buf.data + d->buf.len, d->buf.chunk, n);
            d->buf.len += n;
        }
    }

    bool eof = mel_io_status_eof(st) || mel_io_status_failed(st) || (n == 0);
    if (eof)
    {
        if (d->is_stdout)
            d->ctx->stdout_done = true;
        else
            d->ctx->stderr_done = true;
        run_finalize(d->ctx);
        return;
    }
    drain_kick(d);
}

static void drain_kick(Drain* d)
{
    d->pending = mel_stream_read(d->stream, .buffer = d->buf.chunk, .len = sizeof d->buf.chunk, .deliver = mel_vat_executor(d->ctx->vat));
    if (!d->pending)
    {
        if (d->is_stdout)
            d->ctx->stdout_done = true;
        else
            d->ctx->stderr_done = true;
        run_finalize(d->ctx);
        return;
    }
    mel_task_init(&d->task, drain_on_read);
    mel_future_then(d->pending, &d->task, mel_vat_executor(d->ctx->vat));
}

static void stdin_on_write(Mel_Task* self)
{
    Run_Ctx* c = mel_container_of(self, Run_Ctx, stdin_task);
    mel_stream_future_release(c->stdin_pending);
    c->stdin_pending = NULL;
    mel_process_close_stdin(c->proc);
    c->stdin_done = true;
    run_finalize(c);
}

static void wait_on_exit(Mel_Task* self)
{
    Run_Ctx*                c = mel_container_of(self, Run_Ctx, wait_task);
    const Mel_Process_Exit* ex = mel_process_wait_future_result(c->wait_pending);
    if (ex)
        c->exit = *ex;
    mel_process_wait_future_release(c->wait_pending);
    c->wait_pending = NULL;
    c->wait_done = true;
    run_finalize(c);
}

static Run_Op* run_op_new(const Mel_Alloc* alloc)
{
    Run_Op* op = mel_alloc_type(alloc, Run_Op);
    if (!op)
        return NULL;
    memset(op, 0, sizeof *op);
    op->alloc = alloc;
    mel_future_init(&op->future, NULL, alloc);
    op->future.value = &op->result;
    return op;
}

static Mel_Future* run_fail(const Mel_Alloc* alloc, Mel_Process_Status status, i32 exit_code)
{
    Run_Op* op = run_op_new(alloc);
    if (!op)
        return NULL;
    op->result.status = status;
    op->result.exit_code = exit_code;
    op->result.alloc = alloc;
    mel_future_resolve(&op->future, &op->result, run_future_status_from(status));
    return &op->future;
}

Mel_Future* mel_process_run_opt(Mel_Process_Run_Opt opt)
{
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    if (!opt.argv || opt.argc == 0 || !opt.argv[0])
    {
        mel_log_error("process", "run: argv with at least argv[0] is required");
        return run_fail(alloc, MEL_PROCESS_ERROR | MEL_PROCESS_SPAWN_FAILED, -1);
    }
    if (!opt.vat)
    {
        mel_log_error("process", "run: a vat is required to collect output asynchronously");
        return run_fail(alloc, MEL_PROCESS_ERROR | MEL_PROCESS_UNAVAILABLE, -1);
    }
    assert(mel_vat_is_owner(opt.vat));

    bool has_stdin = opt.stdin_data && opt.stdin_len > 0;

    Mel_Process_Spawn_Result sr = mel_process_spawn_opt((Mel_Process_Spawn_Opt){
        .argv = opt.argv,
        .argc = opt.argc,
        .env = opt.env,
        .env_count = opt.env_count,
        .env_clear = opt.env_clear,
        .cwd = opt.cwd,
        .stdin_cfg = { .disposition = has_stdin ? MEL_PROCESS_STDIO_PIPE : MEL_PROCESS_STDIO_NULL },
        .stdout_cfg = { .disposition = MEL_PROCESS_STDIO_PIPE },
        .stderr_cfg = { .disposition = opt.merge_stderr ? MEL_PROCESS_STDIO_INHERIT : MEL_PROCESS_STDIO_PIPE },
        .merge_stderr = opt.merge_stderr,
        .vat = opt.vat,
        .alloc = alloc,
    });

    if (mel_process_status_failed(sr.status))
        return run_fail(alloc, sr.status, -1);

    Run_Op* op = run_op_new(alloc);
    if (!op)
    {
        mel_process_destroy(sr.value);
        return NULL;
    }

    Run_Ctx* c = mel_alloc_type(alloc, Run_Ctx);
    if (!c)
    {
        mel_process_destroy(sr.value);
        mel_dealloc(alloc, op);
        return NULL;
    }
    memset(c, 0, sizeof *c);
    c->proc = sr.value;
    c->op = op;
    c->vat = opt.vat;
    c->deliver = opt.deliver ? opt.deliver : mel_vat_executor(opt.vat);
    c->alloc = alloc;
    mel_vat_retain(opt.vat);
    c->has_stdin = has_stdin;
    c->has_stderr = !opt.merge_stderr;
    c->stdout_drain.ctx = c;
    c->stdout_drain.buf.alloc = alloc;
    c->stdout_drain.stream = mel_process_stdout(c->proc);
    c->stdout_drain.is_stdout = true;

    c->stderr_drain.ctx = c;
    c->stderr_drain.buf.alloc = alloc;
    c->stderr_drain.stream = mel_process_stderr(c->proc);
    c->stderr_drain.is_stdout = false;

    Mel_Process_Op wop = MEL_PROCESS_OP_NULL;
    c->wait_pending = mel_process_wait(c->proc, .deliver = mel_vat_executor(opt.vat), .out_op = &wop);
    mel_task_init(&c->wait_task, wait_on_exit);
    mel_future_then(c->wait_pending, &c->wait_task, mel_vat_executor(opt.vat));

    if (has_stdin)
    {
        Mel_Stream* sin = mel_process_stdin(c->proc);
        c->stdin_pending = mel_stream_write(sin, .buffer = opt.stdin_data, .len = opt.stdin_len, .deliver = mel_vat_executor(opt.vat));
        mel_task_init(&c->stdin_task, stdin_on_write);
        mel_future_then(c->stdin_pending, &c->stdin_task, mel_vat_executor(opt.vat));
    }
    else
    {
        c->stdin_done = true;
    }

    drain_kick(&c->stdout_drain);
    if (c->has_stderr)
        drain_kick(&c->stderr_drain);
    else
        c->stderr_done = true;

    return &op->future;
}

const Mel_Process_Output* mel_process_run_future_result(Mel_Future* f)
{
    if (!f)
        return NULL;
    Run_Op* op = mel_container_of(f, Run_Op, future);
    return &op->result;
}

void mel_process_run_future_release(Mel_Future* f)
{
    if (!f)
        return;
    Run_Op* op = mel_container_of(f, Run_Op, future);
    if (op->result.stdout_data)
        mel_dealloc(op->result.alloc, op->result.stdout_data);
    if (op->result.stderr_data)
        mel_dealloc(op->result.alloc, op->result.stderr_data);
    mel_dealloc(op->alloc, op);
}
