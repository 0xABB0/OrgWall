#pragma once

#include <core/compiler.h>

#include "core.types.h"
#include "server.fwd.h"

typedef struct {
    const char* ws_path;
} Mel_Server_PubSub_Mount_Opt;

i32  mel_server_pubsub_mount_opt(Mel_Server_Handle s, Mel_Server_PubSub_Mount_Opt opt);
#define mel_server_pubsub_mount(s, ...) \
    mel_server_pubsub_mount_opt((s), (Mel_Server_PubSub_Mount_Opt){__VA_ARGS__})

i32  mel_server_publish      (Mel_Server_Handle s, const char* topic, const char* fmt, ...) MEL_PRINTF_FORMAT(3, 4);
i32  mel_server_publish_bytes(Mel_Server_Handle s, const char* topic, const void* p, usize n);
u32  mel_server_topic_subs   (Mel_Server_Handle s, const char* topic);
