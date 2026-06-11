#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <future/future.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Vat      Mel_Vat;
typedef struct Mel_Executor Mel_Executor;

typedef struct Mel_Net Mel_Net;

typedef u32 Mel_Net_Status;

#define MEL_NET_SEVERITY_MASK  0x3u
#define MEL_NET_OK             0u
#define MEL_NET_WARNED         1u
#define MEL_NET_ERROR          2u

#define MEL_NET_CANCELLED      (1u << 2)
#define MEL_NET_TIMED_OUT      (1u << 3)
#define MEL_NET_REFUSED        (1u << 4)
#define MEL_NET_UNREACHABLE    (1u << 5)
#define MEL_NET_RESET          (1u << 6)
#define MEL_NET_IN_USE         (1u << 7)
#define MEL_NET_BAD_ADDRESS    (1u << 8)
#define MEL_NET_RESOLVE_FAILED (1u << 9)
#define MEL_NET_TRUNCATED      (1u << 10)
#define MEL_NET_UNAVAILABLE    (1u << 11)
#define MEL_NET_CLOSED         (1u << 12)
#define MEL_NET_BUSY           (1u << 13)

static inline bool mel_net_status_ok(Mel_Net_Status s) { return (s & MEL_NET_SEVERITY_MASK) == MEL_NET_OK; }
static inline bool mel_net_status_failed(Mel_Net_Status s) { return (s & MEL_NET_SEVERITY_MASK) == MEL_NET_ERROR; }
static inline bool mel_net_status_warned(Mel_Net_Status s) { return (s & MEL_NET_SEVERITY_MASK) == MEL_NET_WARNED; }
static inline bool mel_net_status_cancelled(Mel_Net_Status s) { return (s & MEL_NET_CANCELLED) != 0u; }
static inline bool mel_net_status_timed_out(Mel_Net_Status s) { return (s & MEL_NET_TIMED_OUT) != 0u; }

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Net_Op;

#define MEL_NET_OP_NULL ((Mel_Net_Op){ 0, 0 })

static inline bool mel_net_op_valid(Mel_Net_Op op) { return op.index != 0 || op.generation != 0; }

typedef struct
{
    Mel_Vat*         vat;
    const Mel_Alloc* alloc;
    u32              resolver_workers;
} Mel_Net_Opt;

Mel_Net* mel_net_create_opt(Mel_Net_Opt opt);
#define mel_net_create(...) mel_net_create_opt((Mel_Net_Opt){ __VA_ARGS__ })

void mel_net_destroy(Mel_Net* net);

bool          mel_net_available(const Mel_Net* net);
Mel_Vat*      mel_net_vat(const Mel_Net* net);
Mel_Executor* mel_net_executor(const Mel_Net* net);
u32           mel_net_pending(const Mel_Net* net);

bool mel_net_cancel(Mel_Net* net, Mel_Net_Op op);

void mel_net_future_release(Mel_Future* f);

#ifdef __cplusplus
}
#endif
