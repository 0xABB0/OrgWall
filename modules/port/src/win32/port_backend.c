#include "../port_internal.h"

#include <allocator/allocator.h>
#include <reactor/reactor.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <io.h>
#include <string.h>

typedef struct
{
    OVERLAPPED ov;
    HANDLE     handle;
    HANDLE     event;
    bool       is_read;
} Port_Win32_Native;

static const Mel_Alloc* port_win32_alloc(Mel_Port_Op_Record* op) { return op->alloc; }

static Mel_Port_Op_Record* port_win32_op_of(Mel_Reactor_Source* s) { return (Mel_Port_Op_Record*)s->user; }

static Mel_Port_Status port_win32_map_error(DWORD err)
{
    switch (err)
    {
    case ERROR_HANDLE_EOF:
    case ERROR_BROKEN_PIPE:
        return MEL_PORT_OK | MEL_PORT_EOF;
    case ERROR_NETNAME_DELETED:
    case ERROR_PIPE_NOT_CONNECTED:
        return MEL_PORT_ERROR | MEL_PORT_PEER_CLOSE;
    case ERROR_INVALID_HANDLE:
        return MEL_PORT_ERROR | MEL_PORT_BAD_FD;
    case ERROR_OPERATION_ABORTED:
        return MEL_PORT_ERROR | MEL_PORT_CANCELLED;
    default:
        return MEL_PORT_ERROR;
    }
}

static void port_win32_settle_bytes(Mel_Port_Op_Record* op, DWORD bytes)
{
    Port_Win32_Native* nat = (Port_Win32_Native*)op->backend.native;
    op->done = (usize)bytes;
    Mel_Port_Status st = MEL_PORT_OK;
    if (nat->is_read)
    {
        if (bytes == 0)
            st = MEL_PORT_OK | MEL_PORT_EOF;
        else if (op->done < op->len)
            st |= MEL_PORT_PARTIAL;
    }
    mel_port__op_settle(op, op->done, 0, st);
}

static void port_win32_harvest(Mel_Port_Op_Record* op)
{
    Port_Win32_Native* nat = (Port_Win32_Native*)op->backend.native;
    DWORD              bytes = 0;
    if (GetOverlappedResult(nat->handle, &nat->ov, &bytes, FALSE))
    {
        port_win32_settle_bytes(op, bytes);
        return;
    }
    DWORD err = GetLastError();
    if (err == ERROR_IO_INCOMPLETE)
        return;
    mel_port__op_settle(op, (usize)bytes, (i32)err, port_win32_map_error(err));
}

static bool port_win32_check(Mel_Reactor_Source* s)
{
    Mel_Port_Op_Record* op = port_win32_op_of(s);
    return (op->backend.poll.revents & MEL_REACTOR_POLL_IN) != 0;
}

static bool port_win32_dispatch(Mel_Reactor_Source* s, Mel_Reactor_Source_Proc callback, void* user)
{
    (void)callback;
    (void)user;
    port_win32_harvest(port_win32_op_of(s));
    return true;
}

static void port_win32_finalize(Mel_Reactor_Source* s) { (void)s; }

static const Mel_Reactor_Source_Callbacks PORT_WIN32_VT = {
    .check = port_win32_check,
    .dispatch = port_win32_dispatch,
    .finalize = port_win32_finalize,
};

bool mel_port__backend_available(void) { return true; }

bool mel_port__backend_port_init(Mel_Port* port)
{
    (void)port;
    return true;
}

void mel_port__backend_port_teardown(Mel_Port* port) { (void)port; }

static void port_win32_native_free(Mel_Port_Op_Record* op)
{
    Port_Win32_Native* nat = (Port_Win32_Native*)op->backend.native;
    if (!nat)
        return;
    if (nat->event)
        CloseHandle(nat->event);
    mel_dealloc(port_win32_alloc(op), nat);
    op->backend.native = NULL;
}

void mel_port__backend_submit(Mel_Port_Op_Record* op)
{
    HANDLE h = (HANDLE)_get_osfhandle(op->fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        mel_port__op_settle(op, 0, (i32)ERROR_INVALID_HANDLE, MEL_PORT_ERROR | MEL_PORT_BAD_FD);
        return;
    }

    Port_Win32_Native* nat = mel_alloc_type(port_win32_alloc(op), Port_Win32_Native);
    if (!nat)
    {
        mel_port__op_settle(op, 0, 0, MEL_PORT_ERROR);
        return;
    }
    memset(nat, 0, sizeof *nat);
    nat->handle = h;
    nat->is_read = (op->backend.poll.events & MEL_REACTOR_POLL_IN) != 0;
    nat->event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!nat->event)
    {
        DWORD err = GetLastError();
        mel_dealloc(port_win32_alloc(op), nat);
        mel_port__op_settle(op, 0, (i32)err, MEL_PORT_ERROR);
        return;
    }
    op->backend.native = nat;

    if (op->offset >= 0)
    {
        nat->ov.Offset = (DWORD)(u64)op->offset;
        nat->ov.OffsetHigh = (DWORD)((u64)op->offset >> 32);
    }
    nat->ov.hEvent = nat->event;

    DWORD want = op->len > 0xffffffffu ? 0xffffffffu : (DWORD)op->len;
    DWORD moved = 0;
    BOOL  ok = nat->is_read ? ReadFile(h, op->buffer, want, &moved, &nat->ov) : WriteFile(h, op->buffer, want, &moved, &nat->ov);

    if (ok)
    {
        port_win32_settle_bytes(op, moved);
        return;
    }

    DWORD err = GetLastError();
    if (err != ERROR_IO_PENDING)
    {
        if (err == ERROR_HANDLE_EOF)
        {
            port_win32_native_free(op);
            mel_port__op_settle(op, 0, 0, MEL_PORT_OK | MEL_PORT_EOF);
            return;
        }
        port_win32_native_free(op);
        mel_port__op_settle(op, 0, (i32)err, port_win32_map_error(err));
        return;
    }

    op->backend.poll.handle = (i64)(intptr_t)nat->event;
    op->backend.poll.events = MEL_REACTOR_POLL_IN;

    Mel_Reactor_Source* s = mel_reactor_source_new(&PORT_WIN32_VT, sizeof(Mel_Reactor_Source));
    op->backend.source = s;
    mel_reactor_source_set_callback(s, NULL, op);
    mel_reactor_source_add_poll(s, &op->backend.poll);
    mel_reactor_source_attach(op->port->reactor, s);
    op->backend.attached = true;
}

void mel_port__backend_retract(Mel_Port_Op_Record* op)
{
    Port_Win32_Native* nat = (Port_Win32_Native*)op->backend.native;
    if (nat && op->backend.attached)
    {
        if (CancelIoEx(nat->handle, &nat->ov) || GetLastError() != ERROR_NOT_FOUND)
        {
            DWORD bytes = 0;
            GetOverlappedResult(nat->handle, &nat->ov, &bytes, TRUE);
        }
    }

    if (op->backend.attached)
    {
        op->backend.attached = false;
        mel_reactor_source_destroy(op->backend.source);
    }
    port_win32_native_free(op);
}
