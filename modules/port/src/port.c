#include "port_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <collection.list/list.h>
#include <executor/executor.h>
#include <future/future.h>
#include <reactor/reactor.h>
#include <log/log.h>

#include <assert.h>
#include <string.h>

Mel_Port* mel_port_create_opt(Mel_Port_Opt opt)
{
    if (!opt.reactor)
    {
        mel_log_error("port", "create: reactor is required");
        return NULL;
    }

    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mel_Port*        port = mel_alloc_type(alloc, Mel_Port);
    if (!port)
        return NULL;
    memset(port, 0, sizeof *port);

    port->reactor = opt.reactor;
    port->executor = mel_reactor_executor(opt.reactor);
    port->alloc = alloc;

    mel_slotmap_init(&port->ops, alloc, .item_size = sizeof(Mel_Port_Op_Record*), .initial_capacity = 8);

    port->backend_ready = mel_port__backend_available() && mel_port__backend_port_init(port);
    if (!port->backend_ready)
        mel_log_warn("port", "no proactor backend on this platform; ops will fail unavailable");

    return port;
}

static Mel_Port_Op_Record* op_from_slot(Mel_Port* port, Mel_SlotMap_Handle h)
{
    Mel_Port_Op_Record** pp = (Mel_Port_Op_Record**)mel_slotmap_get(&port->ops, h);
    return pp ? *pp : NULL;
}

void mel_port__op_detach(Mel_Port_Op_Record* op)
{
    if (op->detached)
        return;
    op->detached = true;
    mel_slotmap_remove(&op->port->ops, op->self);
    if (op->submitted)
        mel_port__backend_retract(op);
}

void mel_port_destroy(Mel_Port* port)
{
    if (!port)
        return;
    assert(mel_reactor_is_owner(port->reactor));

    Mel_Array(Mel_Port_Op_Record*) snap;
    mel_array_init(&snap, port->alloc);
    Mel_Port_Op_Record** data = (Mel_Port_Op_Record**)mel_slotmap_data(&port->ops);
    u32                  n = mel_slotmap_count(&port->ops);
    for (u32 i = 0; i < n; i++)
        mel_array_push(&snap, data[i]);
    for (usize i = 0; i < snap.count; i++)
        mel_port__op_settle(snap.items[i], snap.items[i]->done, 0, MEL_PORT_ERROR | MEL_PORT_CANCELLED);
    mel_array_free(&snap);

    if (port->backend_ready)
        mel_port__backend_port_teardown(port);

    mel_slotmap_free(&port->ops);
    const Mel_Alloc* alloc = port->alloc;
    mel_dealloc(alloc, port);
}

bool          mel_port_available(const Mel_Port* port) { return port && port->backend_ready; }
Mel_Reactor*  mel_port_reactor(const Mel_Port* port) { return port ? port->reactor : NULL; }
Mel_Executor* mel_port_executor(const Mel_Port* port) { return port ? port->executor : NULL; }
u32           mel_port_pending(const Mel_Port* port) { return port ? mel_slotmap_count((Mel_SlotMap*)&port->ops) : 0; }

void mel_port__op_settle(Mel_Port_Op_Record* op, usize bytes, i32 os_error, Mel_Port_Status status)
{
    if (op->settled)
        return;
    op->settled = true;

    op->result.bytes_transferred = bytes;
    op->result.os_error = os_error;
    op->result.status = status;

    mel_port__op_detach(op);

    if (status & MEL_PORT_CANCELLED)
    {
        mel_future_cancel(&op->future);
    }
    else
    {
        Mel_Future_Status fs = status & MEL_FUTURE_SEVERITY_MASK;
        if (status & MEL_PORT_PARTIAL)
            fs |= MEL_FUTURE_PARTIAL;
        mel_future_resolve(&op->future, &op->result, fs);
    }
}

static Mel_Future* op_begin(Mel_Port* port, Mel_Port_Op_Record** out_rec, Mel_Executor* deliver, Mel_Port_Op* out_op)
{
    const Mel_Alloc* alloc = port->alloc;

    Mel_Port_Op_Record* op = mel_alloc_type(alloc, Mel_Port_Op_Record);
    if (!op)
        return NULL;
    memset(op, 0, sizeof *op);

    mel_future_init(&op->future, NULL, alloc);
    op->future.value = &op->result;

    op->port = port;
    op->alloc = alloc;
    op->deliver = deliver ? deliver : port->executor;

    Mel_Port_Op_Record* slot = op;
    op->self = mel_slotmap_insert(&port->ops, &slot);

    *out_rec = op;
    if (out_op)
        *out_op = (Mel_Port_Op){ .index = op->self.index, .generation = op->self.generation };
    return &op->future;
}

static Mel_Future* op_fail_unavailable(Mel_Port* port, Mel_Executor* deliver, Mel_Port_Op* out_op)
{
    Mel_Port_Op_Record* op = NULL;
    Mel_Future*         f = op_begin(port, &op, deliver, out_op);
    if (!f)
        return NULL;
    mel_log_error("port", "submit: no proactor backend available");
    mel_port__op_settle(op, 0, 0, MEL_PORT_ERROR | MEL_PORT_UNAVAILABLE);
    return f;
}

Mel_Future* mel_port_read_opt(Mel_Port* port, Mel_Port_Read_Opt opt)
{
    if (!port)
        return NULL;
    assert(mel_reactor_is_owner(port->reactor));
    if (!port->backend_ready)
        return op_fail_unavailable(port, opt.deliver, opt.out_op);

    Mel_Port_Op_Record* op = NULL;
    Mel_Future*         f = op_begin(port, &op, opt.deliver, opt.out_op);
    if (!f)
        return NULL;

    op->fd = opt.fd;
    op->offset = opt.offset;
    op->len = opt.len;
    op->buffer = opt.buffer;
    op->backend.poll.handle = opt.fd;
    op->backend.poll.events = MEL_REACTOR_POLL_IN;

    if (opt.len == 0)
    {
        mel_port__op_settle(op, 0, 0, MEL_PORT_OK);
        return f;
    }

    op->submitted = true;
    mel_port__backend_submit(op);
    return f;
}

Mel_Future* mel_port_write_opt(Mel_Port* port, Mel_Port_Write_Opt opt)
{
    if (!port)
        return NULL;
    assert(mel_reactor_is_owner(port->reactor));
    if (!port->backend_ready)
        return op_fail_unavailable(port, opt.deliver, opt.out_op);

    Mel_Port_Op_Record* op = NULL;
    Mel_Future*         f = op_begin(port, &op, opt.deliver, opt.out_op);
    if (!f)
        return NULL;

    op->fd = opt.fd;
    op->offset = opt.offset;
    op->len = opt.len;
    op->buffer = (void*)opt.buffer;
    op->backend.poll.handle = opt.fd;
    op->backend.poll.events = MEL_REACTOR_POLL_OUT;

    if (opt.len == 0)
    {
        mel_port__op_settle(op, 0, 0, MEL_PORT_OK);
        return f;
    }

    op->submitted = true;
    mel_port__backend_submit(op);
    return f;
}

bool mel_port_cancel(Mel_Port* port, Mel_Port_Op handle)
{
    if (!port)
        return false;
    assert(mel_reactor_is_owner(port->reactor));
    Mel_SlotMap_Handle  h = mel_slotmap_handle_make(handle.index, handle.generation);
    Mel_Port_Op_Record* op = op_from_slot(port, h);
    if (!op || op->settled)
        return false;
    mel_port__op_settle(op, op->done, 0, MEL_PORT_ERROR | MEL_PORT_CANCELLED);
    return true;
}

const Mel_Port_Result* mel_port_future_result(Mel_Future* f)
{
    if (!f)
        return NULL;
    Mel_Port_Op_Record* op = mel_container_of(f, Mel_Port_Op_Record, future);
    return &op->result;
}

void mel_port_future_release(Mel_Future* f)
{
    if (!f)
        return;
    Mel_Port_Op_Record* op = mel_container_of(f, Mel_Port_Op_Record, future);
    mel_dealloc(op->alloc, op);
}
