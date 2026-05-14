#pragma once

#include <core/types.h>
#include "server.fwd.h"

#define MEL_SERVER_METHOD_ANY "*"

typedef struct {
    const char*           method;
    const char*           path;
    Mel_Server_Route_Fn   handler;
    void*                 user_data;
} Mel_Server_Route_Opt;

i32  mel_server_route_add_opt(Mel_Server_Handle s, Mel_Server_Route_Opt opt, Mel_Server_Route_Handle* out);
#define mel_server_route_add(s, out, ...) \
    mel_server_route_add_opt((s), (Mel_Server_Route_Opt){__VA_ARGS__}, (out))

i32  mel_server_route_remove (Mel_Server_Handle s, Mel_Server_Route_Handle r);

typedef struct {
    const char* url_prefix;
    const char* fs_root;
    const char* extra_headers;
} Mel_Server_Serve_Dir_Opt;

i32  mel_server_serve_dir_opt(Mel_Server_Handle s, Mel_Server_Serve_Dir_Opt opt, Mel_Server_Route_Handle* out);
#define mel_server_serve_dir(s, out, ...) \
    mel_server_serve_dir_opt((s), (Mel_Server_Serve_Dir_Opt){__VA_ARGS__}, (out))

typedef struct {
    const char* url_path;
    const char* fs_path;
    const char* extra_headers;
} Mel_Server_Serve_File_Opt;

i32  mel_server_serve_file_opt(Mel_Server_Handle s, Mel_Server_Serve_File_Opt opt, Mel_Server_Route_Handle* out);
#define mel_server_serve_file(s, out, ...) \
    mel_server_serve_file_opt((s), (Mel_Server_Serve_File_Opt){__VA_ARGS__}, (out))
