#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "server.fwd.h"
#include "server.cfg.h"

typedef struct {
    const char* bind;
    u32 publish_queue_capacity;
    u32 conn_outbox_capacity;
    void* user_data;
} Mel_Server_Opt;

typedef struct {
    const char* url;
} Mel_Server_Listen_Opt;

i32                       mel_server_module_init   (const Mel_Alloc* alloc);
void                      mel_server_module_shutdown(void);
const Mel_Alloc*          mel_server_module_alloc  (void);

i32                       mel_server_create_opt    (Mel_Server_Opt opt, Mel_Server_Handle* out);
#define mel_server_create(out, ...) \
    mel_server_create_opt((Mel_Server_Opt){__VA_ARGS__}, (out))

void                      mel_server_destroy       (Mel_Server_Handle s);
bool                      mel_server_alive         (Mel_Server_Handle s);
void*                     mel_server_user_data     (Mel_Server_Handle s);

i32                       mel_server_listen_opt    (Mel_Server_Handle s, Mel_Server_Listen_Opt opt, Mel_Server_Listener_Handle* out);
#define mel_server_listen(s, out, ...) \
    mel_server_listen_opt((s), (Mel_Server_Listen_Opt){__VA_ARGS__}, (out))

i32                       mel_server_listen_close  (Mel_Server_Handle s, Mel_Server_Listener_Handle l);

void                      mel_server_poll          (Mel_Server_Handle s, i32 timeout_ms);

i32                       mel_server_use           (Mel_Server_Handle s, Mel_Server_MW_Fn fn);

i32                       mel_server_call_main     (Mel_Server_Handle s, Mel_Server_Main_Fn fn, void* user_data);
