#pragma once

#include <core/types.h>
#include <future/future.h>

#include <net/net.h>
#include <net/address.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Net_Udp Mel_Net_Udp;

typedef struct
{
    usize           bytes;
    Mel_Net_Address from;
    Mel_Net_Status  status;
    i32             os_error;
} Mel_Net_Udp_Result;

typedef struct
{
    Mel_Net_Address address;
    bool            bind;
    bool            reuse_addr;
} Mel_Net_Udp_Opt;

typedef struct
{
    Mel_Net_Udp*   value;
    Mel_Net_Status status;
    i32            os_error;
} Mel_Net_Udp_Open_Result;

Mel_Net_Udp_Open_Result mel_net_udp_open_opt(Mel_Net* net, Mel_Net_Udp_Opt opt);
#define mel_net_udp_open(net, ...) mel_net_udp_open_opt((net), (Mel_Net_Udp_Opt){ __VA_ARGS__ })

typedef struct
{
    Mel_Net_Address address;
    const void*     buffer;
    usize           len;
    Mel_Executor*   deliver;
    Mel_Net_Op*     out_op;
} Mel_Net_Udp_Send_Opt;

typedef struct
{
    void*         buffer;
    usize         len;
    Mel_Executor* deliver;
    Mel_Net_Op*   out_op;
} Mel_Net_Udp_Recv_Opt;

Mel_Future* mel_net_udp_send_opt(Mel_Net_Udp* udp, Mel_Net_Udp_Send_Opt opt);
#define mel_net_udp_send(udp, ...) mel_net_udp_send_opt((udp), (Mel_Net_Udp_Send_Opt){ __VA_ARGS__ })

Mel_Future* mel_net_udp_recv_opt(Mel_Net_Udp* udp, Mel_Net_Udp_Recv_Opt opt);
#define mel_net_udp_recv(udp, ...) mel_net_udp_recv_opt((udp), (Mel_Net_Udp_Recv_Opt){ __VA_ARGS__ })

const Mel_Net_Udp_Result* mel_net_future_udp(Mel_Future* f);

Mel_Net_Address mel_net_udp_address(const Mel_Net_Udp* udp);
void            mel_net_udp_destroy(Mel_Net_Udp* udp);

#ifdef __cplusplus
}
#endif
