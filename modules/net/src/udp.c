#include "net_internal.h"
#include "net_backend.h"

#include <allocator/allocator.h>
#include <collection/list.h>
#include <log/log.h>

#include <assert.h>
#include <string.h>

Mel_Net_Udp_Open_Result mel_net_udp_open_opt(Mel_Net* net, Mel_Net_Udp_Opt opt)
{
    Mel_Net_Udp_Open_Result r = { 0 };
    if (!net)
    {
        r.status = MEL_NET_ERROR | MEL_NET_CLOSED;
        return r;
    }
    assert(mel_vat_is_owner(net->vat));
    if (!net->backend_ready)
    {
        mel_log_error("net", "udp open: no socket backend available");
        r.status = MEL_NET_ERROR | MEL_NET_UNAVAILABLE;
        return r;
    }

    i32 fd = -1;
    i32 err = mel_net__backend_udp_open(opt.bind ? &opt.address : NULL, opt.reuse_addr, &fd);
    if (err != 0)
    {
        r.status = mel_net__backend_status_from_os(err);
        r.os_error = err;
        return r;
    }

    Mel_Net_Udp* udp = mel_alloc_type(net->alloc, Mel_Net_Udp);
    if (!udp)
    {
        mel_net__backend_close(fd);
        r.status = MEL_NET_ERROR;
        return r;
    }
    memset(udp, 0, sizeof *udp);
    udp->net = net;
    udp->alloc = net->alloc;
    udp->fd = fd;
    mel_net__backend_local_address(fd, &udp->bound);

    r.value = udp;
    r.status = MEL_NET_OK;
    return r;
}

static void udp_send_on_settle(Mel_Net_Op_Record* op)
{
    if (op->udp && op->udp->pending_send == op)
        op->udp->pending_send = NULL;
}

static void udp_recv_on_settle(Mel_Net_Op_Record* op)
{
    if (op->udp && op->udp->pending_recv == op)
        op->udp->pending_recv = NULL;
}

static bool udp_send_on_event(Mel_Net_Op_Record* op, bool timed_out)
{
    (void)timed_out;
    usize sent = 0;
    i32   err = mel_net__backend_sendto(op->udp->fd, op->cbuffer, op->len, &op->addr, &sent);
    if (err == MEL_NET__WOULD_BLOCK)
        return true;
    if (err != 0)
    {
        mel_net__op_settle(op, mel_net__backend_status_from_os(err), err);
        return false;
    }
    op->result.udp.bytes = sent;
    mel_net__op_settle(op, MEL_NET_OK, 0);
    return false;
}

static bool udp_recv_on_event(Mel_Net_Op_Record* op, bool timed_out)
{
    (void)timed_out;
    usize received = 0;
    bool  truncated = false;
    i32   err = mel_net__backend_recvfrom(op->udp->fd, op->buffer, op->len, &op->result.udp.from, &truncated, &received);
    if (err == MEL_NET__WOULD_BLOCK)
        return true;
    if (err != 0)
    {
        mel_net__op_settle(op, mel_net__backend_status_from_os(err), err);
        return false;
    }
    op->result.udp.bytes = received;
    mel_net__op_settle(op, truncated ? (MEL_NET_WARNED | MEL_NET_TRUNCATED) : MEL_NET_OK, 0);
    return false;
}

Mel_Future* mel_net_udp_send_opt(Mel_Net_Udp* udp, Mel_Net_Udp_Send_Opt opt)
{
    if (!udp)
        return NULL;
    Mel_Net* net = udp->net;
    assert(mel_vat_is_owner(net->vat));

    Mel_Net_Op_Record* op = mel_net__op_begin(net, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->status_slot = &op->result.udp.status;
    op->oserr_slot = &op->result.udp.os_error;
    op->on_event = udp_send_on_event;
    op->on_settle = udp_send_on_settle;
    op->udp = udp;
    op->cbuffer = opt.buffer;
    op->len = opt.len;
    op->addr = opt.address;

    if (udp->fd < 0)
        return mel_net__op_fail(net, op, MEL_NET_ERROR | MEL_NET_CLOSED, 0);
    if (udp->pending_send)
    {
        mel_log_error("net", "udp send: a send is already pending on this socket");
        assert(!udp->pending_send);
        return mel_net__op_fail(net, op, MEL_NET_ERROR | MEL_NET_BUSY, 0);
    }

    udp->pending_send = op;
    if (!udp_send_on_event(op, false))
        return &op->future;
    mel_net__op_arm(op, udp->fd, MEL_VAT_WAKE_OUT, MEL_VAT_NEVER);
    return &op->future;
}

Mel_Future* mel_net_udp_recv_opt(Mel_Net_Udp* udp, Mel_Net_Udp_Recv_Opt opt)
{
    if (!udp)
        return NULL;
    Mel_Net* net = udp->net;
    assert(mel_vat_is_owner(net->vat));

    Mel_Net_Op_Record* op = mel_net__op_begin(net, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->status_slot = &op->result.udp.status;
    op->oserr_slot = &op->result.udp.os_error;
    op->on_event = udp_recv_on_event;
    op->on_settle = udp_recv_on_settle;
    op->udp = udp;
    op->buffer = opt.buffer;
    op->len = opt.len;

    if (udp->fd < 0)
        return mel_net__op_fail(net, op, MEL_NET_ERROR | MEL_NET_CLOSED, 0);
    if (udp->pending_recv)
    {
        mel_log_error("net", "udp recv: a recv is already pending on this socket");
        assert(!udp->pending_recv);
        return mel_net__op_fail(net, op, MEL_NET_ERROR | MEL_NET_BUSY, 0);
    }

    udp->pending_recv = op;
    if (!udp_recv_on_event(op, false))
        return &op->future;
    mel_net__op_arm(op, udp->fd, MEL_VAT_WAKE_IN, MEL_VAT_NEVER);
    return &op->future;
}

const Mel_Net_Udp_Result* mel_net_future_udp(Mel_Future* f)
{
    if (!f)
        return NULL;
    Mel_Net_Op_Record* op = mel_container_of(f, Mel_Net_Op_Record, future);
    return &op->result.udp;
}

Mel_Net_Address mel_net_udp_address(const Mel_Net_Udp* udp) { return udp->bound; }

void mel_net_udp_destroy(Mel_Net_Udp* udp)
{
    if (!udp)
        return;
    assert(mel_vat_is_owner(udp->net->vat));
    if (udp->pending_send)
        mel_net__op_settle(udp->pending_send, MEL_NET_ERROR | MEL_NET_CANCELLED, 0);
    if (udp->pending_recv)
        mel_net__op_settle(udp->pending_recv, MEL_NET_ERROR | MEL_NET_CANCELLED, 0);
    if (udp->fd >= 0)
    {
        mel_net__backend_close(udp->fd);
        udp->fd = -1;
    }
    mel_dealloc(udp->alloc, udp);
}
