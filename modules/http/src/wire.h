#pragma once

#include <http/http.h>

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <collection/array.h>
#include <string/str8.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MEL_HTTP__BODY_NONE       0u
#define MEL_HTTP__BODY_LENGTH     1u
#define MEL_HTTP__BODY_CHUNKED    2u
#define MEL_HTTP__BODY_EOF        3u

#define MEL_HTTP__CHUNK_SIZE      0u
#define MEL_HTTP__CHUNK_DATA      1u
#define MEL_HTTP__CHUNK_DATA_CRLF 2u
#define MEL_HTTP__CHUNK_TRAILER   3u

typedef void (*Mel_Http__Body_Fn)(void* user, str8 span);

typedef Mel_Array(u8) Mel_Http__Buf;

typedef struct
{
    const Mel_Alloc* alloc;
    usize            max_head;
    bool             head_request;

    Mel_Http__Buf head;
    usize         scanned;
    bool          head_done;
    bool          done;

    i32  status_code;
    str8 reason;
    Mel_Array(Mel_Http_Header) headers;

    bool http10;
    bool connection_close;
    bool keep_alive_header;
    bool chunked;
    i64  content_length;

    u32 body_mode;
    u64 remaining;

    u32           chunk_phase;
    u64           chunk_remaining;
    Mel_Http__Buf chunk_line;
} Mel_Http__Resp_Parser;

void mel_http__parser_init(Mel_Http__Resp_Parser* p, const Mel_Alloc* alloc, usize max_head, bool head_request);
void mel_http__parser_free(Mel_Http__Resp_Parser* p);

Mel_Http_Status mel_http__parser_feed(Mel_Http__Resp_Parser* p, const u8* data, usize len, usize* out_consumed, Mel_Http__Body_Fn on_body, void* user);
Mel_Http_Status mel_http__parser_finish_eof(Mel_Http__Resp_Parser* p);

bool mel_http__parser_keep_alive(const Mel_Http__Resp_Parser* p);

str8 mel_http__wire_request_head(const Mel_Alloc* alloc, str8 method, str8 target, str8 host, u16 port, bool default_port, const Mel_Http_Header* headers, usize header_count, i64 body_len);

bool mel_http__header_ieq(str8 name, const char* want);

#ifdef __cplusplus
}
#endif
