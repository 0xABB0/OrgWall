#include "../port_internal.h"

#include <vat/vat.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

static void port_apple_nonblock(i32 fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    fcntl(fd, F_SETNOSIGPIPE, 1);
}

static ssize_t port_apple_write_nosigpipe(Mel_Port_Op_Record* op, const void* base, usize want)
{
    sigset_t block;
    sigemptyset(&block);
    sigaddset(&block, SIGPIPE);

    sigset_t prev;
    pthread_sigmask(SIG_BLOCK, &block, &prev);

    bool was_pending = false;
    if (!sigismember(&prev, SIGPIPE))
    {
        sigset_t pending;
        sigpending(&pending);
        was_pending = sigismember(&pending, SIGPIPE);
    }

    ssize_t n;
    if (op->offset >= 0)
        n = pwrite(op->fd, base, want, (off_t)(op->offset + (i64)op->done));
    else
        n = write(op->fd, base, want);
    i32 saved = errno;

    if (!was_pending)
    {
        sigset_t pending;
        sigpending(&pending);
        if (sigismember(&pending, SIGPIPE))
        {
            int sig = 0;
            while (sigwait(&block, &sig) == EINTR)
            {
            }
        }
    }

    pthread_sigmask(SIG_SETMASK, &prev, NULL);
    errno = saved;
    return n;
}

static Mel_Port_Op_Record* port_apple_op_of(Mel_Vat_Source* s) { return (Mel_Port_Op_Record*)mel_vat_source_state(s); }

static bool port_apple_read_step(Mel_Port_Op_Record* op)
{
    for (;;)
    {
        ssize_t n;
        if (op->offset >= 0)
            n = pread(op->fd, op->buffer, op->len, (off_t)op->offset);
        else
            n = read(op->fd, op->buffer, op->len);

        if (n > 0)
        {
            op->done = (usize)n;
            Mel_Port_Status st = MEL_PORT_OK;
            if (op->done < op->len)
                st |= MEL_PORT_PARTIAL;
            mel_port__op_settle(op, op->done, 0, st);
            return false;
        }
        if (n == 0)
        {
            mel_port__op_settle(op, 0, 0, MEL_PORT_OK | MEL_PORT_EOF);
            return false;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        {
            i32 e = errno;
            mel_port__op_settle(op, 0, e, MEL_PORT_ERROR | (e == EBADF ? MEL_PORT_BAD_FD : 0u));
            return false;
        }
    }
}

static bool port_apple_write_step(Mel_Port_Op_Record* op)
{
    for (;;)
    {
        const u8* base = (const u8*)op->buffer + op->done;
        usize     want = op->len - op->done;
        ssize_t   n = port_apple_write_nosigpipe(op, base, want);

        if (n >= 0)
        {
            op->done += (usize)n;
            if (op->done >= op->len)
            {
                mel_port__op_settle(op, op->done, 0, MEL_PORT_OK);
                return false;
            }
            continue;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        {
            i32             e = errno;
            Mel_Port_Status st = MEL_PORT_ERROR;
            if (e == EBADF)
                st |= MEL_PORT_BAD_FD;
            if (e == EPIPE || e == ECONNRESET)
                st |= MEL_PORT_PEER_CLOSE;
            mel_port__op_settle(op, op->done, e, st);
            return false;
        }
    }
}

static void port_apple_wakeables(Mel_Vat_Source* s, Mel_Vat_Wakeable** out, usize* count)
{
    Mel_Port_Op_Record* op = port_apple_op_of(s);
    *out = &op->backend.wakeable;
    *count = 1;
}

static bool port_apple_drain(Mel_Vat_Source* s, u32 budget)
{
    (void)budget;
    Mel_Port_Op_Record* op = port_apple_op_of(s);
    op->step(op);
    return false;
}

static const Mel_Vat_Source_Vtbl PORT_APPLE_VT = {
    .wakeables = port_apple_wakeables,
    .deadline = NULL,
    .drain = port_apple_drain,
    .cancel = NULL,
};

bool mel_port__backend_available(void) { return true; }

bool mel_port__backend_port_init(Mel_Port* port)
{
    (void)port;
    return true;
}

void mel_port__backend_port_teardown(Mel_Port* port) { (void)port; }

void mel_port__backend_submit(Mel_Port_Op_Record* op)
{
    op->step = (op->backend.wakeable.events & MEL_VAT_WAKE_IN) ? port_apple_read_step : port_apple_write_step;

    if (fcntl(op->fd, F_GETFL, 0) == -1)
    {
        mel_port__op_settle(op, 0, errno, MEL_PORT_ERROR | MEL_PORT_BAD_FD);
        return;
    }
    port_apple_nonblock(op->fd);

    op->backend.source = mel_vat_source_open(op->port->vat, &PORT_APPLE_VT, op);
    op->backend.attached = true;
}

void mel_port__backend_retract(Mel_Port_Op_Record* op)
{
    if (!op->backend.attached)
        return;
    op->backend.attached = false;
    mel_vat_source_close(op->backend.source);
}
