#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <string/str8.fwd.h>

#include <http/http.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    str8 scheme;
    str8 userinfo;
    str8 host;
    str8 path;
    str8 query;
    str8 fragment;
    u16  port;
    bool port_explicit;
} Mel_Http_Url;

Mel_Http_Status mel_http_url_parse(str8 text, Mel_Http_Url* out);

u16  mel_http_url_effective_port(const Mel_Http_Url* url);
str8 mel_http_url_target(const Mel_Http_Url* url, const Mel_Alloc* alloc);

str8            mel_http_percent_encode(str8 text, const Mel_Alloc* alloc);
Mel_Http_Status mel_http_percent_decode(str8 text, const Mel_Alloc* alloc, str8* out);

#ifdef __cplusplus
}
#endif
