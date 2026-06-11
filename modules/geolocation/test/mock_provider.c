#include "mock_provider.h"

#include <string/str8.h>

#include <string.h>

typedef struct
{
    const Mel_Geo_Provider_Sink* sink;
    bool                         attached;
    bool                         available;
    Mel_Geo_Caps                 caps;
    const mel_geo_auth*          auth;
    const mel_geo_result*        start_result;
    Mel_Geo_Fix                  last_known;
    bool                         has_last_known;

    bool           streaming;
    Mel_Geo_Demand demand;
    u32            starts, updates, stops;

    Mel_Geo_Request* pending_request;
    u32              request_calls, request_cancels;

    Mel_Geo_Geocode* pending_geocode;
} Mock_Geo;

static Mock_Geo g_mock;

static bool mock_available(void* user)
{
    (void)user;
    return g_mock.available;
}

static void mock_attach(void* user, Mel_Vat* vat, const Mel_Geo_Provider_Sink* sink)
{
    (void)user;
    (void)vat;
    g_mock.sink = sink;
    g_mock.attached = true;
}

static void mock_detach(void* user)
{
    (void)user;
    g_mock.attached = false;
}

static Mel_Geo_Caps mock_caps(void* user)
{
    (void)user;
    return g_mock.caps;
}

static const mel_geo_auth* mock_authorization(void* user)
{
    (void)user;
    return g_mock.auth;
}

static void mock_authorize(void* user, const mel_geo_scope* scope, Mel_Future* future)
{
    (void)user;
    (void)scope;
    g_mock.sink->on_auth(future, g_mock.auth);
}

static const mel_geo_result* mock_last_known(void* user, Mel_Geo_Fix* out)
{
    (void)user;
    if (!g_mock.has_last_known)
        return &mel_geo_unavailable;
    *out = g_mock.last_known;
    return &mel_geo_ok;
}

static void mock_request(void* user, Mel_Geo_Request* req)
{
    (void)user;
    g_mock.request_calls++;
    g_mock.pending_request = req;
}

static void mock_request_cancel(void* user, Mel_Geo_Request* req)
{
    (void)user;
    if (g_mock.pending_request == req)
        g_mock.pending_request = NULL;
    g_mock.request_cancels++;
}

static const mel_geo_result* mock_stream_start(void* user, const Mel_Geo_Demand* d)
{
    (void)user;
    if (!mel_geo_result_ok(g_mock.start_result))
        return g_mock.start_result;
    g_mock.streaming = true;
    g_mock.demand = *d;
    g_mock.starts++;
    return &mel_geo_ok;
}

static void mock_stream_update(void* user, const Mel_Geo_Demand* d)
{
    (void)user;
    g_mock.demand = *d;
    g_mock.updates++;
}

static void mock_stream_stop(void* user)
{
    (void)user;
    g_mock.streaming = false;
    g_mock.stops++;
}

static const mel_geo_result* mock_heading_start(void* user)
{
    (void)user;
    return &mel_geo_ok;
}

static void mock_heading_stop(void* user) { (void)user; }

static void mock_geocode_forward(void* user, Mel_Geo_Geocode* g)
{
    (void)user;
    g_mock.pending_geocode = g;
}

static void mock_geocode_reverse(void* user, Mel_Geo_Geocode* g)
{
    (void)user;
    g_mock.pending_geocode = g;
}

static Mel_Geo_Provider_Node g_mock_node;

void mock_geo_install(void)
{
    memset(&g_mock, 0, sizeof g_mock);
    g_mock.available = true;
    g_mock.auth = &mel_geo_auth_granted_in_use;
    g_mock.start_result = &mel_geo_ok;
    g_mock.caps = (Mel_Geo_Caps){ .fixes = true, .heading = true, .geocoding = true };

    memset(&g_mock_node, 0, sizeof g_mock_node);
    g_mock_node.desc = (Mel_Geo_Provider_Desc){
        .name = "mock",
        .available = mock_available,
        .attach = mock_attach,
        .detach = mock_detach,
        .caps = mock_caps,
        .authorization = mock_authorization,
        .authorize = mock_authorize,
        .last_known = mock_last_known,
        .request = mock_request,
        .request_cancel = mock_request_cancel,
        .stream_start = mock_stream_start,
        .stream_update = mock_stream_update,
        .stream_stop = mock_stream_stop,
        .heading_start = mock_heading_start,
        .heading_stop = mock_heading_stop,
        .geocode_forward = mock_geocode_forward,
        .geocode_reverse = mock_geocode_reverse,
    };
    mel_geo_provider_register(&g_mock_node);
}

void mock_geo_uninstall(void) { mel_geo_provider_unregister(&g_mock_node); }

void mock_geo_set_available(bool v) { g_mock.available = v; }
void mock_geo_set_caps(Mel_Geo_Caps caps) { g_mock.caps = caps; }
void mock_geo_set_auth(const mel_geo_auth* a) { g_mock.auth = a; }
void mock_geo_set_start_result(const mel_geo_result* r) { g_mock.start_result = r; }

void mock_geo_set_last_known(const Mel_Geo_Fix* fix)
{
    g_mock.last_known = *fix;
    g_mock.has_last_known = true;
}

void mock_geo_push_fix(const Mel_Geo_Fix* fix) { g_mock.sink->on_fix(fix); }
void mock_geo_push_stream_result(const mel_geo_result* r) { g_mock.sink->on_stream_result(r); }
void mock_geo_push_heading(const Mel_Geo_Heading* h) { g_mock.sink->on_heading(h); }

Mel_Geo_Request* mock_geo_pending_request(void) { return g_mock.pending_request; }

void mock_geo_resolve_request(const Mel_Geo_Fix* fix, const mel_geo_result* r)
{
    Mel_Geo_Request* req = g_mock.pending_request;
    g_mock.pending_request = NULL;
    g_mock.sink->on_request(req, fix, r);
}

Mel_Geo_Geocode* mock_geo_pending_geocode(void) { return g_mock.pending_geocode; }

void mock_geo_fill_geocode(u32 count)
{
    Mel_Geo_Geocode* g = g_mock.pending_geocode;
    g_mock.pending_geocode = NULL;
    if (!mel_geo_provider_geocode_claim(g))
        return;
    for (u32 i = 0; i < count; i++)
    {
        Mel_Geo_Place pl = {
            .name = str8_dup_alloc(S8("Mock Place"), g->alloc),
            .locality = str8_dup_alloc(S8("Mockville"), g->alloc),
            .country = str8_dup_alloc(S8("Mockland"), g->alloc),
            .country_code = str8_dup_alloc(S8("MK"), g->alloc),
            .latitude_deg = 10.0 + (f64)i,
            .longitude_deg = 20.0 + (f64)i,
            .valid = MEL_GEO_VALID_POSITION,
        };
        mel_array_push(&g->places, pl);
    }
    g_mock.sink->on_geocode(g, &mel_geo_ok);
}

u32            mock_geo_stream_starts(void) { return g_mock.starts; }
u32            mock_geo_stream_updates(void) { return g_mock.updates; }
u32            mock_geo_stream_stops(void) { return g_mock.stops; }
u32            mock_geo_request_calls(void) { return g_mock.request_calls; }
u32            mock_geo_request_cancels(void) { return g_mock.request_cancels; }
bool           mock_geo_streaming(void) { return g_mock.streaming; }
Mel_Geo_Demand mock_geo_demand(void) { return g_mock.demand; }
