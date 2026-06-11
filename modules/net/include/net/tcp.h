#pragma once

#include <core/types.h>
#include <future/future.h>

#include <net/net.h>
#include <net/address.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Stream Mel_Stream;

typedef struct Mel_Net_Conn     Mel_Net_Conn;
typedef struct Mel_Net_Listener Mel_Net_Listener;

typedef struct
{
    Mel_Net_Conn*  conn;
    Mel_Net_Status status;
    i32            os_error;
} Mel_Net_Conn_Result;

typedef struct
{
    Mel_Net_Address address;
    i64             timeout_ns;
    bool            nodelay;
    Mel_Executor*   deliver;
    Mel_Net_Op*     out_op;
} Mel_Net_Tcp_Connect_Opt;

Mel_Future* mel_net_tcp_connect_opt(Mel_Net* net, Mel_Net_Tcp_Connect_Opt opt);
#define mel_net_tcp_connect(net, ...) mel_net_tcp_connect_opt((net), (Mel_Net_Tcp_Connect_Opt){ __VA_ARGS__ })

const Mel_Net_Conn_Result* mel_net_future_conn(Mel_Future* f);
Mel_Net_Conn*              mel_net_future_take_conn(Mel_Future* f);

Mel_Stream*     mel_net_conn_stream(Mel_Net_Conn* conn);
Mel_Net_Address mel_net_conn_local_address(const Mel_Net_Conn* conn);
Mel_Net_Address mel_net_conn_peer_address(const Mel_Net_Conn* conn);
Mel_Net_Status  mel_net_conn_shutdown(Mel_Net_Conn* conn, bool read, bool write);
void            mel_net_conn_destroy(Mel_Net_Conn* conn);

typedef struct
{
    Mel_Net_Address address;
    u32             backlog;
    bool            reuse_addr;
    bool            v6_only;
} Mel_Net_Tcp_Listen_Opt;

typedef struct
{
    Mel_Net_Listener* value;
    Mel_Net_Status    status;
    i32               os_error;
} Mel_Net_Listener_Result;

Mel_Net_Listener_Result mel_net_tcp_listen_opt(Mel_Net* net, Mel_Net_Tcp_Listen_Opt opt);
#define mel_net_tcp_listen(net, ...) mel_net_tcp_listen_opt((net), (Mel_Net_Tcp_Listen_Opt){ __VA_ARGS__ })

typedef struct
{
    Mel_Executor* deliver;
    Mel_Net_Op*   out_op;
} Mel_Net_Accept_Opt;

Mel_Future* mel_net_listener_accept_opt(Mel_Net_Listener* listener, Mel_Net_Accept_Opt opt);
#define mel_net_listener_accept(listener, ...) mel_net_listener_accept_opt((listener), (Mel_Net_Accept_Opt){ __VA_ARGS__ })

Mel_Net_Address mel_net_listener_address(const Mel_Net_Listener* listener);
void            mel_net_listener_destroy(Mel_Net_Listener* listener);

#ifdef __cplusplus
}
#endif
