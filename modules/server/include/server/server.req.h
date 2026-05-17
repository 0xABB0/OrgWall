#pragma once

#include <core/compiler.h>

#include <core/types.h>
#include <string/str8.fwd.h>
#include "server.fwd.h"

str8                      mel_server_req_method     (Mel_Server_Req* req);
str8                      mel_server_req_uri        (Mel_Server_Req* req);
str8                      mel_server_req_query      (Mel_Server_Req* req);
str8                      mel_server_req_body       (Mel_Server_Req* req);
Mel_Server_Handle         mel_server_req_server     (Mel_Server_Req* req);

bool                      mel_server_req_header     (Mel_Server_Req* req, const char* name, str8* out);
bool                      mel_server_req_query_var  (Mel_Server_Req* req, const char* name, char* buf, usize cap);
bool                      mel_server_req_form_var   (Mel_Server_Req* req, const char* name, char* buf, usize cap);
bool                      mel_server_req_param      (Mel_Server_Req* req, const char* name, str8* out);
bool                      mel_server_req_capture    (Mel_Server_Req* req, u32 index, str8* out);

void*                     mel_server_req_user_data_get(Mel_Server_Req* req, const char* key);
void                      mel_server_req_user_data_set(Mel_Server_Req* req, const char* key, void* value);

void                      mel_server_reply          (Mel_Server_Req* req, i32 status, const char* headers, const char* fmt, ...) MEL_PRINTF_FORMAT(4, 5);
void                      mel_server_reply_text     (Mel_Server_Req* req, i32 status, str8 body);
void                      mel_server_reply_json     (Mel_Server_Req* req, i32 status, const char* fmt, ...) MEL_PRINTF_FORMAT(3, 4);
void                      mel_server_reply_bytes    (Mel_Server_Req* req, i32 status, const void* p, usize n, const char* content_type);
void                      mel_server_reply_status   (Mel_Server_Req* req, i32 status);
void                      mel_server_reply_file     (Mel_Server_Req* req, const char* fs_path);
void                      mel_server_redirect       (Mel_Server_Req* req, i32 status, const char* location);

void                      mel_server_stream_begin   (Mel_Server_Req* req, i32 status, const char* headers);
void                      mel_server_stream_write   (Mel_Server_Req* req, const void* p, usize n);
void                      mel_server_stream_printf  (Mel_Server_Req* req, const char* fmt, ...) MEL_PRINTF_FORMAT(2, 3);
void                      mel_server_stream_end     (Mel_Server_Req* req);
