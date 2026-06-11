#include "net_internal.h"
#include "net_backend.h"

#include <allocator/allocator.h>
#include <collection/list.h>
#include <log/log.h>

#include <assert.h>
#include <string.h>

static void resolve_payload_free(Mel_Net_Op_Record* op)
{
    if (op->result.resolve.items)
    {
        mel_dealloc(op->alloc, op->result.resolve.items);
        op->result.resolve.items = NULL;
        op->result.resolve.count = 0;
    }
}

static void resolve_on_settle(Mel_Net_Op_Record* op)
{
    if (op->host.data)
    {
        mel_dealloc(op->alloc, op->host.data);
        op->host = STR8_EMPTY;
    }
}

Mel_Future* mel_net_resolve_opt(Mel_Net* net, str8 host, Mel_Net_Resolve_Opt opt)
{
    if (!net)
        return NULL;
    assert(mel_vat_is_owner(net->vat));

    Mel_Net_Op_Record* op = mel_net__op_begin(net, opt.deliver, opt.out_op);
    if (!op)
        return NULL;
    op->status_slot = &op->result.resolve.status;
    op->oserr_slot = &op->result.resolve.os_error;
    op->free_payload = resolve_payload_free;
    op->on_settle = resolve_on_settle;

    if (!net->backend_ready)
    {
        mel_log_error("net", "resolve: no socket backend available");
        return mel_net__op_fail(net, op, MEL_NET_ERROR | MEL_NET_UNAVAILABLE, 0);
    }
    if (host.len == 0)
        return mel_net__op_fail(net, op, MEL_NET_ERROR | MEL_NET_BAD_ADDRESS, 0);
    if (opt.v4_only && opt.v6_only)
        return mel_net__op_fail(net, op, MEL_NET_ERROR | MEL_NET_BAD_ADDRESS, 0);

    Mel_Net_Address literal;
    if (mel_net_status_ok(mel_net_address_parse(host, opt.port, &literal)))
    {
        if ((opt.v4_only && literal.v6) || (opt.v6_only && !literal.v6))
            return mel_net__op_fail(net, op, MEL_NET_ERROR | MEL_NET_RESOLVE_FAILED, 0);
        Mel_Net_Address* items = mel_alloc_array(op->alloc, Mel_Net_Address, 1);
        if (!items)
            return mel_net__op_fail(net, op, MEL_NET_ERROR, 0);
        items[0] = literal;
        op->result.resolve.items = items;
        op->result.resolve.count = 1;
        return mel_net__op_fail(net, op, MEL_NET_OK, 0);
    }

    op->host = str8_dup_alloc(host, op->alloc);
    if (host.len > 0 && !op->host.data)
        return mel_net__op_fail(net, op, MEL_NET_ERROR, 0);
    op->rport = opt.port;
    op->v4_only = opt.v4_only;
    op->v6_only = opt.v6_only;

    return mel_net__resolve_submit(op);
}

const Mel_Net_Resolve_Result* mel_net_future_resolve(Mel_Future* f)
{
    if (!f)
        return NULL;
    Mel_Net_Op_Record* op = mel_container_of(f, Mel_Net_Op_Record, future);
    return &op->result.resolve;
}

Mel_Net_Address* mel_net_future_take_addresses(Mel_Future* f, usize* out_count)
{
    if (!f)
        return NULL;
    Mel_Net_Op_Record* op = mel_container_of(f, Mel_Net_Op_Record, future);
    Mel_Net_Address*   items = op->result.resolve.items;
    if (out_count)
        *out_count = op->result.resolve.count;
    op->result.resolve.items = NULL;
    op->result.resolve.count = 0;
    return items;
}
