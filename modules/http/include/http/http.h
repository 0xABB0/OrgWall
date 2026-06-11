#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <future/future.h>
#include <string/str8.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Net      Mel_Net;
typedef struct Mel_Executor Mel_Executor;
typedef struct Mel_Stream   Mel_Stream;

typedef struct Mel_Http Mel_Http;

typedef u32 Mel_Http_Status;

#define MEL_HTTP_SEVERITY_MASK      0x3u
#define MEL_HTTP_OK                 0u
#define MEL_HTTP_WARNED             1u
#define MEL_HTTP_ERROR              2u

#define MEL_HTTP_CANCELLED          (1u << 2)
#define MEL_HTTP_TIMED_OUT          (1u << 3)
#define MEL_HTTP_RESOLVE_FAILED     (1u << 4)
#define MEL_HTTP_CONNECT_FAILED     (1u << 5)
#define MEL_HTTP_TLS_FAILED         (1u << 6)
#define MEL_HTTP_MALFORMED          (1u << 7)
#define MEL_HTTP_TOO_MANY_REDIRECTS (1u << 8)
#define MEL_HTTP_BODY_INCOMPLETE    (1u << 9)
#define MEL_HTTP_BODY_TOO_LARGE     (1u << 10)
#define MEL_HTTP_BAD_URL            (1u << 11)
#define MEL_HTTP_UNAVAILABLE        (1u << 12)
#define MEL_HTTP_CLOSED             (1u << 13)
#define MEL_HTTP_SINK_FAILED        (1u << 14)

static inline bool mel_http_status_ok(Mel_Http_Status s) { return (s & MEL_HTTP_SEVERITY_MASK) == MEL_HTTP_OK; }
static inline bool mel_http_status_failed(Mel_Http_Status s) { return (s & MEL_HTTP_SEVERITY_MASK) == MEL_HTTP_ERROR; }
static inline bool mel_http_status_cancelled(Mel_Http_Status s) { return (s & MEL_HTTP_CANCELLED) != 0u; }
static inline bool mel_http_status_timed_out(Mel_Http_Status s) { return (s & MEL_HTTP_TIMED_OUT) != 0u; }

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Http_Op;

#define MEL_HTTP_OP_NULL ((Mel_Http_Op){ 0, 0 })

static inline bool mel_http_op_valid(Mel_Http_Op op) { return op.index != 0 || op.generation != 0; }

typedef struct
{
    str8 name;
    str8 value;
} Mel_Http_Header;

typedef struct
{
    Mel_Net*         net;
    const Mel_Alloc* alloc;
    u32              max_conns_per_host;
    i64              pool_idle_timeout_ns;
    usize            max_header_bytes;
} Mel_Http_Opt;

Mel_Http* mel_http_create_opt(Mel_Http_Opt opt);
#define mel_http_create(...) mel_http_create_opt((Mel_Http_Opt){ __VA_ARGS__ })

void mel_http_destroy(Mel_Http* http);

bool     mel_http_available(const Mel_Http* http);
Mel_Net* mel_http_net(const Mel_Http* http);
u32      mel_http_pending(const Mel_Http* http);

bool mel_http_cancel(Mel_Http* http, Mel_Http_Op op);

typedef struct
{
    str8                   method;
    str8                   url;
    const Mel_Http_Header* headers;
    usize                  header_count;
    str8                   body;
    Mel_Stream*            body_stream;
    i64                    body_len;
} Mel_Http_Request;

typedef struct
{
    i64           connect_timeout_ns;
    i64           response_timeout_ns;
    i64           total_timeout_ns;
    u32           max_redirects;
    bool          allow_insecure_redirect;
    usize         max_body_bytes;
    Mel_Stream*   sink;
    Mel_Executor* deliver;
    Mel_Http_Op*  out_op;
} Mel_Http_Fetch_Opt;

Mel_Future* mel_http_fetch_opt(Mel_Http* http, Mel_Http_Request req, Mel_Http_Fetch_Opt opt);
#define mel_http_fetch(http, req, ...) mel_http_fetch_opt((http), (req), (Mel_Http_Fetch_Opt){ __VA_ARGS__ })

typedef struct
{
    i32              status_code;
    str8             reason;
    Mel_Http_Header* headers;
    usize            header_count;
    str8             body;
    Mel_Http_Status  status;
    i32              os_error;
} Mel_Http_Result;

const Mel_Http_Result* mel_http_future_result(Mel_Future* f);
str8                   mel_http_future_take_body(Mel_Future* f);
void                   mel_http_future_release(Mel_Future* f);

bool mel_http_result_header(const Mel_Http_Result* r, str8 name, str8* out_value);

#ifdef __cplusplus
}
#endif
