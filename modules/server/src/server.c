#include <core/types.h>
#include <allocator/allocator.h>
#include <string/str8.h>
#include <log/log.h>
#include <collection.array/array.h>
#include <collection.map.hashmap/hashmap.h>
#include <collection.slotmap/slotmap.h>
#include <collection.mpmc/mpmc.h>

#include <server/server.fwd.h>
#include <server/server.cfg.h>
#include <server/server.h>
#include <server/server.req.h>
#include <server/server.http.h>
#include <server/server.ws.h>
#include <server/server.rpc.h>
#include <server/server.pubsub.h>
#include <server/server.tls.h>

#include <mongoose.h>

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================
 * Mongoose allocator shim (MG_ENABLE_CUSTOM_CALLOC=1)
 * Routed to module-global allocator set by mel_server_module_init.
 * ============================================================ */

static const Mel_Alloc* g_module_alloc = NULL;
static Mel_SlotMap      g_registry;
static bool             g_module_initialized = false;

void* mg_calloc(size_t count, size_t size)
{
    if (g_module_alloc == NULL) return calloc(count, size);
    usize total = count * size;
    if (total == 0) total = 1;
    void* p = mel_alloc(g_module_alloc, total);
    if (p != NULL) memset(p, 0, total);
    return p;
}

void mg_free(void* ptr)
{
    if (ptr == NULL) return;
    if (g_module_alloc == NULL) { free(ptr); return; }
    mel_dealloc(g_module_alloc, ptr);
}

/* ============================================================
 * Internal types
 * ============================================================ */

#define MEL__SERVER_ROUTE_KIND_REST     1
#define MEL__SERVER_ROUTE_KIND_STATIC_DIR 2
#define MEL__SERVER_ROUTE_KIND_STATIC_FILE 3
#define MEL__SERVER_ROUTE_KIND_WS       4
#define MEL__SERVER_ROUTE_KIND_RPC      5
#define MEL__SERVER_ROUTE_KIND_PUBSUB   6

#define MEL__SERVER_CONN_KIND_HTTP      0
#define MEL__SERVER_CONN_KIND_WS        1
#define MEL__SERVER_CONN_KIND_RPC       2
#define MEL__SERVER_CONN_KIND_PUBSUB    3

typedef Mel_Array(str8)                              Mel__Server_Str8_Array;
typedef Mel_Array(struct Mel__Server_Route_Slot)     Mel__Server_Route_Array;
typedef Mel_Array(Mel_Server_MW_Fn)                  Mel__Server_MW_Array;

typedef struct Mel__Server_Route_Slot {
    bool                       alive;
    u8                         kind;
    u32                        generation;
    str8                       method;
    str8                       pattern;
    Mel__Server_Str8_Array     param_names;
    Mel_Server_Route_Fn        rest_handler;
    Mel_Server_WS_Open_Fn      ws_on_open;
    Mel_Server_WS_Msg_Fn       ws_on_message;
    Mel_Server_WS_Close_Fn     ws_on_close;
    str8                       static_fs_path;
    str8                       static_extra_headers;
    void*                      user_data;
} Mel__Server_Route_Slot;

typedef struct {
    bool                       alive;
    u32                        generation;
    struct mg_connection*      mg;
} Mel__Server_Listener;

typedef struct {
    bool                       alive;
    u32                        generation;
    u8                         kind;
    Mel_Server_Conn_Handle     self;
    struct mg_connection*      mg;
    Mel__Server_Str8_Array     subbed_topics;
    void*                      user_data;
    str8                       ws_path;
    Mel_Server_WS_Open_Fn      ws_on_open;
    Mel_Server_WS_Msg_Fn       ws_on_message;
    Mel_Server_WS_Close_Fn     ws_on_close;
    void*                      route_user_data;
} Mel__Server_Conn;

typedef struct {
    bool                       alive;
    u32                        generation;
    str8                       method;
    Mel_Server_RPC_Fn          handler;
    void*                      user_data;
} Mel__Server_RPC_Method;

typedef struct {
    Mel_Server_Main_Fn         fn;
    void*                      user_data;
} Mel__Server_Main_Call_Msg;

typedef struct {
    str8                       topic;
    void*                      bytes;
    usize                      len;
    bool                       is_text;
} Mel__Server_Pub_Msg;

#define MEL__SERVER_MSG_KIND_PUBLISH    1
#define MEL__SERVER_MSG_KIND_CALL_MAIN  2

typedef struct {
    u32                        kind;
    union {
        Mel__Server_Pub_Msg        publish;
        Mel__Server_Main_Call_Msg  call_main;
    };
} Mel__Server_Msg;

struct Mel_Server {
    bool                       alive;
    u32                        generation;
    Mel_Server_Handle          self;

    Mel_Server_Opt             opt;
    str8                       bind_url;

    struct mg_mgr              mgr;

    Mel_SlotMap                routes;
    Mel_SlotMap                listeners;
    Mel_SlotMap                conns;
    Mel_SlotMap                rpc_methods;

    Mel__Server_MW_Array       middleware;

    str8                       rpc_ws_path;
    bool                       rpc_mounted;

    str8                       pubsub_ws_path;
    bool                       pubsub_mounted;

    Mel_Mpmc                   xt_queue;
    bool                       wakeup_initialized;

    bool                       tls_enabled;
    str8                       tls_cert_pem;
    str8                       tls_key_pem;
    str8                       tls_ca_pem;
    bool                       tls_skip_verification;
};

struct Mel_Server_Req {
    Mel_Server*                server;
    struct mg_connection*      mg_conn;
    struct mg_http_message*    hm;
    Mel__Server_Route_Slot*    route;
    Mel_Server_Conn*           conn;
    Mel__Server_Str8_Array     captures;
    Mel_HashMap                user_data;
    bool                       user_data_initialized;
    bool                       responded;
};

struct Mel_Server_RPC_Req {
    Mel_Server*                server;
    Mel__Server_Conn*          conn;
    str8                       method;
    str8                       params_json;
    str8                       id_json;
    bool                       has_id;
    bool                       responded;
};

/* ============================================================
 * Helpers: str8 <-> mg_str
 * ============================================================ */

static inline str8 mel__from_mg(struct mg_str s)
{
    return (str8){ .data = (u8*)s.buf, .len = (size)s.len };
}

static inline struct mg_str mel__to_mg(str8 s)
{
    return mg_str_n((const char*)s.data, (size_t)s.len);
}

static inline str8 mel__cstr(const char* s)
{
    return s ? (str8){ .data = (u8*)s, .len = (size)strlen(s) } : (str8){0};
}

static str8 mel__str8_dup(const Mel_Alloc* a, str8 s)
{
    if (s.len == 0) return (str8){0};
    u8* p = (u8*)mel_alloc(a, (usize)s.len);
    memcpy(p, s.data, (usize)s.len);
    return (str8){ .data = p, .len = s.len };
}

static void mel__str8_free(const Mel_Alloc* a, str8* s)
{
    if (s->data && s->len > 0) mel_dealloc(a, s->data);
    s->data = NULL;
    s->len = 0;
}

/* ============================================================
 * Module init / shutdown
 * ============================================================ */

i32 mel_server_module_init(const Mel_Alloc* alloc)
{
    if (g_module_initialized) return MEL_SERVER_ERR_ALREADY_INITIALIZED;
    if (alloc == NULL) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    g_module_alloc = alloc;
    mel_slotmap_init(&g_registry, alloc,
        .item_size = sizeof(Mel_Server),
        .initial_capacity = MEL_SERVER_REGISTRY_INITIAL_CAPACITY);
    g_module_initialized = true;
    mel_log_info(MEL_SERVER_LOG_DOMAIN, "module initialized");
    return MEL_SERVER_OK;
}

void mel_server_module_shutdown(void)
{
    if (!g_module_initialized) return;
    u32 n = mel_slotmap_count(&g_registry);
    if (n > 0) {
        mel_log_warn(MEL_SERVER_LOG_DOMAIN,
                     "shutdown with %u live servers; destroying them", n);
    }
    mel_slotmap_free(&g_registry);
    g_module_alloc = NULL;
    g_module_initialized = false;
}

const Mel_Alloc* mel_server_module_alloc(void) { return g_module_alloc; }

/* ============================================================
 * Server resolve
 * ============================================================ */

static Mel_Server* mel__server_resolve(Mel_Server_Handle h)
{
    if (!g_module_initialized) return NULL;
    Mel_Server* s = (Mel_Server*)mel_slotmap_get(&g_registry, h);
    if (s == NULL || !s->alive) return NULL;
    return s;
}

bool mel_server_alive(Mel_Server_Handle h) { return mel__server_resolve(h) != NULL; }

void* mel_server_user_data(Mel_Server_Handle h)
{
    Mel_Server* s = mel__server_resolve(h);
    return s ? s->opt.user_data : NULL;
}

/* ============================================================
 * Mongoose ev forward
 * ============================================================ */

static void mel__server_ev(struct mg_connection* c, int ev, void* ev_data);

/* ============================================================
 * Server create / destroy
 * ============================================================ */

i32 mel_server_create_opt(Mel_Server_Opt opt, Mel_Server_Handle* out)
{
    if (!g_module_initialized) return MEL_SERVER_ERR_NOT_INITIALIZED;
    if (out == NULL) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    *out = MEL_SERVER_HANDLE_NULL;

    Mel_Server zero = {0};
    Mel_Server_Handle h = mel_slotmap_insert(&g_registry, &zero);
    Mel_Server* s = (Mel_Server*)mel_slotmap_get(&g_registry, h);
    if (s == NULL) return MEL_SERVER_ERR_OOM;

    s->alive = true;
    s->generation = h.generation;
    s->self = h;
    s->opt = opt;
    if (s->opt.publish_queue_capacity == 0) s->opt.publish_queue_capacity = MEL_SERVER_DEFAULT_PUBLISH_QUEUE_CAPACITY;
    if (s->opt.conn_outbox_capacity == 0)   s->opt.conn_outbox_capacity   = MEL_SERVER_DEFAULT_CONN_OUTBOX_CAPACITY;
    if (opt.bind != NULL) s->bind_url = mel__str8_dup(g_module_alloc, mel__cstr(opt.bind));

    mg_mgr_init(&s->mgr);
    s->mgr.userdata = s;

    mel_slotmap_init(&s->routes,      g_module_alloc, .item_size = sizeof(Mel__Server_Route_Slot), .initial_capacity = 16);
    mel_slotmap_init(&s->listeners,   g_module_alloc, .item_size = sizeof(Mel__Server_Listener),   .initial_capacity = 4);
    mel_slotmap_init(&s->conns,       g_module_alloc, .item_size = sizeof(Mel__Server_Conn),       .initial_capacity = 32);
    mel_slotmap_init(&s->rpc_methods, g_module_alloc, .item_size = sizeof(Mel__Server_RPC_Method), .initial_capacity = 16);

    mel_array_init(&s->middleware, g_module_alloc);

    mel_mpmc_init(&s->xt_queue, s->opt.publish_queue_capacity, g_module_alloc);
    s->wakeup_initialized = mg_wakeup_init(&s->mgr);
    if (!s->wakeup_initialized) {
        mel_log_warn(MEL_SERVER_LOG_DOMAIN, "mg_wakeup_init failed; cross-thread publish/call_main disabled");
    }

    *out = h;
    mel_log_info(MEL_SERVER_LOG_DOMAIN, "server %u:%u created", h.index, h.generation);
    return MEL_SERVER_OK;
}

static void mel__server_drain_xt_queue(Mel_Server* s);

void mel_server_destroy(Mel_Server_Handle h)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return;

    mel__server_drain_xt_queue(s);

    mg_mgr_free(&s->mgr);

    for (u32 i = 0; i < s->routes.slot_count; i++) {
        Mel_SlotMap_Handle rh = mel_slotmap_handle_make(i, s->routes.slots[i].generation);
        Mel__Server_Route_Slot* r = (Mel__Server_Route_Slot*)mel_slotmap_get(&s->routes, rh);
        if (r == NULL || !r->alive) continue;
        mel__str8_free(g_module_alloc, &r->method);
        mel__str8_free(g_module_alloc, &r->pattern);
        mel__str8_free(g_module_alloc, &r->static_fs_path);
        mel__str8_free(g_module_alloc, &r->static_extra_headers);
        for (usize j = 0; j < r->param_names.count; j++) {
            mel__str8_free(g_module_alloc, &r->param_names.items[j]);
        }
        mel_array_free(&r->param_names);
    }
    mel_slotmap_free(&s->routes);

    for (u32 i = 0; i < s->conns.slot_count; i++) {
        Mel_SlotMap_Handle ch = mel_slotmap_handle_make(i, s->conns.slots[i].generation);
        Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
        if (cn == NULL || !cn->alive) continue;
        for (usize j = 0; j < cn->subbed_topics.count; j++) {
            mel__str8_free(g_module_alloc, &cn->subbed_topics.items[j]);
        }
        mel_array_free(&cn->subbed_topics);
        mel__str8_free(g_module_alloc, &cn->ws_path);
    }
    mel_slotmap_free(&s->conns);

    for (u32 i = 0; i < s->rpc_methods.slot_count; i++) {
        Mel_SlotMap_Handle mh = mel_slotmap_handle_make(i, s->rpc_methods.slots[i].generation);
        Mel__Server_RPC_Method* m = (Mel__Server_RPC_Method*)mel_slotmap_get(&s->rpc_methods, mh);
        if (m == NULL || !m->alive) continue;
        mel__str8_free(g_module_alloc, &m->method);
    }
    mel_slotmap_free(&s->rpc_methods);

    mel_slotmap_free(&s->listeners);
    mel_array_free(&s->middleware);

    mel_mpmc_free(&s->xt_queue);

    mel__str8_free(g_module_alloc, &s->bind_url);
    mel__str8_free(g_module_alloc, &s->rpc_ws_path);
    mel__str8_free(g_module_alloc, &s->pubsub_ws_path);
    mel__str8_free(g_module_alloc, &s->tls_cert_pem);
    mel__str8_free(g_module_alloc, &s->tls_key_pem);
    mel__str8_free(g_module_alloc, &s->tls_ca_pem);

    s->alive = false;
    mel_slotmap_remove(&g_registry, h);
    mel_log_info(MEL_SERVER_LOG_DOMAIN, "server %u:%u destroyed", h.index, h.generation);
}

/* ============================================================
 * Listen
 * ============================================================ */

i32 mel_server_listen_opt(Mel_Server_Handle h, Mel_Server_Listen_Opt opt, Mel_Server_Listener_Handle* out)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (out) *out = MEL_SERVER_HANDLE_NULL;

    const char* url = opt.url ? opt.url : (const char*)s->bind_url.data;
    if (url == NULL) return MEL_SERVER_ERR_INVALID_ARGUMENT;

    struct mg_connection* c = mg_http_listen(&s->mgr, url, mel__server_ev, NULL);
    if (c == NULL) {
        mel_log_error(MEL_SERVER_LOG_DOMAIN, "listen on %s failed", url);
        return MEL_SERVER_ERR_LISTEN;
    }
    *(Mel_Server**)c->data = s;

    Mel__Server_Listener z = { .alive = true, .mg = c };
    Mel_Server_Listener_Handle lh = mel_slotmap_insert(&s->listeners, &z);
    Mel__Server_Listener* l = (Mel__Server_Listener*)mel_slotmap_get(&s->listeners, lh);
    if (l) l->generation = lh.generation;
    if (out) *out = lh;

    mel_log_info(MEL_SERVER_LOG_DOMAIN, "listening on %s", url);
    return MEL_SERVER_OK;
}

i32 mel_server_listen_close(Mel_Server_Handle h, Mel_Server_Listener_Handle lh)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    Mel__Server_Listener* l = (Mel__Server_Listener*)mel_slotmap_get(&s->listeners, lh);
    if (l == NULL || !l->alive) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (l->mg) l->mg->is_closing = 1;
    l->alive = false;
    mel_slotmap_remove(&s->listeners, lh);
    return MEL_SERVER_OK;
}

/* ============================================================
 * Poll
 * ============================================================ */

void mel_server_poll(Mel_Server_Handle h, i32 timeout_ms)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return;
    mg_mgr_poll(&s->mgr, timeout_ms < 0 ? 0 : timeout_ms);
}

/* ============================================================
 * Use middleware
 * ============================================================ */

i32 mel_server_use(Mel_Server_Handle h, Mel_Server_MW_Fn fn)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (fn == NULL) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    mel_array_push(&s->middleware, fn);
    return MEL_SERVER_OK;
}

/* ============================================================
 * Cross-thread bridge
 * ============================================================ */

i32 mel_server_call_main(Mel_Server_Handle h, Mel_Server_Main_Fn fn, void* user_data)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (fn == NULL) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    if (!s->wakeup_initialized) return MEL_SERVER_ERR_UNIMPLEMENTED;

    Mel__Server_Msg* msg = (Mel__Server_Msg*)mel_alloc(g_module_alloc, sizeof(*msg));
    msg->kind = MEL__SERVER_MSG_KIND_CALL_MAIN;
    msg->call_main.fn = fn;
    msg->call_main.user_data = user_data;
    if (!mel_mpmc_push(&s->xt_queue, msg)) {
        mel_dealloc(g_module_alloc, msg);
        return MEL_SERVER_ERR_QUEUE_FULL;
    }
    mg_wakeup(&s->mgr, mel_slotmap_handle_pack64(s->self), NULL, 0);
    return MEL_SERVER_OK;
}

static void mel__server_drain_xt_queue(Mel_Server* s)
{
    void* p = NULL;
    while (mel_mpmc_pop(&s->xt_queue, &p)) {
        Mel__Server_Msg* msg = (Mel__Server_Msg*)p;
        if (msg == NULL) continue;
        if (msg->kind == MEL__SERVER_MSG_KIND_CALL_MAIN) {
            msg->call_main.fn(s->self, msg->call_main.user_data);
        } else if (msg->kind == MEL__SERVER_MSG_KIND_PUBLISH) {
            for (u32 i = 0; i < s->conns.slot_count; i++) {
                Mel_SlotMap_Handle ch = mel_slotmap_handle_make(i, s->conns.slots[i].generation);
                Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
                if (cn == NULL || !cn->alive) continue;
                if (cn->kind != MEL__SERVER_CONN_KIND_PUBSUB) continue;
                bool subscribed = false;
                for (usize j = 0; j < cn->subbed_topics.count; j++) {
                    if (str8_equals(cn->subbed_topics.items[j], msg->publish.topic)) {
                        subscribed = true;
                        break;
                    }
                }
                if (!subscribed) continue;
                int op = msg->publish.is_text ? WEBSOCKET_OP_TEXT : WEBSOCKET_OP_BINARY;
                if (cn->mg && !cn->mg->is_closing) {
                    mg_ws_send(cn->mg, msg->publish.bytes, msg->publish.len, op);
                }
            }
            if (msg->publish.bytes) mel_dealloc(g_module_alloc, msg->publish.bytes);
            mel__str8_free(g_module_alloc, &msg->publish.topic);
        }
        mel_dealloc(g_module_alloc, msg);
    }
}

/* ============================================================
 * Path translation: :id, *splat -> mongoose * and #
 * ============================================================ */

static void mel__server_translate_path(const Mel_Alloc* a, str8 input,
                                       str8* out_pattern,
                                       Mel__Server_Str8_Array* out_names)
{
    mel_array_init(out_names, a);

    char* buf = (char*)mel_alloc(a, (usize)input.len + 1);
    usize bp = 0;
    usize i = 0;
    while (i < (usize)input.len) {
        char ch = (char)input.data[i];
        if (ch == ':' || ch == '*') {
            char trans = ch == ':' ? '*' : '#';
            i++;
            usize start = i;
            while (i < (usize)input.len &&
                   ((char)input.data[i] != '/') &&
                   ((char)input.data[i] != '.')) {
                i++;
            }
            usize name_len = i - start;
            str8 name = (str8){0};
            if (name_len > 0) {
                u8* p = (u8*)mel_alloc(a, name_len);
                memcpy(p, input.data + start, name_len);
                name = (str8){ .data = p, .len = (size)name_len };
            }
            mel_array_push(out_names, name);
            buf[bp++] = trans;
        } else {
            buf[bp++] = ch;
            i++;
        }
    }
    buf[bp] = 0;

    out_pattern->data = (u8*)buf;
    out_pattern->len = (size)bp;
}

/* ============================================================
 * Routes
 * ============================================================ */

i32 mel_server_route_add_opt(Mel_Server_Handle h, Mel_Server_Route_Opt opt,
                             Mel_Server_Route_Handle* out)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (opt.path == NULL || opt.handler == NULL) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    if (out) *out = MEL_SERVER_HANDLE_NULL;

    Mel__Server_Route_Slot z = {0};
    z.alive = true;
    z.kind = MEL__SERVER_ROUTE_KIND_REST;
    z.method = mel__str8_dup(g_module_alloc, mel__cstr(opt.method ? opt.method : MEL_SERVER_METHOD_ANY));
    mel__server_translate_path(g_module_alloc, mel__cstr(opt.path), &z.pattern, &z.param_names);
    z.rest_handler = opt.handler;
    z.user_data = opt.user_data;

    Mel_Server_Route_Handle rh = mel_slotmap_insert(&s->routes, &z);
    Mel__Server_Route_Slot* r = (Mel__Server_Route_Slot*)mel_slotmap_get(&s->routes, rh);
    if (r) r->generation = rh.generation;
    if (out) *out = rh;
    mel_log_debug(MEL_SERVER_LOG_DOMAIN_ROUTER,
                  "route + %.*s %s", (int)z.method.len, (const char*)z.method.data, opt.path);
    return MEL_SERVER_OK;
}

i32 mel_server_route_remove(Mel_Server_Handle h, Mel_Server_Route_Handle rh)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    Mel__Server_Route_Slot* r = (Mel__Server_Route_Slot*)mel_slotmap_get(&s->routes, rh);
    if (r == NULL || !r->alive) return MEL_SERVER_ERR_INVALID_HANDLE;
    mel__str8_free(g_module_alloc, &r->method);
    mel__str8_free(g_module_alloc, &r->pattern);
    mel__str8_free(g_module_alloc, &r->static_fs_path);
    mel__str8_free(g_module_alloc, &r->static_extra_headers);
    for (usize i = 0; i < r->param_names.count; i++) {
        mel__str8_free(g_module_alloc, &r->param_names.items[i]);
    }
    mel_array_free(&r->param_names);
    r->alive = false;
    mel_slotmap_remove(&s->routes, rh);
    return MEL_SERVER_OK;
}

/* ============================================================
 * Static
 * ============================================================ */

i32 mel_server_serve_dir_opt(Mel_Server_Handle h, Mel_Server_Serve_Dir_Opt opt,
                             Mel_Server_Route_Handle* out)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (opt.fs_root == NULL) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    const char* prefix = opt.url_prefix ? opt.url_prefix : "/";
    if (out) *out = MEL_SERVER_HANDLE_NULL;

    Mel__Server_Route_Slot z = {0};
    z.alive = true;
    z.kind = MEL__SERVER_ROUTE_KIND_STATIC_DIR;
    z.method = mel__str8_dup(g_module_alloc, mel__cstr("GET"));
    mel_array_init(&z.param_names, g_module_alloc);
    str8 pattern_in;
    if (strcmp(prefix, "/") == 0) {
        pattern_in = mel__cstr("/#splat");
    } else {
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s%s*splat",
                 prefix,
                 (prefix[strlen(prefix)-1] == '/') ? "" : "/");
        pattern_in = mel__cstr(tmp);
    }
    mel__server_translate_path(g_module_alloc, pattern_in, &z.pattern, &z.param_names);
    z.static_fs_path = mel__str8_dup(g_module_alloc, mel__cstr(opt.fs_root));
    z.static_extra_headers = opt.extra_headers
        ? mel__str8_dup(g_module_alloc, mel__cstr(opt.extra_headers))
        : (str8){0};

    Mel_Server_Route_Handle rh = mel_slotmap_insert(&s->routes, &z);
    Mel__Server_Route_Slot* r = (Mel__Server_Route_Slot*)mel_slotmap_get(&s->routes, rh);
    if (r) r->generation = rh.generation;
    if (out) *out = rh;
    mel_log_info(MEL_SERVER_LOG_DOMAIN, "serving %s from %s", prefix, opt.fs_root);
    return MEL_SERVER_OK;
}

i32 mel_server_serve_file_opt(Mel_Server_Handle h, Mel_Server_Serve_File_Opt opt,
                              Mel_Server_Route_Handle* out)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (opt.url_path == NULL || opt.fs_path == NULL) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    if (out) *out = MEL_SERVER_HANDLE_NULL;

    Mel__Server_Route_Slot z = {0};
    z.alive = true;
    z.kind = MEL__SERVER_ROUTE_KIND_STATIC_FILE;
    z.method = mel__str8_dup(g_module_alloc, mel__cstr("GET"));
    mel__server_translate_path(g_module_alloc, mel__cstr(opt.url_path), &z.pattern, &z.param_names);
    z.static_fs_path = mel__str8_dup(g_module_alloc, mel__cstr(opt.fs_path));
    z.static_extra_headers = opt.extra_headers
        ? mel__str8_dup(g_module_alloc, mel__cstr(opt.extra_headers))
        : (str8){0};

    Mel_Server_Route_Handle rh = mel_slotmap_insert(&s->routes, &z);
    Mel__Server_Route_Slot* r = (Mel__Server_Route_Slot*)mel_slotmap_get(&s->routes, rh);
    if (r) r->generation = rh.generation;
    if (out) *out = rh;
    return MEL_SERVER_OK;
}

/* ============================================================
 * WebSocket route
 * ============================================================ */

i32 mel_server_ws_add_opt(Mel_Server_Handle h, Mel_Server_WS_Opt opt,
                          Mel_Server_Route_Handle* out)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (opt.path == NULL) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    if (out) *out = MEL_SERVER_HANDLE_NULL;

    Mel__Server_Route_Slot z = {0};
    z.alive = true;
    z.kind = MEL__SERVER_ROUTE_KIND_WS;
    z.method = mel__str8_dup(g_module_alloc, mel__cstr("GET"));
    mel__server_translate_path(g_module_alloc, mel__cstr(opt.path), &z.pattern, &z.param_names);
    z.ws_on_open = opt.on_open;
    z.ws_on_message = opt.on_message;
    z.ws_on_close = opt.on_close;
    z.user_data = opt.user_data;

    Mel_Server_Route_Handle rh = mel_slotmap_insert(&s->routes, &z);
    Mel__Server_Route_Slot* r = (Mel__Server_Route_Slot*)mel_slotmap_get(&s->routes, rh);
    if (r) r->generation = rh.generation;
    if (out) *out = rh;
    return MEL_SERVER_OK;
}

/* ============================================================
 * RPC
 * ============================================================ */

static void mel__server_rpc_dispatch(Mel_Server* s, Mel__Server_Conn* cn, str8 frame);

static void mel__server_rpc_ws_open(Mel_Server_Handle sh, Mel_Server_Conn_Handle ch) {
    (void)sh; (void)ch;
}

static void mel__server_rpc_ws_msg(Mel_Server_Handle sh, Mel_Server_Conn_Handle ch, str8 data, bool is_binary) {
    (void)is_binary;
    Mel_Server* s = mel__server_resolve(sh);
    if (s == NULL) return;
    Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
    if (cn == NULL || !cn->alive) return;
    mel__server_rpc_dispatch(s, cn, data);
}

i32 mel_server_rpc_mount_opt(Mel_Server_Handle h, Mel_Server_RPC_Mount_Opt opt)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    const char* path = opt.ws_path ? opt.ws_path : "/rpc";
    if (s->rpc_mounted) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    s->rpc_ws_path = mel__str8_dup(g_module_alloc, mel__cstr(path));
    s->rpc_mounted = true;

    Mel_Server_WS_Opt wopt = {
        .path = path,
        .on_open = mel__server_rpc_ws_open,
        .on_message = mel__server_rpc_ws_msg,
    };
    Mel_Server_Route_Handle rh;
    return mel_server_ws_add_opt(h, wopt, &rh);
}

i32 mel_server_rpc_add_opt(Mel_Server_Handle h, Mel_Server_RPC_Opt opt,
                           Mel_Server_RPC_Method_Handle* out)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (opt.method == NULL || opt.handler == NULL) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    if (out) *out = MEL_SERVER_HANDLE_NULL;

    Mel__Server_RPC_Method z = {0};
    z.alive = true;
    z.method = mel__str8_dup(g_module_alloc, mel__cstr(opt.method));
    z.handler = opt.handler;
    z.user_data = opt.user_data;
    Mel_Server_RPC_Method_Handle mh = mel_slotmap_insert(&s->rpc_methods, &z);
    Mel__Server_RPC_Method* m = (Mel__Server_RPC_Method*)mel_slotmap_get(&s->rpc_methods, mh);
    if (m) m->generation = mh.generation;
    if (out) *out = mh;
    return MEL_SERVER_OK;
}

i32 mel_server_rpc_remove(Mel_Server_Handle h, Mel_Server_RPC_Method_Handle mh)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    Mel__Server_RPC_Method* m = (Mel__Server_RPC_Method*)mel_slotmap_get(&s->rpc_methods, mh);
    if (m == NULL || !m->alive) return MEL_SERVER_ERR_INVALID_HANDLE;
    mel__str8_free(g_module_alloc, &m->method);
    m->alive = false;
    mel_slotmap_remove(&s->rpc_methods, mh);
    return MEL_SERVER_OK;
}

static void mel__server_rpc_dispatch(Mel_Server* s, Mel__Server_Conn* cn, str8 frame)
{
    struct mg_str j = mel__to_mg(frame);

    str8 method  = mel__from_mg(mg_json_get_tok(j, "$.method"));
    str8 params  = mel__from_mg(mg_json_get_tok(j, "$.params"));
    str8 id_tok  = mel__from_mg(mg_json_get_tok(j, "$.id"));
    bool has_id  = id_tok.len > 0;

    if (method.len == 0) {
        if (has_id) {
            mg_ws_printf(cn->mg, WEBSOCKET_OP_TEXT,
                         "{\"jsonrpc\":\"2.0\",\"id\":%.*s,\"error\":{\"code\":%d,\"message\":\"invalid request\"}}",
                         (int)id_tok.len, (const char*)id_tok.data,
                         MEL_SERVER_RPC_ERR_INVALID_REQUEST);
        }
        return;
    }

    str8 method_str = method;
    if (method.len >= 2 && method.data[0] == '"' && method.data[method.len-1] == '"') {
        method_str = (str8){ .data = method.data + 1, .len = method.len - 2 };
    }

    Mel__Server_RPC_Method* found = NULL;
    for (u32 i = 0; i < s->rpc_methods.slot_count; i++) {
        Mel_SlotMap_Handle mh = mel_slotmap_handle_make(i, s->rpc_methods.slots[i].generation);
        Mel__Server_RPC_Method* m = (Mel__Server_RPC_Method*)mel_slotmap_get(&s->rpc_methods, mh);
        if (m == NULL || !m->alive) continue;
        if (str8_equals(m->method, method_str)) { found = m; break; }
    }

    if (found == NULL) {
        if (has_id) {
            mg_ws_printf(cn->mg, WEBSOCKET_OP_TEXT,
                         "{\"jsonrpc\":\"2.0\",\"id\":%.*s,\"error\":{\"code\":%d,\"message\":\"method not found\"}}",
                         (int)id_tok.len, (const char*)id_tok.data,
                         MEL_SERVER_RPC_ERR_METHOD_NOT_FOUND);
        }
        return;
    }

    Mel_Server_RPC_Req r = {0};
    r.server = s;
    r.conn = cn;
    r.method = method_str;
    r.params_json = params;
    r.id_json = id_tok;
    r.has_id = has_id;
    found->handler(&r);

    if (has_id && !r.responded) {
        mg_ws_printf(cn->mg, WEBSOCKET_OP_TEXT,
                     "{\"jsonrpc\":\"2.0\",\"id\":%.*s,\"error\":{\"code\":%d,\"message\":\"no response\"}}",
                     (int)id_tok.len, (const char*)id_tok.data,
                     MEL_SERVER_RPC_ERR_INTERNAL);
    }
}

str8 mel_server_rpc_method_name(Mel_Server_RPC_Req* r) { return r ? r->method : (str8){0}; }
str8 mel_server_rpc_params_json(Mel_Server_RPC_Req* r) { return r ? r->params_json : (str8){0}; }
Mel_Server_Conn_Handle mel_server_rpc_conn(Mel_Server_RPC_Req* r)
{
    return (r && r->conn) ? r->conn->self : MEL_SERVER_HANDLE_NULL;
}

void mel_server_rpc_ok(Mel_Server_RPC_Req* r, const char* fmt, ...)
{
    if (r == NULL || !r->has_id || r->responded) return;
    char body[2048];
    va_list ap; va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    mg_ws_printf(r->conn->mg, WEBSOCKET_OP_TEXT,
                 "{\"jsonrpc\":\"2.0\",\"id\":%.*s,\"result\":%s}",
                 (int)r->id_json.len, (const char*)r->id_json.data, body);
    r->responded = true;
}

void mel_server_rpc_err(Mel_Server_RPC_Req* r, i32 code, const char* msg)
{
    if (r == NULL || !r->has_id || r->responded) return;
    mg_ws_printf(r->conn->mg, WEBSOCKET_OP_TEXT,
                 "{\"jsonrpc\":\"2.0\",\"id\":%.*s,\"error\":{\"code\":%d,\"message\":\"%s\"}}",
                 (int)r->id_json.len, (const char*)r->id_json.data, code, msg ? msg : "");
    r->responded = true;
}

/* ============================================================
 * PubSub
 * ============================================================ */

static void mel__server_pubsub_handle_sub(Mel_Server* s, Mel__Server_Conn* cn, str8 frame, bool is_unsub);

static void mel__server_pubsub_ws_open(Mel_Server_Handle sh, Mel_Server_Conn_Handle ch) {
    (void)sh; (void)ch;
}

static void mel__server_pubsub_ws_msg(Mel_Server_Handle sh, Mel_Server_Conn_Handle ch, str8 data, bool is_binary)
{
    (void)is_binary;
    Mel_Server* s = mel__server_resolve(sh);
    if (s == NULL) return;
    Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
    if (cn == NULL || !cn->alive) return;

    struct mg_str j = mel__to_mg(data);
    str8 sub_tok = mel__from_mg(mg_json_get_tok(j, "$.sub"));
    if (sub_tok.len > 0) { mel__server_pubsub_handle_sub(s, cn, sub_tok, false); return; }
    str8 unsub_tok = mel__from_mg(mg_json_get_tok(j, "$.unsub"));
    if (unsub_tok.len > 0) { mel__server_pubsub_handle_sub(s, cn, unsub_tok, true); return; }
}

static void mel__server_pubsub_ws_close(Mel_Server_Handle sh, Mel_Server_Conn_Handle ch) {
    (void)sh; (void)ch;
}

static void mel__server_pubsub_handle_sub(Mel_Server* s, Mel__Server_Conn* cn, str8 topic_tok, bool is_unsub)
{
    str8 t = topic_tok;
    if (t.len >= 2 && t.data[0] == '"' && t.data[t.len-1] == '"') {
        t = (str8){ .data = t.data + 1, .len = t.len - 2 };
    }
    if (is_unsub) {
        for (usize i = 0; i < cn->subbed_topics.count; i++) {
            if (str8_equals(cn->subbed_topics.items[i], t)) {
                mel__str8_free(g_module_alloc, &cn->subbed_topics.items[i]);
                mel_array_remove_unordered(&cn->subbed_topics, i);
                break;
            }
        }
        return;
    }
    for (usize i = 0; i < cn->subbed_topics.count; i++) {
        if (str8_equals(cn->subbed_topics.items[i], t)) return;
    }
    str8 dup = mel__str8_dup(g_module_alloc, t);
    mel_array_push(&cn->subbed_topics, dup);
    cn->kind = MEL__SERVER_CONN_KIND_PUBSUB;
    mel_log_debug(MEL_SERVER_LOG_DOMAIN_PUBSUB,
                  "+sub %.*s", (int)t.len, (const char*)t.data);
    (void)s;
}

i32 mel_server_pubsub_mount_opt(Mel_Server_Handle h, Mel_Server_PubSub_Mount_Opt opt)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    const char* path = opt.ws_path ? opt.ws_path : "/pubsub";
    if (s->pubsub_mounted) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    s->pubsub_ws_path = mel__str8_dup(g_module_alloc, mel__cstr(path));
    s->pubsub_mounted = true;

    Mel_Server_WS_Opt wopt = {
        .path = path,
        .on_open = mel__server_pubsub_ws_open,
        .on_message = mel__server_pubsub_ws_msg,
        .on_close = mel__server_pubsub_ws_close,
    };
    Mel_Server_Route_Handle rh;
    return mel_server_ws_add_opt(h, wopt, &rh);
}

i32 mel_server_publish_bytes(Mel_Server_Handle h, const char* topic, const void* p, usize n)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (topic == NULL) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    if (!s->wakeup_initialized) return MEL_SERVER_ERR_UNIMPLEMENTED;

    Mel__Server_Msg* msg = (Mel__Server_Msg*)mel_alloc(g_module_alloc, sizeof(*msg));
    msg->kind = MEL__SERVER_MSG_KIND_PUBLISH;
    msg->publish.topic = mel__str8_dup(g_module_alloc, mel__cstr(topic));
    msg->publish.is_text = false;
    msg->publish.len = n;
    if (n > 0) {
        msg->publish.bytes = mel_alloc(g_module_alloc, n);
        memcpy(msg->publish.bytes, p, n);
    } else {
        msg->publish.bytes = NULL;
    }
    if (!mel_mpmc_push(&s->xt_queue, msg)) {
        if (msg->publish.bytes) mel_dealloc(g_module_alloc, msg->publish.bytes);
        mel__str8_free(g_module_alloc, &msg->publish.topic);
        mel_dealloc(g_module_alloc, msg);
        return MEL_SERVER_ERR_QUEUE_FULL;
    }
    mg_wakeup(&s->mgr, mel_slotmap_handle_pack64(s->self), NULL, 0);
    return MEL_SERVER_OK;
}

i32 mel_server_publish(Mel_Server_Handle h, const char* topic, const char* fmt, ...)
{
    char buf[MEL_SERVER_DEFAULT_PUBLISH_PAYLOAD_LIMIT];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    if ((usize)n > sizeof(buf)) n = (int)sizeof(buf);

    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (topic == NULL) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    if (!s->wakeup_initialized) return MEL_SERVER_ERR_UNIMPLEMENTED;

    Mel__Server_Msg* msg = (Mel__Server_Msg*)mel_alloc(g_module_alloc, sizeof(*msg));
    msg->kind = MEL__SERVER_MSG_KIND_PUBLISH;
    msg->publish.topic = mel__str8_dup(g_module_alloc, mel__cstr(topic));
    msg->publish.is_text = true;
    msg->publish.len = (usize)n;
    msg->publish.bytes = mel_alloc(g_module_alloc, (usize)n);
    memcpy(msg->publish.bytes, buf, (usize)n);

    if (!mel_mpmc_push(&s->xt_queue, msg)) {
        mel_dealloc(g_module_alloc, msg->publish.bytes);
        mel__str8_free(g_module_alloc, &msg->publish.topic);
        mel_dealloc(g_module_alloc, msg);
        return MEL_SERVER_ERR_QUEUE_FULL;
    }
    mg_wakeup(&s->mgr, mel_slotmap_handle_pack64(s->self), NULL, 0);
    return MEL_SERVER_OK;
}

u32 mel_server_topic_subs(Mel_Server_Handle h, const char* topic)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL || topic == NULL) return 0;
    str8 t = mel__cstr(topic);
    u32 count = 0;
    for (u32 i = 0; i < s->conns.slot_count; i++) {
        Mel_SlotMap_Handle ch = mel_slotmap_handle_make(i, s->conns.slots[i].generation);
        Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
        if (cn == NULL || !cn->alive) continue;
        if (cn->kind != MEL__SERVER_CONN_KIND_PUBSUB) continue;
        for (usize j = 0; j < cn->subbed_topics.count; j++) {
            if (str8_equals(cn->subbed_topics.items[j], t)) { count++; break; }
        }
    }
    return count;
}

/* ============================================================
 * TLS
 * ============================================================ */

i32 mel_server_tls_set_opt(Mel_Server_Handle h, Mel_Server_TLS_Opt opt)
{
#if MEL_SERVER_TLS
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    s->tls_enabled = (opt.cert_pem != NULL && opt.key_pem != NULL);
    if (opt.cert_pem) s->tls_cert_pem = mel__str8_dup(g_module_alloc, mel__cstr(opt.cert_pem));
    if (opt.key_pem)  s->tls_key_pem  = mel__str8_dup(g_module_alloc, mel__cstr(opt.key_pem));
    if (opt.ca_pem)   s->tls_ca_pem   = mel__str8_dup(g_module_alloc, mel__cstr(opt.ca_pem));
    s->tls_skip_verification = opt.skip_verification;
    return MEL_SERVER_OK;
#else
    (void)h; (void)opt;
    return MEL_SERVER_ERR_TLS_DISABLED;
#endif
}

/* ============================================================
 * Conn (WS)
 * ============================================================ */

bool mel_server_conn_alive(Mel_Server_Handle h, Mel_Server_Conn_Handle ch)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return false;
    Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
    return cn != NULL && cn->alive;
}

void* mel_server_conn_user_data_get(Mel_Server_Handle h, Mel_Server_Conn_Handle ch)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return NULL;
    Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
    return (cn && cn->alive) ? cn->user_data : NULL;
}

void mel_server_conn_user_data_set(Mel_Server_Handle h, Mel_Server_Conn_Handle ch, void* v)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return;
    Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
    if (cn && cn->alive) cn->user_data = v;
}

i32 mel_server_conn_close(Mel_Server_Handle h, Mel_Server_Conn_Handle ch)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
    if (cn == NULL || !cn->alive) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (cn->mg) cn->mg->is_closing = 1;
    return MEL_SERVER_OK;
}

i32 mel_server_ws_send_text(Mel_Server_Handle h, Mel_Server_Conn_Handle ch, str8 data)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
    if (cn == NULL || !cn->alive || !cn->mg) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (cn->mg->is_closing) return MEL_SERVER_ERR_CONN_CLOSED;
    mg_ws_send(cn->mg, data.data, (size_t)data.len, WEBSOCKET_OP_TEXT);
    return MEL_SERVER_OK;
}

i32 mel_server_ws_send_textf(Mel_Server_Handle h, Mel_Server_Conn_Handle ch, const char* fmt, ...)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
    if (cn == NULL || !cn->alive || !cn->mg) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (cn->mg->is_closing) return MEL_SERVER_ERR_CONN_CLOSED;
    va_list ap; va_start(ap, fmt);
    mg_ws_vprintf(cn->mg, WEBSOCKET_OP_TEXT, fmt, &ap);
    va_end(ap);
    return MEL_SERVER_OK;
}

i32 mel_server_ws_send_bytes(Mel_Server_Handle h, Mel_Server_Conn_Handle ch, const void* p, usize n)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
    if (cn == NULL || !cn->alive || !cn->mg) return MEL_SERVER_ERR_INVALID_HANDLE;
    if (cn->mg->is_closing) return MEL_SERVER_ERR_CONN_CLOSED;
    mg_ws_send(cn->mg, p, n, WEBSOCKET_OP_BINARY);
    return MEL_SERVER_OK;
}

i32 mel_server_ws_broadcast_text(Mel_Server_Handle h, const char* path, str8 data)
{
    Mel_Server* s = mel__server_resolve(h);
    if (s == NULL) return MEL_SERVER_ERR_INVALID_HANDLE;
    str8 want = path ? mel__cstr(path) : (str8){0};
    for (u32 i = 0; i < s->conns.slot_count; i++) {
        Mel_SlotMap_Handle ch = mel_slotmap_handle_make(i, s->conns.slots[i].generation);
        Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
        if (cn == NULL || !cn->alive || !cn->mg) continue;
        if (cn->kind != MEL__SERVER_CONN_KIND_WS && cn->kind != MEL__SERVER_CONN_KIND_PUBSUB && cn->kind != MEL__SERVER_CONN_KIND_RPC) continue;
        if (cn->mg->is_closing) continue;
        if (path && !str8_equals(cn->ws_path, want)) continue;
        mg_ws_send(cn->mg, data.data, (size_t)data.len, WEBSOCKET_OP_TEXT);
    }
    return MEL_SERVER_OK;
}

i32 mel_server_ws_broadcast_textf(Mel_Server_Handle h, const char* path, const char* fmt, ...)
{
    char buf[16384];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return MEL_SERVER_ERR_INVALID_ARGUMENT;
    if ((usize)n > sizeof(buf)) n = (int)sizeof(buf);
    return mel_server_ws_broadcast_text(h, path, (str8){ .data = (u8*)buf, .len = (size)n });
}

/* ============================================================
 * Request accessors
 * ============================================================ */

str8 mel_server_req_method(Mel_Server_Req* req) { return mel__from_mg(req->hm->method); }
str8 mel_server_req_uri   (Mel_Server_Req* req) { return mel__from_mg(req->hm->uri); }
str8 mel_server_req_query (Mel_Server_Req* req) { return mel__from_mg(req->hm->query); }
str8 mel_server_req_body  (Mel_Server_Req* req) { return mel__from_mg(req->hm->body); }
Mel_Server_Handle mel_server_req_server(Mel_Server_Req* req) { return req->server->self; }

bool mel_server_req_header(Mel_Server_Req* req, const char* name, str8* out)
{
    struct mg_str* h = mg_http_get_header(req->hm, name);
    if (h == NULL) return false;
    if (out) *out = mel__from_mg(*h);
    return true;
}

bool mel_server_req_query_var(Mel_Server_Req* req, const char* name, char* buf, usize cap)
{
    int r = mg_http_get_var(&req->hm->query, name, buf, cap);
    return r > 0;
}

bool mel_server_req_form_var(Mel_Server_Req* req, const char* name, char* buf, usize cap)
{
    int r = mg_http_get_var(&req->hm->body, name, buf, cap);
    return r > 0;
}

bool mel_server_req_capture(Mel_Server_Req* req, u32 index, str8* out)
{
    if (index >= req->captures.count) return false;
    if (out) *out = req->captures.items[index];
    return true;
}

bool mel_server_req_param(Mel_Server_Req* req, const char* name, str8* out)
{
    if (req->route == NULL) return false;
    str8 want = mel__cstr(name);
    for (usize i = 0; i < req->route->param_names.count && i < req->captures.count; i++) {
        if (str8_equals(req->route->param_names.items[i], want)) {
            if (out) *out = req->captures.items[i];
            return true;
        }
    }
    return false;
}

static u64 mel__server_hash_cstr(const void* k)  { return mel_hashmap_hash_str(k); }
static bool mel__server_eq_cstr(const void* a, const void* b) { return strcmp((const char*)a, (const char*)b) == 0; }

void* mel_server_req_user_data_get(Mel_Server_Req* req, const char* key)
{
    if (!req->user_data_initialized) return NULL;
    return mel_hashmap_get(&req->user_data, (void*)key);
}

void mel_server_req_user_data_set(Mel_Server_Req* req, const char* key, void* value)
{
    if (!req->user_data_initialized) {
        mel_hashmap_init(&req->user_data, mel__server_hash_cstr, mel__server_eq_cstr, g_module_alloc);
        req->user_data_initialized = true;
    }
    mel_hashmap_put(&req->user_data, (void*)key, value);
}

/* ============================================================
 * Reply
 * ============================================================ */

void mel_server_reply(Mel_Server_Req* req, i32 status, const char* headers, const char* fmt, ...)
{
    char body[8192];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((usize)n > sizeof(body)) n = (int)sizeof(body);
    mg_http_reply(req->mg_conn, status, headers ? headers : "", "%.*s", n, body);
    req->responded = true;
}

void mel_server_reply_text(Mel_Server_Req* req, i32 status, str8 body)
{
    mg_http_reply(req->mg_conn, status, "Content-Type: text/plain\r\n",
                  "%.*s", (int)body.len, (const char*)body.data);
    req->responded = true;
}

void mel_server_reply_json(Mel_Server_Req* req, i32 status, const char* fmt, ...)
{
    char body[8192];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((usize)n > sizeof(body)) n = (int)sizeof(body);
    mg_http_reply(req->mg_conn, status, "Content-Type: application/json\r\n",
                  "%.*s", n, body);
    req->responded = true;
}

void mel_server_reply_bytes(Mel_Server_Req* req, i32 status, const void* p, usize n, const char* content_type)
{
    char hdr[256];
    snprintf(hdr, sizeof(hdr), "Content-Type: %s\r\n", content_type ? content_type : "application/octet-stream");
    mg_printf(req->mg_conn,
              "HTTP/1.1 %d \r\n%sContent-Length: %lu\r\n\r\n",
              status, hdr, (unsigned long)n);
    mg_send(req->mg_conn, p, n);
    req->responded = true;
}

void mel_server_reply_status(Mel_Server_Req* req, i32 status)
{
    mg_http_reply(req->mg_conn, status, "", "");
    req->responded = true;
}

void mel_server_reply_file(Mel_Server_Req* req, const char* fs_path)
{
    struct mg_http_serve_opts opts = {0};
    mg_http_serve_file(req->mg_conn, req->hm, fs_path, &opts);
    req->responded = true;
}

void mel_server_redirect(Mel_Server_Req* req, i32 status, const char* location)
{
    mg_http_reply(req->mg_conn, status,
                  "Content-Type: text/plain\r\n",
                  "Location: %s\r\n", location);
    char hdr[1024];
    int hl = snprintf(hdr, sizeof(hdr), "Location: %s\r\n", location);
    (void)hl;
    req->responded = true;
}

void mel_server_stream_begin(Mel_Server_Req* req, i32 status, const char* headers)
{
    mg_printf(req->mg_conn,
              "HTTP/1.1 %d \r\n%sTransfer-Encoding: chunked\r\n\r\n",
              status, headers ? headers : "");
    req->responded = true;
}

void mel_server_stream_write(Mel_Server_Req* req, const void* p, usize n)
{
    mg_http_write_chunk(req->mg_conn, (const char*)p, n);
}

void mel_server_stream_printf(Mel_Server_Req* req, const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    char buf[4096];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((usize)n > sizeof(buf)) n = (int)sizeof(buf);
    mg_http_write_chunk(req->mg_conn, buf, (size_t)n);
}

void mel_server_stream_end(Mel_Server_Req* req)
{
    mg_http_write_chunk(req->mg_conn, "", 0);
}

/* ============================================================
 * Conn lifecycle helpers
 * ============================================================ */

static Mel__Server_Conn* mel__server_conn_get_or_alloc(Mel_Server* s, struct mg_connection* c, Mel__Server_Route_Slot* route)
{
    Mel_SlotMap_Handle existing = *(Mel_SlotMap_Handle*)c->data;
    Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, existing);
    if (cn != NULL && cn->alive && cn->mg == c) return cn;

    Mel__Server_Conn z = {0};
    z.alive = true;
    z.mg = c;
    z.kind = MEL__SERVER_CONN_KIND_HTTP;
    if (route) {
        z.ws_on_open = route->ws_on_open;
        z.ws_on_message = route->ws_on_message;
        z.ws_on_close = route->ws_on_close;
        z.route_user_data = route->user_data;
        z.ws_path = mel__str8_dup(g_module_alloc, route->pattern);
    }
    mel_array_init(&z.subbed_topics, g_module_alloc);
    Mel_Server_Conn_Handle ch = mel_slotmap_insert(&s->conns, &z);
    Mel__Server_Conn* nc = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
    if (nc) {
        nc->generation = ch.generation;
        nc->self = ch;
    }
    *(Mel_SlotMap_Handle*)c->data = ch;
    return nc;
}

static void mel__server_conn_destroy(Mel_Server* s, struct mg_connection* c)
{
    Mel_SlotMap_Handle ch = *(Mel_SlotMap_Handle*)c->data;
    Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
    if (cn == NULL || !cn->alive) return;
    if (cn->ws_on_close) cn->ws_on_close(s->self, ch);
    for (usize i = 0; i < cn->subbed_topics.count; i++) {
        mel__str8_free(g_module_alloc, &cn->subbed_topics.items[i]);
    }
    mel_array_free(&cn->subbed_topics);
    mel__str8_free(g_module_alloc, &cn->ws_path);
    cn->alive = false;
    mel_slotmap_remove(&s->conns, ch);
}

/* ============================================================
 * Route matching + dispatch
 * ============================================================ */

static bool mel__server_method_matches(str8 want, str8 actual)
{
    if (want.len == 1 && want.data[0] == '*') return true;
    return str8_ieq(want, actual);
}

static Mel__Server_Route_Slot* mel__server_match_route(Mel_Server* s,
                                                      struct mg_http_message* hm,
                                                      Mel__Server_Str8_Array* out_caps,
                                                      bool want_ws)
{
    str8 method = mel__from_mg(hm->method);
    str8 uri = mel__from_mg(hm->uri);

    for (u32 i = 0; i < s->routes.slot_count; i++) {
        Mel_SlotMap_Handle rh = mel_slotmap_handle_make(i, s->routes.slots[i].generation);
        Mel__Server_Route_Slot* r = (Mel__Server_Route_Slot*)mel_slotmap_get(&s->routes, rh);
        if (r == NULL || !r->alive) continue;
        bool is_ws = (r->kind == MEL__SERVER_ROUTE_KIND_WS);
        if (is_ws != want_ws) continue;
        if (!is_ws && !mel__server_method_matches(r->method, method)) continue;
        if (is_ws && !str8_ieq(r->method, method)) continue;

        struct mg_str caps_buf[8] = {0};
        if (!mg_match(mel__to_mg(uri), mel__to_mg(r->pattern), caps_buf)) continue;

        if (out_caps) {
            mel_array_clear(out_caps);
            for (usize k = 0; k < r->param_names.count && k < 8; k++) {
                str8 cap = mel__from_mg(caps_buf[k]);
                mel_array_push(out_caps, cap);
            }
        }
        return r;
    }
    return NULL;
}

static void mel__server_handle_http(Mel_Server* s, struct mg_connection* c, struct mg_http_message* hm)
{
    Mel_Server_Req req = {0};
    req.server = s;
    req.mg_conn = c;
    req.hm = hm;
    mel_array_init(&req.captures, g_module_alloc);

    Mel__Server_Route_Slot* r = mel__server_match_route(s, hm, &req.captures, false);

    if (r == NULL) {
        mg_http_reply(c, 404, "Content-Type: text/plain\r\n", "not found\n");
        goto cleanup;
    }
    req.route = r;

    for (usize i = 0; i < s->middleware.count; i++) {
        Mel_Server_MW_Result mr = s->middleware.items[i](&req);
        if (mr == MEL_SERVER_MW_STOP) goto cleanup;
        if (req.responded) goto cleanup;
    }

    if (r->kind == MEL__SERVER_ROUTE_KIND_REST) {
        r->rest_handler(&req);
        if (!req.responded) {
            mg_http_reply(c, 204, "", "");
        }
    } else if (r->kind == MEL__SERVER_ROUTE_KIND_STATIC_DIR) {
        struct mg_http_serve_opts opts = {0};
        opts.root_dir = (const char*)r->static_fs_path.data;
        if (r->static_extra_headers.len > 0) {
            opts.extra_headers = (const char*)r->static_extra_headers.data;
        }
        mg_http_serve_dir(c, hm, &opts);
    } else if (r->kind == MEL__SERVER_ROUTE_KIND_STATIC_FILE) {
        struct mg_http_serve_opts opts = {0};
        if (r->static_extra_headers.len > 0) {
            opts.extra_headers = (const char*)r->static_extra_headers.data;
        }
        mg_http_serve_file(c, hm, (const char*)r->static_fs_path.data, &opts);
    }

cleanup:
    if (req.user_data_initialized) mel_hashmap_free(&req.user_data);
    mel_array_free(&req.captures);
}

static void mel__server_handle_ws_upgrade(Mel_Server* s, struct mg_connection* c, struct mg_http_message* hm)
{
    Mel__Server_Str8_Array caps;
    mel_array_init(&caps, g_module_alloc);

    Mel__Server_Route_Slot* r = mel__server_match_route(s, hm, &caps, true);
    if (r == NULL) {
        mg_http_reply(c, 404, "", "ws path not found\n");
        mel_array_free(&caps);
        return;
    }

    mg_ws_upgrade(c, hm, NULL);

    Mel__Server_Conn* cn = mel__server_conn_get_or_alloc(s, c, r);
    if (cn) {
        cn->kind = MEL__SERVER_CONN_KIND_WS;
        if (s->rpc_mounted && str8_equals(cn->ws_path, s->rpc_ws_path)) {
            cn->kind = MEL__SERVER_CONN_KIND_RPC;
        } else if (s->pubsub_mounted && str8_equals(cn->ws_path, s->pubsub_ws_path)) {
            cn->kind = MEL__SERVER_CONN_KIND_PUBSUB;
        }
    }

    mel_array_free(&caps);
}

/* ============================================================
 * Main mongoose ev handler
 * ============================================================ */

static void mel__server_ev(struct mg_connection* c, int ev, void* ev_data)
{
    Mel_Server* s = (Mel_Server*)c->mgr->userdata;
    if (s == NULL) return;

    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;
        struct mg_str* upgrade = mg_http_get_header(hm, "Upgrade");
        if (upgrade && upgrade->len == 9 && memcmp(upgrade->buf, "websocket", 9) == 0) {
            mel__server_handle_ws_upgrade(s, c, hm);
        } else {
            mel__server_handle_http(s, c, hm);
        }
    } else if (ev == MG_EV_WS_OPEN) {
        Mel_SlotMap_Handle ch = *(Mel_SlotMap_Handle*)c->data;
        Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
        if (cn && cn->ws_on_open) cn->ws_on_open(s->self, ch);
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message* wm = (struct mg_ws_message*)ev_data;
        Mel_SlotMap_Handle ch = *(Mel_SlotMap_Handle*)c->data;
        Mel__Server_Conn* cn = (Mel__Server_Conn*)mel_slotmap_get(&s->conns, ch);
        if (cn && cn->ws_on_message) {
            bool is_bin = (wm->flags & 15) == WEBSOCKET_OP_BINARY;
            cn->ws_on_message(s->self, ch, mel__from_mg(wm->data), is_bin);
        }
    } else if (ev == MG_EV_CLOSE) {
        if (c->is_websocket) mel__server_conn_destroy(s, c);
    } else if (ev == MG_EV_WAKEUP) {
        mel__server_drain_xt_queue(s);
    } else if (ev == MG_EV_ACCEPT) {
#if MEL_SERVER_TLS
        if (s->tls_enabled) {
            struct mg_tls_opts opts = {
                .cert = mel__to_mg(s->tls_cert_pem),
                .key  = mel__to_mg(s->tls_key_pem),
                .ca   = s->tls_ca_pem.len > 0 ? mel__to_mg(s->tls_ca_pem) : (struct mg_str){0},
                .skip_verification = s->tls_skip_verification,
            };
            mg_tls_init(c, &opts);
        }
#endif
    }
}
