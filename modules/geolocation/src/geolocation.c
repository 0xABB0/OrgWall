#include <geolocation/geolocation.h>
#include <geolocation/provider.h>

#include <allocator/allocator.h>
#include <collection/list.h>
#include <debug/assert.h>
#include <log/log.h>
#include <time/nano.h>
#include <vat/vat.h>

#include <math.h>
#include <string.h>

struct mel_geo_auth
{
    const char* name;
    bool        granted;
};

const mel_geo_auth mel_geo_auth_granted_always = { "granted-always", true };
const mel_geo_auth mel_geo_auth_granted_in_use = { "granted-in-use", true };
const mel_geo_auth mel_geo_auth_denied = { "denied", false };
const mel_geo_auth mel_geo_auth_restricted = { "restricted", false };
const mel_geo_auth mel_geo_auth_not_determined = { "not-determined", false };

const char* mel_geo_auth_name(const mel_geo_auth* a) { return a != NULL ? a->name : "null"; }
bool        mel_geo_auth_is_granted(const mel_geo_auth* a) { return a != NULL && a->granted; }

struct mel_geo_scope
{
    const char* name;
};

const mel_geo_scope mel_geo_scope_in_use = { "in-use" };
const mel_geo_scope mel_geo_scope_always = { "always" };

const char* mel_geo_scope_name(const mel_geo_scope* s) { return s != NULL ? s->name : "null"; }

struct mel_geo_result
{
    const char* name;
    bool        ok;
};

const mel_geo_result mel_geo_ok = { "ok", true };
const mel_geo_result mel_geo_denied = { "denied", false };
const mel_geo_result mel_geo_unavailable = { "unavailable", false };
const mel_geo_result mel_geo_timeout = { "timeout", false };
const mel_geo_result mel_geo_cancelled = { "cancelled", false };
const mel_geo_result mel_geo_unsupported = { "unsupported", false };
const mel_geo_result mel_geo_lost = { "lost", false };
const mel_geo_result mel_geo_exhausted = { "exhausted", false };

const char* mel_geo_result_name(const mel_geo_result* r) { return r != NULL ? r->name : "null"; }
bool        mel_geo_result_ok(const mel_geo_result* r) { return r != NULL && r->ok; }

typedef struct
{
    Mel_Vat*               vat;
    Mel_Geo_Provider_Node* providers;
    Mel_Geo_Provider_Node* active;

    Mel_Geo_Watch*         watches;
    Mel_Geo_Heading_Watch* heading_watches;
    Mel_Geo_Region*        regions;
    Mel_Geo_Request*       requests;
    Mel_Geo_Geocode*       geocodes;

    Mel_Vat_Source* timeouts;
    Mel_Task        pump;

    Mel_Geo_Demand demand;
    bool           streaming;
    bool           heading_streaming;

    _Atomic(u32) fix_seq;
    Mel_Geo_Fix  fix_slot;
    u32          fix_consumed;

    _Atomic(u32)    heading_seq;
    Mel_Geo_Heading heading_slot;
    u32             heading_consumed;

    _Atomic(const mel_geo_result*) stream_result;
    const mel_geo_result*          last_result;
    _Atomic(u32)                   retry_stream;

    Mel_Geo_Fix latest;
    bool        have_fix;
} Mel_Geo_State;

static Mel_Geo_State g_geo;

#define GEO_EARTH_RADIUS_M 6371008.8
#define GEO_DEG_TO_RAD     0.017453292519943295

f64 mel_geo_distance_m(f64 lat1_deg, f64 lon1_deg, f64 lat2_deg, f64 lon2_deg)
{
    f64 lat1 = lat1_deg * GEO_DEG_TO_RAD;
    f64 lat2 = lat2_deg * GEO_DEG_TO_RAD;
    f64 dlat = (lat2_deg - lat1_deg) * GEO_DEG_TO_RAD;
    f64 dlon = (lon2_deg - lon1_deg) * GEO_DEG_TO_RAD;
    f64 sa = sin(dlat * 0.5);
    f64 sb = sin(dlon * 0.5);
    f64 h = sa * sa + cos(lat1) * cos(lat2) * sb * sb;
    return 2.0 * GEO_EARTH_RADIUS_M * atan2(sqrt(h), sqrt(1.0 - h));
}

static void geo__assert_owner(void)
{
    mel_assert_msg("geolocation api off the home vat owner thread", g_geo.vat != NULL && mel_vat_is_owner(g_geo.vat));
}

static void geo__post_pump(void)
{
    mel_executor_submit(mel_vat_executor(g_geo.vat), &g_geo.pump);
}

static void geo__resolve(Mel_Future* f, const mel_geo_result* r, u32 extra)
{
    u32 sev = mel_geo_result_ok(r) ? MEL_FUTURE_OK : MEL_FUTURE_ERROR;
    mel_future_resolve(f, (void*)(uintptr_t)r, sev | extra);
}

const mel_geo_result* mel_geo_future_result(const Mel_Future* f)
{
    mel_assert(f != NULL && mel_future_resolved(f));
    if (mel_future_status_cancelled(mel_future_status(f)))
        return &mel_geo_cancelled;
    return (const mel_geo_result*)mel_future_value(f);
}

const mel_geo_auth* mel_geo_future_auth(const Mel_Future* f)
{
    mel_assert(f != NULL && mel_future_resolved(f));
    return (const mel_geo_auth*)mel_future_value(f);
}

static void geo__watch_publish(Mel_Geo_Watch* w, const Mel_Geo_Fix* fix, const mel_geo_result* r)
{
    u32 s = atomic_load_explicit(&w->pending_seq, memory_order_relaxed);
    atomic_store_explicit(&w->pending_seq, s + 1, memory_order_release);
    w->pending_fix = fix != NULL ? *fix : (Mel_Geo_Fix){ 0 };
    w->pending_result = r;
    atomic_store_explicit(&w->pending_seq, s + 2, memory_order_release);
    mel_executor_submit(w->exec, &w->task);
}

static void geo__watch_task(Mel_Task* t)
{
    Mel_Geo_Watch* w = mel_container_of(t, Mel_Geo_Watch, task);
    Mel_Geo_Fix           fix;
    const mel_geo_result* r;
    for (;;)
    {
        u32 s0 = atomic_load_explicit(&w->pending_seq, memory_order_acquire);
        if (s0 & 1u)
            continue;
        fix = w->pending_fix;
        r = w->pending_result;
        atomic_thread_fence(memory_order_acquire);
        u32 s1 = atomic_load_explicit(&w->pending_seq, memory_order_acquire);
        if (s0 == s1)
            break;
    }
    if (atomic_load_explicit(&w->live, memory_order_acquire) == 0)
        return;
    w->cb(&fix, r, w->user);
}

static void geo__heading_publish(Mel_Geo_Heading_Watch* w, const Mel_Geo_Heading* h, const mel_geo_result* r)
{
    u32 s = atomic_load_explicit(&w->pending_seq, memory_order_relaxed);
    atomic_store_explicit(&w->pending_seq, s + 1, memory_order_release);
    w->pending_heading = h != NULL ? *h : (Mel_Geo_Heading){ 0 };
    w->pending_result = r;
    atomic_store_explicit(&w->pending_seq, s + 2, memory_order_release);
    mel_executor_submit(w->exec, &w->task);
}

static void geo__heading_task(Mel_Task* t)
{
    Mel_Geo_Heading_Watch* w = mel_container_of(t, Mel_Geo_Heading_Watch, task);
    Mel_Geo_Heading       h;
    const mel_geo_result* r;
    for (;;)
    {
        u32 s0 = atomic_load_explicit(&w->pending_seq, memory_order_acquire);
        if (s0 & 1u)
            continue;
        h = w->pending_heading;
        r = w->pending_result;
        atomic_thread_fence(memory_order_acquire);
        u32 s1 = atomic_load_explicit(&w->pending_seq, memory_order_acquire);
        if (s0 == s1)
            break;
    }
    if (atomic_load_explicit(&w->live, memory_order_acquire) == 0)
        return;
    w->cb(&h, r, w->user);
}

#define GEO_REGION_ENTERED (1u << 0)
#define GEO_REGION_EXITED  (1u << 1)

static void geo__region_publish(Mel_Geo_Region* r, u32 bits, const mel_geo_result* result)
{
    if (bits != 0u)
        atomic_fetch_or_explicit(&r->pending_bits, bits, memory_order_release);
    if (result != NULL)
        atomic_store_explicit(&r->pending_result, result, memory_order_release);
    mel_executor_submit(r->exec, &r->task);
}

static void geo__region_task(Mel_Task* t)
{
    Mel_Geo_Region* r = mel_container_of(t, Mel_Geo_Region, task);
    u32                   bits = atomic_exchange_explicit(&r->pending_bits, 0u, memory_order_acq_rel);
    const mel_geo_result* res = atomic_exchange_explicit(&r->pending_result, NULL, memory_order_acq_rel);
    if (atomic_load_explicit(&r->live, memory_order_acquire) == 0)
        return;
    if (bits == 0u && res == NULL)
        return;
    Mel_Geo_Region_Event ev = {
        .region = r,
        .entered = (bits & GEO_REGION_ENTERED) != 0u,
        .exited = (bits & GEO_REGION_EXITED) != 0u,
        .result = res != NULL ? res : &mel_geo_ok,
    };
    r->cb(&ev, r->user);
}

static i64 geo__region_interval_quantized(void)
{
    static const i64 buckets_s[] = { 1, 2, 5, 10, 30, 60 };
    f64              t = 1.0;
    if (g_geo.have_fix && (g_geo.latest.valid & MEL_GEO_VALID_POSITION))
    {
        f64 speed = (g_geo.latest.valid & MEL_GEO_VALID_SPEED) && g_geo.latest.speed_mps > 1.0 ? g_geo.latest.speed_mps : 1.0;
        f64 nearest = 1e18;
        for (Mel_Geo_Region* r = g_geo.regions; r != NULL; r = r->next)
        {
            if (!r->sw)
                continue;
            f64 d = mel_geo_distance_m(g_geo.latest.latitude_deg, g_geo.latest.longitude_deg, r->latitude_deg, r->longitude_deg);
            f64 boundary = fabs(d - r->radius_m);
            if (boundary < nearest)
                nearest = boundary;
        }
        t = nearest / speed;
    }
    i64 chosen = buckets_s[0];
    for (usize i = 0; i < lengthof(buckets_s); i++)
        if ((f64)buckets_s[i] <= t)
            chosen = buckets_s[i];
    return chosen * 1000000000ll;
}

static const mel_geo_result* geo__recompute_demand(void)
{
    if (g_geo.active == NULL)
        return &mel_geo_unavailable;

    bool           want = false;
    Mel_Geo_Demand d = { .accuracy_m = 1e18, .min_interval_ns = INT64_MAX, .min_distance_m = 1e18 };

    for (Mel_Geo_Watch* w = g_geo.watches; w != NULL; w = w->next)
    {
        want = true;
        if (w->accuracy_m < d.accuracy_m)
            d.accuracy_m = w->accuracy_m;
        if (w->min_interval_ns < d.min_interval_ns)
            d.min_interval_ns = w->min_interval_ns;
        if (w->min_distance_m < d.min_distance_m)
            d.min_distance_m = w->min_distance_m;
    }

    bool sw_regions = false;
    f64  min_radius = 1e18;
    for (Mel_Geo_Region* r = g_geo.regions; r != NULL; r = r->next)
        if (r->sw)
        {
            sw_regions = true;
            if (r->radius_m < min_radius)
                min_radius = r->radius_m;
        }
    if (sw_regions)
    {
        want = true;
        f64 acc = min_radius * 0.5;
        if (acc < 10.0)
            acc = 10.0;
        if (acc > 1000.0)
            acc = 1000.0;
        if (acc < d.accuracy_m)
            d.accuracy_m = acc;
        i64 iv = geo__region_interval_quantized();
        if (iv < d.min_interval_ns)
            d.min_interval_ns = iv;
        d.min_distance_m = 0.0;
    }

    const Mel_Geo_Provider_Desc* p = &g_geo.active->desc;
    if (!want)
    {
        if (g_geo.streaming)
        {
            p->stream_stop(p->user);
            g_geo.streaming = false;
        }
        g_geo.demand = (Mel_Geo_Demand){ 0 };
        return &mel_geo_ok;
    }

    if (!g_geo.streaming)
    {
        const mel_geo_result* r = p->stream_start(p->user, &d);
        if (!mel_geo_result_ok(r))
        {
            mel_log_warn("geo", "stream start failed: %s", mel_geo_result_name(r));
            return r;
        }
        g_geo.streaming = true;
        g_geo.demand = d;
        return &mel_geo_ok;
    }

    if (memcmp(&d, &g_geo.demand, sizeof d) != 0)
    {
        p->stream_update(p->user, &d);
        g_geo.demand = d;
    }
    return &mel_geo_ok;
}

static void geo__request_unlink(Mel_Geo_Request* req)
{
    for (Mel_Geo_Request** pp = &g_geo.requests; *pp != NULL; pp = &(*pp)->next)
        if (*pp == req)
        {
            *pp = req->next;
            break;
        }
    req->next = NULL;
    req->linked = false;
    if (g_geo.timeouts != NULL)
        mel_vat_source_demand_changed(g_geo.timeouts);
}

static void geo__request_finish(Mel_Geo_Request* req, const Mel_Geo_Fix* fix, const mel_geo_result* r, u32 extra)
{
    if (fix != NULL)
        req->fix = *fix;
    if (req->linked)
        geo__request_unlink(req);
    geo__resolve(&req->future, r, extra);
}

static void geo__geocode_unlink(Mel_Geo_Geocode* g)
{
    for (Mel_Geo_Geocode** pp = &g_geo.geocodes; *pp != NULL; pp = &(*pp)->next)
        if (*pp == g)
        {
            *pp = g->next;
            break;
        }
    g->next = NULL;
    g->linked = false;
}

static void geo__fanout_result(const mel_geo_result* r)
{
    for (Mel_Geo_Watch* w = g_geo.watches; w != NULL; w = w->next)
        geo__watch_publish(w, NULL, r);
    for (Mel_Geo_Heading_Watch* w = g_geo.heading_watches; w != NULL; w = w->next)
        geo__heading_publish(w, NULL, r);
}

static void geo__fanout_fix(const Mel_Geo_Fix* fix)
{
    i64 now = (i64)mel_nanos_since_unspecified_epoch();
    for (Mel_Geo_Watch* w = g_geo.watches; w != NULL; w = w->next)
    {
        if (w->min_interval_ns > 0 && w->has_last && now - w->last_delivery_ns < w->min_interval_ns)
            continue;
        if (w->min_distance_m > 0.0 && w->has_last && (fix->valid & MEL_GEO_VALID_POSITION) && (w->last_fix.valid & MEL_GEO_VALID_POSITION))
        {
            f64 d = mel_geo_distance_m(w->last_fix.latitude_deg, w->last_fix.longitude_deg, fix->latitude_deg, fix->longitude_deg);
            if (d < w->min_distance_m)
                continue;
        }
        w->last_fix = *fix;
        w->has_last = true;
        w->last_delivery_ns = now;
        geo__watch_publish(w, fix, &mel_geo_ok);
    }
}

static void geo__regions_eval(const Mel_Geo_Fix* fix)
{
    if (!(fix->valid & MEL_GEO_VALID_POSITION))
        return;
    f64 hacc = (fix->valid & MEL_GEO_VALID_HACC) ? fix->horizontal_accuracy_m : 0.0;
    for (Mel_Geo_Region* r = g_geo.regions; r != NULL; r = r->next)
    {
        if (!r->sw)
            continue;
        f64 d = mel_geo_distance_m(fix->latitude_deg, fix->longitude_deg, r->latitude_deg, r->longitude_deg);
        f64 band = hacc > 0.1 * r->radius_m ? hacc : 0.1 * r->radius_m;
        if (band > 0.5 * r->radius_m)
            band = 0.5 * r->radius_m;
        if (!r->seeded)
        {
            r->inside = d <= r->radius_m;
            r->seeded = true;
            continue;
        }
        if (!r->inside && d <= r->radius_m - band * 0.5)
        {
            r->inside = true;
            if (r->notify_enter)
                geo__region_publish(r, GEO_REGION_ENTERED, NULL);
        }
        else if (r->inside && d >= r->radius_m + band * 0.5)
        {
            r->inside = false;
            if (r->notify_exit)
                geo__region_publish(r, GEO_REGION_EXITED, NULL);
        }
    }
}

static void geo__sweep_requests(void)
{
    Mel_Geo_Request* req = g_geo.requests;
    while (req != NULL)
    {
        Mel_Geo_Request* next = req->next;
        if (atomic_load_explicit(&req->claimed, memory_order_acquire) != 0u && req->done_result != NULL)
            geo__request_finish(req, &req->done_fix, req->done_result, 0);
        req = next;
    }
}

static void geo__sweep_geocodes(void)
{
    Mel_Geo_Geocode* g = g_geo.geocodes;
    while (g != NULL)
    {
        Mel_Geo_Geocode* next = g->next;
        if (atomic_load_explicit(&g->claimed, memory_order_acquire) != 0u && g->done_result != NULL)
        {
            geo__geocode_unlink(g);
            geo__resolve(&g->future, g->done_result, 0);
        }
        g = next;
    }
}

static void geo__resolve_pending_requests(const mel_geo_result* r)
{
    Mel_Geo_Request* req = g_geo.requests;
    while (req != NULL)
    {
        Mel_Geo_Request* next = req->next;
        u32              expected = 0;
        if (atomic_compare_exchange_strong_explicit(&req->claimed, &expected, 1u, memory_order_acq_rel, memory_order_acquire))
        {
            const Mel_Geo_Provider_Desc* p = &g_geo.active->desc;
            if (p->request_cancel != NULL)
                p->request_cancel(p->user, req);
            geo__request_finish(req, g_geo.have_fix ? &g_geo.latest : NULL, r, 0);
        }
        req = next;
    }
}

static void geo__pump(Mel_Task* t)
{
    (void)t;
    if (g_geo.vat == NULL)
        return;

    const mel_geo_result* r = atomic_load_explicit(&g_geo.stream_result, memory_order_acquire);
    if (r != NULL && r != g_geo.last_result)
    {
        g_geo.last_result = r;
        geo__fanout_result(r);
        if (r == &mel_geo_denied || r == &mel_geo_lost)
            geo__resolve_pending_requests(r);
    }

    if (atomic_exchange_explicit(&g_geo.retry_stream, 0u, memory_order_acq_rel) != 0u)
    {
        if (!g_geo.streaming)
            geo__recompute_demand();
        if (!g_geo.heading_streaming && g_geo.heading_watches != NULL && g_geo.active != NULL && g_geo.active->desc.heading_start != NULL)
            if (mel_geo_result_ok(g_geo.active->desc.heading_start(g_geo.active->desc.user)))
                g_geo.heading_streaming = true;
    }

    for (;;)
    {
        u32 s0 = atomic_load_explicit(&g_geo.fix_seq, memory_order_acquire);
        if (s0 & 1u)
            continue;
        if (s0 == g_geo.fix_consumed)
            break;
        Mel_Geo_Fix fix = g_geo.fix_slot;
        atomic_thread_fence(memory_order_acquire);
        u32 s1 = atomic_load_explicit(&g_geo.fix_seq, memory_order_acquire);
        if (s0 != s1)
            continue;
        g_geo.fix_consumed = s0;
        g_geo.latest = fix;
        g_geo.have_fix = true;
        geo__fanout_fix(&fix);
        geo__regions_eval(&fix);
        geo__recompute_demand();
        break;
    }

    for (;;)
    {
        u32 s0 = atomic_load_explicit(&g_geo.heading_seq, memory_order_acquire);
        if (s0 & 1u)
            continue;
        if (s0 == g_geo.heading_consumed)
            break;
        Mel_Geo_Heading h = g_geo.heading_slot;
        atomic_thread_fence(memory_order_acquire);
        u32 s1 = atomic_load_explicit(&g_geo.heading_seq, memory_order_acquire);
        if (s0 != s1)
            continue;
        g_geo.heading_consumed = s0;
        for (Mel_Geo_Heading_Watch* w = g_geo.heading_watches; w != NULL; w = w->next)
            geo__heading_publish(w, &h, &mel_geo_ok);
        break;
    }

    geo__sweep_requests();
    geo__sweep_geocodes();
}

static void geo__sink_on_fix(const Mel_Geo_Fix* fix)
{
    u32 s = atomic_load_explicit(&g_geo.fix_seq, memory_order_relaxed);
    atomic_store_explicit(&g_geo.fix_seq, s + 1, memory_order_release);
    g_geo.fix_slot = *fix;
    atomic_store_explicit(&g_geo.fix_seq, s + 2, memory_order_release);
    geo__post_pump();
}

static void geo__sink_on_stream_result(const mel_geo_result* r)
{
    atomic_store_explicit(&g_geo.stream_result, r, memory_order_release);
    geo__post_pump();
}

static void geo__sink_on_heading(const Mel_Geo_Heading* h)
{
    u32 s = atomic_load_explicit(&g_geo.heading_seq, memory_order_relaxed);
    atomic_store_explicit(&g_geo.heading_seq, s + 1, memory_order_release);
    g_geo.heading_slot = *h;
    atomic_store_explicit(&g_geo.heading_seq, s + 2, memory_order_release);
    geo__post_pump();
}

static void geo__sink_on_region(Mel_Geo_Region* region, bool entered)
{
    if (entered && !region->notify_enter)
        return;
    if (!entered && !region->notify_exit)
        return;
    geo__region_publish(region, entered ? GEO_REGION_ENTERED : GEO_REGION_EXITED, NULL);
}

static void geo__sink_on_region_result(Mel_Geo_Region* region, const mel_geo_result* r)
{
    geo__region_publish(region, 0u, r);
}

static void geo__sink_on_auth(Mel_Future* future, const mel_geo_auth* auth)
{
    if (future != NULL)
        mel_future_resolve(future, (void*)(uintptr_t)auth, mel_geo_auth_is_granted(auth) ? MEL_FUTURE_OK : MEL_FUTURE_ERROR);
    if (mel_geo_auth_is_granted(auth))
    {
        atomic_store_explicit(&g_geo.retry_stream, 1u, memory_order_release);
        geo__post_pump();
    }
}

static void geo__sink_on_request(Mel_Geo_Request* req, const Mel_Geo_Fix* fix, const mel_geo_result* r)
{
    u32 expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&req->claimed, &expected, 1u, memory_order_acq_rel, memory_order_acquire))
        return;
    req->done_fix = fix != NULL ? *fix : (Mel_Geo_Fix){ 0 };
    req->done_result = r;
    geo__post_pump();
}

static void geo__sink_on_geocode(Mel_Geo_Geocode* g, const mel_geo_result* r)
{
    mel_assert(atomic_load_explicit(&g->claimed, memory_order_acquire) != 0u);
    g->done_result = r;
    geo__post_pump();
}

bool mel_geo_provider_geocode_claim(Mel_Geo_Geocode* g)
{
    u32 expected = 0;
    return atomic_compare_exchange_strong_explicit(&g->claimed, &expected, 1u, memory_order_acq_rel, memory_order_acquire);
}

static const Mel_Geo_Provider_Sink GEO_SINK = {
    .on_fix = geo__sink_on_fix,
    .on_stream_result = geo__sink_on_stream_result,
    .on_heading = geo__sink_on_heading,
    .on_region = geo__sink_on_region,
    .on_region_result = geo__sink_on_region_result,
    .on_auth = geo__sink_on_auth,
    .on_request = geo__sink_on_request,
    .on_geocode = geo__sink_on_geocode,
};

static void geo__activate(void)
{
    for (Mel_Geo_Provider_Node* n = g_geo.providers; n != NULL; n = n->next)
    {
        if (n->desc.available != NULL && !n->desc.available(n->desc.user))
            continue;
        g_geo.active = n;
        if (n->desc.attach != NULL)
            n->desc.attach(n->desc.user, g_geo.vat, &GEO_SINK);
        mel_log_info("geo", "active provider: %s", n->desc.name);
        return;
    }
    mel_log_warn("geo", "no location provider available");
}

void mel_geo_provider_register(Mel_Geo_Provider_Node* node)
{
    mel_assert(node != NULL && node->next == NULL);
    Mel_Geo_Provider_Node** pp = &g_geo.providers;
    while (*pp != NULL)
        pp = &(*pp)->next;
    *pp = node;
    if (g_geo.vat != NULL && g_geo.active == NULL)
        geo__activate();
}

void mel_geo_provider_register_host(Mel_Geo_Provider_Node* node)
{
    node->host = true;
    mel_geo_provider_register(node);
}

void mel_geo_provider_unregister(Mel_Geo_Provider_Node* node)
{
    mel_assert(node != NULL);
    for (Mel_Geo_Provider_Node** pp = &g_geo.providers; *pp != NULL; pp = &(*pp)->next)
        if (*pp == node)
        {
            *pp = node->next;
            break;
        }
    node->next = NULL;
    if (g_geo.active != node)
        return;
    if (node->desc.detach != NULL)
        node->desc.detach(node->desc.user);
    g_geo.active = NULL;
    g_geo.streaming = false;
    g_geo.heading_streaming = false;
    g_geo.last_result = &mel_geo_lost;
    geo__fanout_result(&mel_geo_lost);
    Mel_Geo_Request* req = g_geo.requests;
    while (req != NULL)
    {
        Mel_Geo_Request* next = req->next;
        u32              expected = 0;
        if (atomic_compare_exchange_strong_explicit(&req->claimed, &expected, 1u, memory_order_acq_rel, memory_order_acquire))
            geo__request_finish(req, NULL, &mel_geo_lost, 0);
        req = next;
    }
    Mel_Geo_Geocode* g = g_geo.geocodes;
    while (g != NULL)
    {
        Mel_Geo_Geocode* next = g->next;
        if (mel_geo_provider_geocode_claim(g))
        {
            geo__geocode_unlink(g);
            geo__resolve(&g->future, &mel_geo_lost, 0);
        }
        g = next;
    }
}

static void geo__timeout_drain_expired(i64 now)
{
    Mel_Geo_Request* req = g_geo.requests;
    while (req != NULL)
    {
        Mel_Geo_Request* next = req->next;
        if (req->deadline_ns != MEL_GEO_NEVER && req->deadline_ns <= now)
        {
            u32 expected = 0;
            if (atomic_compare_exchange_strong_explicit(&req->claimed, &expected, 1u, memory_order_acq_rel, memory_order_acquire))
            {
                const Mel_Geo_Provider_Desc* p = g_geo.active != NULL ? &g_geo.active->desc : NULL;
                if (p != NULL && p->request_cancel != NULL)
                    p->request_cancel(p->user, req);
                geo__request_finish(req, g_geo.have_fix ? &g_geo.latest : NULL, &mel_geo_timeout, MEL_FUTURE_TIMED_OUT);
            }
        }
        req = next;
    }
}

static i64 geo__timeout_deadline(Mel_Vat_Source* source)
{
    (void)source;
    i64 min = MEL_VAT_NEVER;
    for (Mel_Geo_Request* req = g_geo.requests; req != NULL; req = req->next)
        if (atomic_load_explicit(&req->claimed, memory_order_acquire) == 0u && req->deadline_ns < min)
            min = req->deadline_ns;
    return min;
}

static bool geo__timeout_drain(Mel_Vat_Source* source, u32 budget)
{
    (void)source;
    (void)budget;
    geo__timeout_drain_expired((i64)mel_nanos_since_unspecified_epoch());
    return false;
}

static const Mel_Vat_Source_Vtbl GEO_TIMEOUT_VT = {
    .deadline = geo__timeout_deadline,
    .drain = geo__timeout_drain,
};

void mel_geo_init(Mel_Vat* vat)
{
    mel_assert(vat != NULL);
    mel_assert_msg("geolocation already initialised", g_geo.vat == NULL);
    mel_assert(mel_vat_is_owner(vat));
    g_geo.vat = vat;
    g_geo.fix_consumed = atomic_load_explicit(&g_geo.fix_seq, memory_order_relaxed);
    g_geo.heading_consumed = atomic_load_explicit(&g_geo.heading_seq, memory_order_relaxed);
    mel_task_init(&g_geo.pump, geo__pump);
    mel_geo__register_host_providers();
    if (g_geo.active == NULL)
        geo__activate();
    g_geo.timeouts = mel_vat_source_open(vat, &GEO_TIMEOUT_VT, NULL);
}

void mel_geo_shutdown(void)
{
    if (g_geo.vat == NULL)
        return;
    geo__assert_owner();
    mel_assert_msg("watches still started at shutdown", g_geo.watches == NULL);
    mel_assert_msg("heading watches still started at shutdown", g_geo.heading_watches == NULL);
    mel_assert_msg("regions still added at shutdown", g_geo.regions == NULL);
    mel_assert_msg("requests still pending at shutdown", g_geo.requests == NULL);
    mel_assert_msg("geocodes still pending at shutdown", g_geo.geocodes == NULL);
    if (g_geo.active != NULL)
    {
        const Mel_Geo_Provider_Desc* p = &g_geo.active->desc;
        if (g_geo.streaming)
            p->stream_stop(p->user);
        if (g_geo.heading_streaming && p->heading_stop != NULL)
            p->heading_stop(p->user);
        if (p->detach != NULL)
            p->detach(p->user);
    }
    mel_vat_source_close(g_geo.timeouts);

    Mel_Geo_Provider_Node* external_head = NULL;
    Mel_Geo_Provider_Node** ep = &external_head;
    for (Mel_Geo_Provider_Node* n = g_geo.providers; n != NULL;)
    {
        Mel_Geo_Provider_Node* next = n->next;
        n->next = NULL;
        if (!n->host)
        {
            *ep = n;
            ep = &n->next;
        }
        n = next;
    }

    memset(&g_geo, 0, sizeof g_geo);
    g_geo.providers = external_head;
}

Mel_Geo_Caps mel_geo_caps(void)
{
    geo__assert_owner();
    if (g_geo.active == NULL || g_geo.active->desc.caps == NULL)
        return (Mel_Geo_Caps){ 0 };
    return g_geo.active->desc.caps(g_geo.active->desc.user);
}

const mel_geo_auth* mel_geo_authorization(void)
{
    geo__assert_owner();
    if (g_geo.active == NULL || g_geo.active->desc.authorization == NULL)
        return &mel_geo_auth_restricted;
    return g_geo.active->desc.authorization(g_geo.active->desc.user);
}

void mel_geo_authorize(const mel_geo_scope* scope, Mel_Future* future)
{
    geo__assert_owner();
    mel_assert(scope != NULL && future != NULL);
    if (g_geo.active == NULL || g_geo.active->desc.authorize == NULL)
    {
        mel_future_resolve(future, (void*)(uintptr_t)&mel_geo_auth_restricted, MEL_FUTURE_ERROR);
        return;
    }
    g_geo.active->desc.authorize(g_geo.active->desc.user, scope, future);
}

const mel_geo_result* mel_geo_last_known(Mel_Geo_Fix* out)
{
    geo__assert_owner();
    mel_assert(out != NULL);
    if (g_geo.active == NULL || g_geo.active->desc.last_known == NULL)
        return &mel_geo_unavailable;
    return g_geo.active->desc.last_known(g_geo.active->desc.user, out);
}

void mel_geo_request(Mel_Geo_Request* req)
{
    geo__assert_owner();
    mel_assert(req != NULL && !req->linked);
    mel_assert_msg("request accuracy_m must be > 0", req->accuracy_m > 0.0);
    mel_assert_msg("request timeout_ns must be > 0 or MEL_GEO_NEVER", req->timeout_ns > 0);
    mel_future_init(&req->future, NULL, NULL);
    atomic_store_explicit(&req->claimed, 0u, memory_order_release);
    req->done_result = NULL;
    req->fix = (Mel_Geo_Fix){ 0 };
    req->next = NULL;

    if (g_geo.active == NULL)
    {
        geo__resolve(&req->future, &mel_geo_unavailable, 0);
        return;
    }
    const Mel_Geo_Provider_Desc* p = &g_geo.active->desc;

    if (req->max_age_ns > 0 && p->last_known != NULL)
    {
        Mel_Geo_Fix lk = { 0 };
        if (mel_geo_result_ok(p->last_known(p->user, &lk)) && (lk.valid & MEL_GEO_VALID_POSITION) && (lk.valid & MEL_GEO_VALID_MONOTONIC))
        {
            u64  now = mel_nanos_since_unspecified_epoch();
            bool fresh = now - lk.monotonic_ns <= (u64)req->max_age_ns;
            bool sharp = !(lk.valid & MEL_GEO_VALID_HACC) || lk.horizontal_accuracy_m <= req->accuracy_m;
            if (fresh && sharp)
            {
                req->fix = lk;
                geo__resolve(&req->future, &mel_geo_ok, 0);
                return;
            }
        }
    }

    req->deadline_ns = req->timeout_ns == MEL_GEO_NEVER ? MEL_GEO_NEVER : (i64)mel_nanos_since_unspecified_epoch() + req->timeout_ns;
    req->linked = true;
    req->next = g_geo.requests;
    g_geo.requests = req;
    mel_vat_source_demand_changed(g_geo.timeouts);
    if (p->request != NULL)
        p->request(p->user, req);
    else
    {
        u32 expected = 0;
        if (atomic_compare_exchange_strong_explicit(&req->claimed, &expected, 1u, memory_order_acq_rel, memory_order_acquire))
            geo__request_finish(req, NULL, &mel_geo_unsupported, 0);
    }
}

void mel_geo_request_cancel(Mel_Geo_Request* req)
{
    geo__assert_owner();
    mel_assert(req != NULL);
    u32 expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&req->claimed, &expected, 1u, memory_order_acq_rel, memory_order_acquire))
        return;
    if (g_geo.active != NULL && g_geo.active->desc.request_cancel != NULL)
        g_geo.active->desc.request_cancel(g_geo.active->desc.user, req);
    if (req->linked)
        geo__request_unlink(req);
    mel_future_cancel(&req->future);
}

const mel_geo_result* mel_geo_watch_start(Mel_Geo_Watch* w)
{
    geo__assert_owner();
    mel_assert(w != NULL && w->cb != NULL);
    mel_assert_msg("watch exec is required", w->exec != NULL);
    mel_assert_msg("watch accuracy_m must be > 0", w->accuracy_m > 0.0);
    mel_assert_msg("watch already started", atomic_load_explicit(&w->live, memory_order_relaxed) == 0u);
    mel_task_init(&w->task, geo__watch_task);
    atomic_store_explicit(&w->pending_seq, 0u, memory_order_relaxed);
    w->pending_result = NULL;
    w->has_last = false;
    w->last_delivery_ns = 0;
    atomic_store_explicit(&w->live, 1u, memory_order_release);
    w->next = g_geo.watches;
    g_geo.watches = w;
    return geo__recompute_demand();
}

void mel_geo_watch_stop(Mel_Geo_Watch* w)
{
    geo__assert_owner();
    mel_assert(w != NULL);
    if (atomic_load_explicit(&w->live, memory_order_relaxed) == 0u)
        return;
    atomic_store_explicit(&w->live, 0u, memory_order_release);
    for (Mel_Geo_Watch** pp = &g_geo.watches; *pp != NULL; pp = &(*pp)->next)
        if (*pp == w)
        {
            *pp = w->next;
            break;
        }
    w->next = NULL;
    geo__recompute_demand();
}

const mel_geo_result* mel_geo_heading_start(Mel_Geo_Heading_Watch* w)
{
    geo__assert_owner();
    mel_assert(w != NULL && w->cb != NULL);
    mel_assert_msg("heading watch exec is required", w->exec != NULL);
    mel_assert_msg("heading watch already started", atomic_load_explicit(&w->live, memory_order_relaxed) == 0u);
    if (g_geo.active == NULL)
        return &mel_geo_unavailable;
    if (g_geo.active->desc.heading_start == NULL)
        return &mel_geo_unsupported;
    mel_task_init(&w->task, geo__heading_task);
    atomic_store_explicit(&w->pending_seq, 0u, memory_order_relaxed);
    w->pending_result = NULL;
    atomic_store_explicit(&w->live, 1u, memory_order_release);
    w->next = g_geo.heading_watches;
    g_geo.heading_watches = w;
    if (g_geo.heading_streaming)
        return &mel_geo_ok;
    const mel_geo_result* r = g_geo.active->desc.heading_start(g_geo.active->desc.user);
    if (mel_geo_result_ok(r))
        g_geo.heading_streaming = true;
    return r;
}

void mel_geo_heading_stop(Mel_Geo_Heading_Watch* w)
{
    geo__assert_owner();
    mel_assert(w != NULL);
    if (atomic_load_explicit(&w->live, memory_order_relaxed) == 0u)
        return;
    atomic_store_explicit(&w->live, 0u, memory_order_release);
    for (Mel_Geo_Heading_Watch** pp = &g_geo.heading_watches; *pp != NULL; pp = &(*pp)->next)
        if (*pp == w)
        {
            *pp = w->next;
            break;
        }
    w->next = NULL;
    if (g_geo.heading_watches == NULL && g_geo.heading_streaming && g_geo.active != NULL && g_geo.active->desc.heading_stop != NULL)
    {
        g_geo.active->desc.heading_stop(g_geo.active->desc.user);
        g_geo.heading_streaming = false;
    }
}

const mel_geo_result* mel_geo_region_add(Mel_Geo_Region* r)
{
    geo__assert_owner();
    mel_assert(r != NULL && r->cb != NULL);
    mel_assert_msg("region exec is required", r->exec != NULL);
    mel_assert_msg("region radius_m must be > 0", r->radius_m > 0.0);
    mel_assert_msg("region must notify enter, exit, or both", r->notify_enter || r->notify_exit);
    mel_assert_msg("region already added", atomic_load_explicit(&r->live, memory_order_relaxed) == 0u);
    if (g_geo.active == NULL)
        return &mel_geo_unavailable;
    mel_task_init(&r->task, geo__region_task);
    atomic_store_explicit(&r->pending_bits, 0u, memory_order_relaxed);
    atomic_store_explicit(&r->pending_result, NULL, memory_order_relaxed);
    r->seeded = false;
    r->inside = false;
    const Mel_Geo_Provider_Desc* p = &g_geo.active->desc;
    if (p->region_add != NULL)
    {
        const mel_geo_result* res = p->region_add(p->user, r);
        if (!mel_geo_result_ok(res))
            return res;
        r->sw = false;
    }
    else
        r->sw = true;
    atomic_store_explicit(&r->live, 1u, memory_order_release);
    r->next = g_geo.regions;
    g_geo.regions = r;
    if (r->sw)
        geo__recompute_demand();
    return &mel_geo_ok;
}

void mel_geo_region_remove(Mel_Geo_Region* r)
{
    geo__assert_owner();
    mel_assert(r != NULL);
    if (atomic_load_explicit(&r->live, memory_order_relaxed) == 0u)
        return;
    atomic_store_explicit(&r->live, 0u, memory_order_release);
    for (Mel_Geo_Region** pp = &g_geo.regions; *pp != NULL; pp = &(*pp)->next)
        if (*pp == r)
        {
            *pp = r->next;
            break;
        }
    r->next = NULL;
    if (!r->sw && g_geo.active != NULL && g_geo.active->desc.region_remove != NULL)
        g_geo.active->desc.region_remove(g_geo.active->desc.user, r);
    if (r->sw)
        geo__recompute_demand();
}

static void geo__geocode_submit(Mel_Geo_Geocode* g, bool reverse)
{
    geo__assert_owner();
    mel_assert(g != NULL && !g->linked);
    mel_assert_msg("geocode alloc is required", g->alloc != NULL);
    mel_assert_msg("geocode max_results must be > 0", g->max_results > 0);
    if (!reverse)
        mel_assert_msg("forward geocode query must be non-empty", g->query.len > 0);
    mel_future_init(&g->future, NULL, NULL);
    atomic_store_explicit(&g->claimed, 0u, memory_order_release);
    g->done_result = NULL;
    g->reverse = reverse;
    g->next = NULL;
    mel_array_init(&g->places, g->alloc);

    const Mel_Geo_Provider_Desc* p = g_geo.active != NULL ? &g_geo.active->desc : NULL;
    void (*op)(void*, Mel_Geo_Geocode*) = NULL;
    if (p != NULL)
        op = reverse ? p->geocode_reverse : p->geocode_forward;
    if (op == NULL)
    {
        geo__resolve(&g->future, &mel_geo_unsupported, 0);
        return;
    }
    g->linked = true;
    g->next = g_geo.geocodes;
    g_geo.geocodes = g;
    op(p->user, g);
}

void mel_geo_geocode_forward(Mel_Geo_Geocode* g) { geo__geocode_submit(g, false); }
void mel_geo_geocode_reverse(Mel_Geo_Geocode* g) { geo__geocode_submit(g, true); }

void mel_geo_geocode_cancel(Mel_Geo_Geocode* g)
{
    geo__assert_owner();
    mel_assert(g != NULL);
    if (!mel_geo_provider_geocode_claim(g))
        return;
    if (g->linked)
        geo__geocode_unlink(g);
    mel_future_cancel(&g->future);
}

static void geo__place_str_free(str8 s, const Mel_Alloc* alloc)
{
    if (s.data != NULL && s.len > 0)
        mel_dealloc(alloc, s.data);
}

void mel_geo_geocode_free(Mel_Geo_Geocode* g)
{
    geo__assert_owner();
    mel_assert(g != NULL);
    mel_assert_msg("geocode freed while in flight", mel_future_resolved(&g->future));
    for (usize i = 0; i < g->places.count; i++)
    {
        Mel_Geo_Place* pl = &g->places.items[i];
        geo__place_str_free(pl->name, g->alloc);
        geo__place_str_free(pl->thoroughfare, g->alloc);
        geo__place_str_free(pl->locality, g->alloc);
        geo__place_str_free(pl->admin_area, g->alloc);
        geo__place_str_free(pl->postal_code, g->alloc);
        geo__place_str_free(pl->country, g->alloc);
        geo__place_str_free(pl->country_code, g->alloc);
    }
    mel_array_free(&g->places);
}
