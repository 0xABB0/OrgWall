#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "wasm/emscripten-only translation unit"
#endif

#include <geolocation/provider.h>

#include <debug/assert.h>
#include <log/log.h>
#include <time/nano.h>

#include <emscripten.h>

static const Mel_Geo_Provider_Sink* g_sink;
static Mel_Geo_Request*             g_pending;
static Mel_Future*                  g_auth_future;
static const mel_geo_auth*          g_auth_state = &mel_geo_auth_not_determined;
static Mel_Geo_Fix                  g_last_fix;
static bool                         g_have_last;
static bool                         g_streaming;

EM_JS(int, mel_geo_wasm_supported, (), {
    return (typeof navigator !== 'undefined' && navigator.geolocation && self.isSecureContext) ? 1 : 0;
});

EM_JS(void, mel_geo_wasm_watch_start, (double accuracyM), {
    var opts = { enableHighAccuracy: accuracyM <= 100.0, maximumAge: 0 };
    Module._melGeoWatch = navigator.geolocation.watchPosition(
        function(p) {
            var c = p.coords;
            Module._mel_geo_wasm_on_fix(1, 0,
                c.latitude, c.longitude,
                c.altitude === null ? 0 : c.altitude, c.altitude === null ? 0 : 1,
                c.accuracy,
                c.altitudeAccuracy === null ? 0 : c.altitudeAccuracy, c.altitudeAccuracy === null ? 0 : 1,
                c.speed === null ? 0 : c.speed, c.speed === null ? 0 : 1,
                c.heading === null || isNaN(c.heading) ? 0 : c.heading, c.heading === null || isNaN(c.heading) ? 0 : 1,
                p.timestamp);
        },
        function(e) {
            Module._mel_geo_wasm_on_error(1, 0, e.code === 1 ? 1 : 2);
        },
        opts);
});

EM_JS(void, mel_geo_wasm_watch_stop, (), {
    if (Module._melGeoWatch !== undefined)
    {
        navigator.geolocation.clearWatch(Module._melGeoWatch);
        Module._melGeoWatch = undefined;
    }
});

EM_JS(void, mel_geo_wasm_request, (double reqPtr, double accuracyM, double maxAgeMs), {
    var opts = { enableHighAccuracy: accuracyM <= 100.0, maximumAge: maxAgeMs };
    navigator.geolocation.getCurrentPosition(
        function(p) {
            var c = p.coords;
            Module._mel_geo_wasm_on_fix(0, reqPtr,
                c.latitude, c.longitude,
                c.altitude === null ? 0 : c.altitude, c.altitude === null ? 0 : 1,
                c.accuracy,
                c.altitudeAccuracy === null ? 0 : c.altitudeAccuracy, c.altitudeAccuracy === null ? 0 : 1,
                c.speed === null ? 0 : c.speed, c.speed === null ? 0 : 1,
                c.heading === null || isNaN(c.heading) ? 0 : c.heading, c.heading === null || isNaN(c.heading) ? 0 : 1,
                p.timestamp);
        },
        function(e) {
            Module._mel_geo_wasm_on_error(0, reqPtr, e.code === 1 ? 1 : 2);
        },
        opts);
});

EM_JS(void, mel_geo_wasm_query_permission, (), {
    if (typeof navigator === 'undefined' || !navigator.permissions || !navigator.permissions.query)
        return;
    navigator.permissions.query({ name: 'geolocation' }).then(function(st) {
        var code = st.state === 'granted' ? 1 : st.state === 'denied' ? 2 : 0;
        Module._mel_geo_wasm_on_permission(code, 0);
        st.onchange = function() {
            var c = st.state === 'granted' ? 1 : st.state === 'denied' ? 2 : 0;
            Module._mel_geo_wasm_on_permission(c, 0);
        };
    }).catch(function() {});
});

EM_JS(void, mel_geo_wasm_force_prompt, (), {
    navigator.geolocation.getCurrentPosition(
        function(p) { Module._mel_geo_wasm_on_permission(1, 1); },
        function(e) { Module._mel_geo_wasm_on_permission(e.code === 1 ? 2 : 0, 1); },
        { maximumAge: 3600000, timeout: 600000 });
});

static Mel_Geo_Fix geo_wasm__fix(double lat, double lon, double alt, int has_alt, double hacc, double vacc, int has_vacc,
                                 double speed, int has_speed, double heading, int has_heading, double utc_ms)
{
    Mel_Geo_Fix fix = {
        .latitude_deg = lat,
        .longitude_deg = lon,
        .horizontal_accuracy_m = hacc,
        .utc_unix_ms = (u64)utc_ms,
        .monotonic_ns = mel_nanos_since_unspecified_epoch(),
        .valid = MEL_GEO_VALID_POSITION | MEL_GEO_VALID_HACC | MEL_GEO_VALID_UTC | MEL_GEO_VALID_MONOTONIC,
    };
    if (has_alt)
    {
        fix.altitude_m = alt;
        fix.valid |= MEL_GEO_VALID_ALTITUDE;
    }
    if (has_vacc)
    {
        fix.vertical_accuracy_m = vacc;
        fix.valid |= MEL_GEO_VALID_VACC;
    }
    if (has_speed)
    {
        fix.speed_mps = speed;
        fix.valid |= MEL_GEO_VALID_SPEED;
    }
    if (has_heading)
    {
        fix.course_deg = heading;
        fix.valid |= MEL_GEO_VALID_COURSE;
    }
    return fix;
}

static bool geo_wasm__pending_take(Mel_Geo_Request* req)
{
    for (Mel_Geo_Request** pp = &g_pending; *pp != NULL; pp = &(*pp)->provider_next)
        if (*pp == req)
        {
            *pp = req->provider_next;
            req->provider_next = NULL;
            return true;
        }
    return false;
}

EMSCRIPTEN_KEEPALIVE void mel_geo_wasm_on_fix(int stream, double req_ptr, double lat, double lon, double alt, int has_alt,
                                              double hacc, double vacc, int has_vacc, double speed, int has_speed,
                                              double heading, int has_heading, double utc_ms)
{
    Mel_Geo_Fix fix = geo_wasm__fix(lat, lon, alt, has_alt, hacc, vacc, has_vacc, speed, has_speed, heading, has_heading, utc_ms);
    g_last_fix = fix;
    g_have_last = true;
    g_auth_state = &mel_geo_auth_granted_in_use;
    if (stream)
    {
        if (g_streaming)
            g_sink->on_fix(&fix);
        return;
    }
    Mel_Geo_Request* req = (Mel_Geo_Request*)(uintptr_t)req_ptr;
    if (geo_wasm__pending_take(req))
        g_sink->on_request(req, &fix, &mel_geo_ok);
}

EMSCRIPTEN_KEEPALIVE void mel_geo_wasm_on_error(int stream, double req_ptr, int code)
{
    const mel_geo_result* r = code == 1 ? &mel_geo_denied : &mel_geo_unavailable;
    if (code == 1)
        g_auth_state = &mel_geo_auth_denied;
    if (stream)
    {
        if (g_streaming)
            g_sink->on_stream_result(r);
        return;
    }
    Mel_Geo_Request* req = (Mel_Geo_Request*)(uintptr_t)req_ptr;
    if (geo_wasm__pending_take(req))
        g_sink->on_request(req, NULL, r);
}

EMSCRIPTEN_KEEPALIVE void mel_geo_wasm_on_permission(int code, int from_prompt)
{
    const mel_geo_auth* auth = code == 1 ? &mel_geo_auth_granted_in_use
                             : code == 2 ? &mel_geo_auth_denied
                                         : &mel_geo_auth_not_determined;
    g_auth_state = auth;
    if (g_auth_future == NULL)
        return;
    if (code == 0 && !from_prompt)
    {
        mel_geo_wasm_force_prompt();
        return;
    }
    Mel_Future* f = g_auth_future;
    g_auth_future = NULL;
    g_sink->on_auth(f, auth);
}

static bool geo_wasm_available(void* user)
{
    (void)user;
    return mel_geo_wasm_supported() != 0;
}

static void geo_wasm_attach(void* user, Mel_Vat* vat, const Mel_Geo_Provider_Sink* sink)
{
    (void)user;
    (void)vat;
    g_sink = sink;
    mel_geo_wasm_query_permission();
}

static void geo_wasm_detach(void* user)
{
    (void)user;
    if (g_streaming)
        mel_geo_wasm_watch_stop();
    g_streaming = false;
    g_sink = NULL;
    g_pending = NULL;
    g_auth_future = NULL;
}

static Mel_Geo_Caps geo_wasm_caps(void* user)
{
    (void)user;
    return (Mel_Geo_Caps){ .fixes = true };
}

static const mel_geo_auth* geo_wasm_authorization(void* user)
{
    (void)user;
    return g_auth_state;
}

static void geo_wasm_authorize(void* user, const mel_geo_scope* scope, Mel_Future* future)
{
    (void)user;
    (void)scope;
    if (g_auth_state != &mel_geo_auth_not_determined)
    {
        g_sink->on_auth(future, g_auth_state);
        return;
    }
    mel_assert_msg("an authorize is already pending", g_auth_future == NULL);
    g_auth_future = future;
    mel_geo_wasm_force_prompt();
}

static const mel_geo_result* geo_wasm_last_known(void* user, Mel_Geo_Fix* out)
{
    (void)user;
    if (!g_have_last)
        return &mel_geo_unavailable;
    *out = g_last_fix;
    return &mel_geo_ok;
}

static void geo_wasm_request(void* user, Mel_Geo_Request* req)
{
    (void)user;
    req->provider_next = g_pending;
    g_pending = req;
    double max_age_ms = req->max_age_ns > 0 ? (double)(req->max_age_ns / 1000000) : 0.0;
    mel_geo_wasm_request((double)(uintptr_t)req, req->accuracy_m, max_age_ms);
}

static void geo_wasm_request_cancel(void* user, Mel_Geo_Request* req)
{
    (void)user;
    geo_wasm__pending_take(req);
}

static const mel_geo_result* geo_wasm_stream_start(void* user, const Mel_Geo_Demand* d)
{
    (void)user;
    mel_geo_wasm_watch_start(d->accuracy_m);
    g_streaming = true;
    return &mel_geo_ok;
}

static void geo_wasm_stream_update(void* user, const Mel_Geo_Demand* d)
{
    (void)user;
    mel_geo_wasm_watch_stop();
    mel_geo_wasm_watch_start(d->accuracy_m);
}

static void geo_wasm_stream_stop(void* user)
{
    (void)user;
    mel_geo_wasm_watch_stop();
    g_streaming = false;
}

void mel_geo__register_host_providers(void)
{
    static Mel_Geo_Provider_Node node;
    node.desc = (Mel_Geo_Provider_Desc){
        .name = "web-geolocation",
        .available = geo_wasm_available,
        .attach = geo_wasm_attach,
        .detach = geo_wasm_detach,
        .caps = geo_wasm_caps,
        .authorization = geo_wasm_authorization,
        .authorize = geo_wasm_authorize,
        .last_known = geo_wasm_last_known,
        .request = geo_wasm_request,
        .request_cancel = geo_wasm_request_cancel,
        .stream_start = geo_wasm_stream_start,
        .stream_update = geo_wasm_stream_update,
        .stream_stop = geo_wasm_stream_stop,
    };
    mel_geo_provider_register(&node);
}
