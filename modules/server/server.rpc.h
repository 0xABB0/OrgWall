#pragma once

#include <core/compiler.h>

#include "core.types.h"
#include "string.str8.fwd.h"
#include "server.fwd.h"

typedef struct {
    const char* ws_path;
} Mel_Server_RPC_Mount_Opt;

i32  mel_server_rpc_mount_opt(Mel_Server_Handle s, Mel_Server_RPC_Mount_Opt opt);
#define mel_server_rpc_mount(s, ...) \
    mel_server_rpc_mount_opt((s), (Mel_Server_RPC_Mount_Opt){__VA_ARGS__})

typedef struct {
    const char*           method;
    Mel_Server_RPC_Fn     handler;
    void*                 user_data;
} Mel_Server_RPC_Opt;

i32  mel_server_rpc_add_opt(Mel_Server_Handle s, Mel_Server_RPC_Opt opt, Mel_Server_RPC_Method_Handle* out);
#define mel_server_rpc_add(s, out, ...) \
    mel_server_rpc_add_opt((s), (Mel_Server_RPC_Opt){__VA_ARGS__}, (out))

i32  mel_server_rpc_remove (Mel_Server_Handle s, Mel_Server_RPC_Method_Handle h);

str8                  mel_server_rpc_method_name  (Mel_Server_RPC_Req* r);
str8                  mel_server_rpc_params_json  (Mel_Server_RPC_Req* r);
Mel_Server_Conn_Handle mel_server_rpc_conn        (Mel_Server_RPC_Req* r);
void*                 mel_server_rpc_user_data    (Mel_Server_RPC_Req* r);

void                  mel_server_rpc_ok           (Mel_Server_RPC_Req* r, const char* result_fmt, ...) MEL_PRINTF_FORMAT(2, 3);
void                  mel_server_rpc_err          (Mel_Server_RPC_Req* r, i32 code, const char* msg);

#define MEL_SERVER_RPC_ERR_PARSE          -32700
#define MEL_SERVER_RPC_ERR_INVALID_REQUEST -32600
#define MEL_SERVER_RPC_ERR_METHOD_NOT_FOUND -32601
#define MEL_SERVER_RPC_ERR_INVALID_PARAMS  -32602
#define MEL_SERVER_RPC_ERR_INTERNAL        -32603
