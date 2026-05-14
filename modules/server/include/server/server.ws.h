#pragma once

#include <core/compiler.h>

#include <core/types.h>
#include <string/string.str8.fwd.h>
#include "server.fwd.h"

typedef struct {
    const char*               path;
    Mel_Server_WS_Open_Fn     on_open;
    Mel_Server_WS_Msg_Fn      on_message;
    Mel_Server_WS_Close_Fn    on_close;
    void*                     user_data;
} Mel_Server_WS_Opt;

i32  mel_server_ws_add_opt(Mel_Server_Handle s, Mel_Server_WS_Opt opt, Mel_Server_Route_Handle* out);
#define mel_server_ws_add(s, out, ...) \
    mel_server_ws_add_opt((s), (Mel_Server_WS_Opt){__VA_ARGS__}, (out))

bool  mel_server_conn_alive          (Mel_Server_Handle s, Mel_Server_Conn_Handle c);
void* mel_server_conn_user_data_get  (Mel_Server_Handle s, Mel_Server_Conn_Handle c);
void  mel_server_conn_user_data_set  (Mel_Server_Handle s, Mel_Server_Conn_Handle c, void* v);
i32   mel_server_conn_close          (Mel_Server_Handle s, Mel_Server_Conn_Handle c);

i32   mel_server_ws_send_text        (Mel_Server_Handle s, Mel_Server_Conn_Handle c, str8 data);
i32   mel_server_ws_send_textf       (Mel_Server_Handle s, Mel_Server_Conn_Handle c, const char* fmt, ...) MEL_PRINTF_FORMAT(3, 4);
i32   mel_server_ws_send_bytes       (Mel_Server_Handle s, Mel_Server_Conn_Handle c, const void* p, usize n);

i32   mel_server_ws_broadcast_text   (Mel_Server_Handle s, const char* path, str8 data);
i32   mel_server_ws_broadcast_textf  (Mel_Server_Handle s, const char* path, const char* fmt, ...) MEL_PRINTF_FORMAT(3, 4);
