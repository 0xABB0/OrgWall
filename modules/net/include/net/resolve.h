#pragma once

#include <core/types.h>
#include <string/str8.fwd.h>
#include <future/future.h>

#include <net/net.h>
#include <net/address.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_Net_Address* items;
    usize            count;
    Mel_Net_Status   status;
    i32              os_error;
} Mel_Net_Resolve_Result;

typedef struct
{
    u16           port;
    bool          v4_only;
    bool          v6_only;
    Mel_Executor* deliver;
    Mel_Net_Op*   out_op;
} Mel_Net_Resolve_Opt;

Mel_Future* mel_net_resolve_opt(Mel_Net* net, str8 host, Mel_Net_Resolve_Opt opt);
#define mel_net_resolve(net, host, ...) mel_net_resolve_opt((net), (host), (Mel_Net_Resolve_Opt){ __VA_ARGS__ })

const Mel_Net_Resolve_Result* mel_net_future_resolve(Mel_Future* f);
Mel_Net_Address*              mel_net_future_take_addresses(Mel_Future* f, usize* out_count);

#ifdef __cplusplus
}
#endif
