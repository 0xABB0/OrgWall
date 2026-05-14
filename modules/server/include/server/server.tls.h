#pragma once

#include <core/types.h>
#include "server.fwd.h"

typedef struct {
    const char* cert_pem;
    const char* key_pem;
    const char* ca_pem;
    bool        skip_verification;
} Mel_Server_TLS_Opt;

i32  mel_server_tls_set_opt(Mel_Server_Handle s, Mel_Server_TLS_Opt opt);
#define mel_server_tls_set(s, ...) \
    mel_server_tls_set_opt((s), (Mel_Server_TLS_Opt){__VA_ARGS__})
