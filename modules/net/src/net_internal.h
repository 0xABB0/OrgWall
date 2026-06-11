#pragma once

#include <net/net.h>
#include <net/address.h>
#include <net/resolve.h>
#include <net/tcp.h>
#include <net/udp.h>

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <collection/slotmap.h>
#include <collection/mpsc.h>
#include <executor/executor.h>
#include <future/future.h>
#include <string/str8.h>
#include <thread/thread.h>
#include <thread/sem.h>
#include <vat/vat.h>

#include <stdatomic.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Net_Op_Record Mel_Net_Op_Record;

struct Mel_Net_Op_Record
{
    Mel_Net*           net;
    const Mel_Alloc*   alloc;
    Mel_SlotMap_Handle self;
    Mel_Executor*      deliver;

    Mel_Future      future;
    Mel_Net_Status* status_slot;
    i32*            oserr_slot;

    bool settled;
    bool detached;

    Mel_Vat_Source*  source;
    Mel_Vat_Wakeable wakeable;
    bool             armed;
    i64              deadline;

    bool (*on_event)(Mel_Net_Op_Record* op, bool timed_out);
    void (*on_settle)(Mel_Net_Op_Record* op);
    void (*free_payload)(Mel_Net_Op_Record* op);

    i32  fd;
    bool owns_fd;

    Mel_Net_Listener* listener;
    Mel_Net_Udp*      udp;
    void*             buffer;
    const void*       cbuffer;
    usize             len;
    Mel_Net_Address   addr;

    Mel_Mpsc_Node queue_node;
    Mel_Task      completion_task;
    bool          worker_op;
    bool          submitted;
    bool          cancel_requested;
    str8          host;
    u16           rport;
    bool          v4_only;
    bool          v6_only;

    union
    {
        Mel_Net_Conn_Result    conn;
        Mel_Net_Udp_Result     udp;
        Mel_Net_Resolve_Result resolve;
    } result;
};

struct Mel_Net
{
    Mel_Vat*         vat;
    Mel_Executor*    executor;
    const Mel_Alloc* alloc;

    Mel_SlotMap ops;
    bool        backend_ready;

    u32          worker_count;
    Mel_Thread*  workers;
    Mel_Mpsc     queue;
    Mel_Sem      queue_items;
    _Atomic(i32) running;
    i32          pending_posts;
    bool         destroying;
    bool         zero_timeout_logged;
};

struct Mel_Net_Listener
{
    Mel_Net*           net;
    const Mel_Alloc*   alloc;
    i32                fd;
    Mel_Net_Address    bound;
    Mel_Net_Op_Record* pending_accept;
};

struct Mel_Net_Udp
{
    Mel_Net*           net;
    const Mel_Alloc*   alloc;
    i32                fd;
    Mel_Net_Address    bound;
    Mel_Net_Op_Record* pending_send;
    Mel_Net_Op_Record* pending_recv;
};

struct Mel_Net_Conn
{
    Mel_Net*         net;
    const Mel_Alloc* alloc;
    i32              fd;
    Mel_Stream*      stream;
    Mel_Net_Address  local;
    Mel_Net_Address  peer;
};

Mel_Net_Op_Record* mel_net__op_begin(Mel_Net* net, Mel_Executor* deliver, Mel_Net_Op* out_op);
void               mel_net__op_settle(Mel_Net_Op_Record* op, Mel_Net_Status status, i32 os_error);
void               mel_net__op_arm(Mel_Net_Op_Record* op, i32 fd, u32 events, i64 deadline);
Mel_Future*        mel_net__op_fail(Mel_Net* net, Mel_Net_Op_Record* op, Mel_Net_Status status, i32 os_error);
Mel_Future*        mel_net__resolve_submit(Mel_Net_Op_Record* op);
void               mel_net__op_free_record(Mel_Net_Op_Record* op);

Mel_Net_Conn* mel_net__conn_create(Mel_Net* net, i32 fd, const Mel_Alloc* alloc);

#ifdef __cplusplus
}
#endif
