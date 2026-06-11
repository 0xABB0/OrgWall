#include <geolocation/geolocation.h>
#include <geolocation/provider.h>

#include "mock_provider.h"

#include <test/test.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <future/future.h>
#include <time/nano.h>
#include <vat/vat.h>

#include <string.h>

void mel_geo__register_host_providers(void) {}

#define GEO_M_PER_DEG_LAT 111194.926644559

static Mel_Vat* g_vat;

static void geo_setup(void)
{
    const Mel_Alloc* a = mel_alloc_heap();
    g_vat = mel_vat_open(a, (Mel_Vat_Desc){ .waiter = mel_vat_waiter_io(a), .driver = mel_vat_driver_fair(a, 64) });
    mock_geo_install();
    mel_geo_init(g_vat);
}

static void geo_teardown(void)
{
    mel_geo_shutdown();
    mock_geo_uninstall();
    mel_vat_close(g_vat);
    g_vat = NULL;
}

static void geo_step(void) { mel_vat_step(g_vat); }

static Mel_Geo_Fix make_fix(f64 lat_deg, f64 lon_deg, f64 hacc_m)
{
    return (Mel_Geo_Fix){
        .latitude_deg = lat_deg,
        .longitude_deg = lon_deg,
        .horizontal_accuracy_m = hacc_m,
        .monotonic_ns = mel_nanos_since_unspecified_epoch(),
        .valid = MEL_GEO_VALID_POSITION | MEL_GEO_VALID_HACC | MEL_GEO_VALID_MONOTONIC,
    };
}

typedef struct
{
    u32                   calls;
    Mel_Geo_Fix           fix;
    const mel_geo_result* result;
} Fix_Rec;

static void rec_fix(const Mel_Geo_Fix* fix, const mel_geo_result* r, void* user)
{
    Fix_Rec* rec = user;
    rec->calls++;
    rec->fix = *fix;
    rec->result = r;
}

typedef struct
{
    u32                   enters, exits;
    const mel_geo_result* result;
} Region_Rec;

static void rec_region(const Mel_Geo_Region_Event* ev, void* user)
{
    Region_Rec* rec = user;
    if (ev->entered)
        rec->enters++;
    if (ev->exited)
        rec->exits++;
    rec->result = ev->result;
}

typedef struct
{
    u32                   calls;
    Mel_Geo_Heading       heading;
    const mel_geo_result* result;
} Heading_Rec;

static void rec_heading(const Mel_Geo_Heading* h, const mel_geo_result* r, void* user)
{
    Heading_Rec* rec = user;
    rec->calls++;
    rec->heading = *h;
    rec->result = r;
}

MEL_TEST(geolocation, init_activates_provider_and_caps_pass_through)
{
    geo_setup();
    Mel_Geo_Caps caps = mel_geo_caps();
    MEL_EXPECT(caps.fixes);
    MEL_EXPECT(caps.heading);
    MEL_EXPECT(caps.geocoding);
    MEL_EXPECT(!caps.regions_native);
    MEL_EXPECT(mel_geo_auth_is_granted(mel_geo_authorization()));
    geo_teardown();
}

MEL_TEST(geolocation, watch_starts_stream_and_delivers_fix)
{
    geo_setup();
    Fix_Rec       rec = { 0 };
    Mel_Geo_Watch w = { .accuracy_m = 50.0, .cb = rec_fix, .user = &rec, .exec = mel_executor_inline() };
    MEL_REQUIRE(mel_geo_result_ok(mel_geo_watch_start(&w)));
    MEL_EXPECT(mock_geo_streaming());
    MEL_EXPECT_EQ(mock_geo_demand().accuracy_m, 50.0);

    Mel_Geo_Fix fix = make_fix(45.07, 7.69, 10.0);
    mock_geo_push_fix(&fix);
    geo_step();
    MEL_EXPECT_EQ(rec.calls, (u32)1);
    MEL_EXPECT_EQ(rec.fix.latitude_deg, 45.07);
    MEL_EXPECT(mel_geo_result_ok(rec.result));

    mel_geo_watch_stop(&w);
    MEL_EXPECT(!mock_geo_streaming());
    geo_teardown();
}

MEL_TEST(geolocation, watch_coalesces_to_latest_fix)
{
    geo_setup();
    Fix_Rec       rec = { 0 };
    Mel_Geo_Watch w = { .accuracy_m = 50.0, .cb = rec_fix, .user = &rec, .exec = mel_executor_inline() };
    MEL_REQUIRE(mel_geo_result_ok(mel_geo_watch_start(&w)));

    Mel_Geo_Fix f1 = make_fix(1.0, 1.0, 10.0);
    Mel_Geo_Fix f2 = make_fix(2.0, 2.0, 10.0);
    Mel_Geo_Fix f3 = make_fix(3.0, 3.0, 10.0);
    mock_geo_push_fix(&f1);
    mock_geo_push_fix(&f2);
    mock_geo_push_fix(&f3);
    geo_step();
    geo_step();
    MEL_EXPECT_EQ(rec.calls, (u32)1);
    MEL_EXPECT_EQ(rec.fix.latitude_deg, 3.0);

    mel_geo_watch_stop(&w);
    geo_teardown();
}

MEL_TEST(geolocation, watch_interval_filter_drops_fast_updates)
{
    geo_setup();
    Fix_Rec       rec = { 0 };
    Mel_Geo_Watch w = { .accuracy_m = 50.0, .min_interval_ns = 3600ll * 1000000000ll, .cb = rec_fix, .user = &rec, .exec = mel_executor_inline() };
    MEL_REQUIRE(mel_geo_result_ok(mel_geo_watch_start(&w)));

    Mel_Geo_Fix f1 = make_fix(1.0, 1.0, 10.0);
    mock_geo_push_fix(&f1);
    geo_step();
    MEL_EXPECT_EQ(rec.calls, (u32)1);

    Mel_Geo_Fix f2 = make_fix(2.0, 2.0, 10.0);
    mock_geo_push_fix(&f2);
    geo_step();
    MEL_EXPECT_EQ(rec.calls, (u32)1);

    mel_geo_watch_stop(&w);
    geo_teardown();
}

MEL_TEST(geolocation, watch_distance_filter_drops_near_updates)
{
    geo_setup();
    Fix_Rec       rec = { 0 };
    Mel_Geo_Watch w = { .accuracy_m = 50.0, .min_distance_m = 100.0, .cb = rec_fix, .user = &rec, .exec = mel_executor_inline() };
    MEL_REQUIRE(mel_geo_result_ok(mel_geo_watch_start(&w)));

    Mel_Geo_Fix f1 = make_fix(0.0, 0.0, 10.0);
    mock_geo_push_fix(&f1);
    geo_step();
    MEL_EXPECT_EQ(rec.calls, (u32)1);

    Mel_Geo_Fix f2 = make_fix(10.0 / GEO_M_PER_DEG_LAT, 0.0, 10.0);
    mock_geo_push_fix(&f2);
    geo_step();
    MEL_EXPECT_EQ(rec.calls, (u32)1);

    Mel_Geo_Fix f3 = make_fix(200.0 / GEO_M_PER_DEG_LAT, 0.0, 10.0);
    mock_geo_push_fix(&f3);
    geo_step();
    MEL_EXPECT_EQ(rec.calls, (u32)2);

    mel_geo_watch_stop(&w);
    geo_teardown();
}

MEL_TEST(geolocation, watch_survives_outage_and_recovers)
{
    geo_setup();
    Fix_Rec       rec = { 0 };
    Mel_Geo_Watch w = { .accuracy_m = 50.0, .cb = rec_fix, .user = &rec, .exec = mel_executor_inline() };
    MEL_REQUIRE(mel_geo_result_ok(mel_geo_watch_start(&w)));

    mock_geo_push_stream_result(&mel_geo_denied);
    geo_step();
    MEL_EXPECT_EQ(rec.calls, (u32)1);
    MEL_EXPECT_EQ(rec.result, &mel_geo_denied);
    MEL_EXPECT_EQ(rec.fix.valid, (u32)0);

    mock_geo_push_stream_result(&mel_geo_ok);
    geo_step();
    MEL_EXPECT_EQ(rec.calls, (u32)2);
    MEL_EXPECT(mel_geo_result_ok(rec.result));

    Mel_Geo_Fix fix = make_fix(5.0, 5.0, 10.0);
    mock_geo_push_fix(&fix);
    geo_step();
    MEL_EXPECT_EQ(rec.calls, (u32)3);
    MEL_EXPECT_EQ(rec.fix.latitude_deg, 5.0);

    mel_geo_watch_stop(&w);
    geo_teardown();
}

MEL_TEST(geolocation, demand_is_union_of_watches)
{
    geo_setup();
    Fix_Rec       rec = { 0 };
    Mel_Geo_Watch coarse = { .accuracy_m = 100.0, .cb = rec_fix, .user = &rec, .exec = mel_executor_inline() };
    Mel_Geo_Watch fine = { .accuracy_m = 10.0, .cb = rec_fix, .user = &rec, .exec = mel_executor_inline() };

    MEL_REQUIRE(mel_geo_result_ok(mel_geo_watch_start(&coarse)));
    MEL_EXPECT_EQ(mock_geo_stream_starts(), (u32)1);
    MEL_EXPECT_EQ(mock_geo_demand().accuracy_m, 100.0);

    MEL_REQUIRE(mel_geo_result_ok(mel_geo_watch_start(&fine)));
    MEL_EXPECT_EQ(mock_geo_demand().accuracy_m, 10.0);

    mel_geo_watch_stop(&fine);
    MEL_EXPECT_EQ(mock_geo_demand().accuracy_m, 100.0);

    mel_geo_watch_stop(&coarse);
    MEL_EXPECT_EQ(mock_geo_stream_stops(), (u32)1);
    MEL_EXPECT(!mock_geo_streaming());
    geo_teardown();
}

MEL_TEST(geolocation, request_resolves_from_backend)
{
    geo_setup();
    Mel_Geo_Request req = { .accuracy_m = 50.0, .timeout_ns = MEL_GEO_NEVER };
    mel_geo_request(&req);
    MEL_EXPECT_EQ(mock_geo_request_calls(), (u32)1);
    MEL_REQUIRE(mock_geo_pending_request() == &req);

    Mel_Geo_Fix fix = make_fix(45.0, 7.0, 8.0);
    mock_geo_resolve_request(&fix, &mel_geo_ok);
    geo_step();
    MEL_REQUIRE(mel_future_resolved(&req.future));
    MEL_EXPECT(mel_geo_result_ok(mel_geo_future_result(&req.future)));
    MEL_EXPECT_EQ(req.fix.latitude_deg, 45.0);
    geo_teardown();
}

MEL_TEST(geolocation, request_cancel_resolves_cancelled)
{
    geo_setup();
    Mel_Geo_Request req = { .accuracy_m = 50.0, .timeout_ns = MEL_GEO_NEVER };
    mel_geo_request(&req);
    mel_geo_request_cancel(&req);
    MEL_REQUIRE(mel_future_resolved(&req.future));
    MEL_EXPECT_EQ(mel_geo_future_result(&req.future), &mel_geo_cancelled);
    MEL_EXPECT_EQ(mock_geo_request_cancels(), (u32)1);
    geo_teardown();
}

MEL_TEST(geolocation, request_served_from_last_known_when_fresh)
{
    geo_setup();
    Mel_Geo_Fix lk = make_fix(44.0, 8.0, 5.0);
    mock_geo_set_last_known(&lk);

    Mel_Geo_Request req = { .accuracy_m = 10.0, .timeout_ns = MEL_GEO_NEVER, .max_age_ns = 10ll * 1000000000ll };
    mel_geo_request(&req);
    MEL_REQUIRE(mel_future_resolved(&req.future));
    MEL_EXPECT(mel_geo_result_ok(mel_geo_future_result(&req.future)));
    MEL_EXPECT_EQ(req.fix.latitude_deg, 44.0);
    MEL_EXPECT_EQ(mock_geo_request_calls(), (u32)0);
    geo_teardown();
}

MEL_TEST(geolocation, request_times_out_with_partial_result)
{
    geo_setup();
    Mel_Geo_Request req = { .accuracy_m = 50.0, .timeout_ns = 50ll * 1000000ll };
    mel_geo_request(&req);
    for (u32 i = 0; i < 200 && !mel_future_resolved(&req.future); i++)
        geo_step();
    MEL_REQUIRE(mel_future_resolved(&req.future));
    MEL_EXPECT_EQ(mel_geo_future_result(&req.future), &mel_geo_timeout);
    MEL_EXPECT((mel_future_status(&req.future) & MEL_FUTURE_TIMED_OUT) != 0u);
    MEL_EXPECT_EQ(mock_geo_request_cancels(), (u32)1);
    geo_teardown();
}

MEL_TEST(geolocation, software_region_enters_and_exits_with_hysteresis)
{
    geo_setup();
    Region_Rec     rec = { 0 };
    Mel_Geo_Region region = {
        .latitude_deg = 0.0,
        .longitude_deg = 0.0,
        .radius_m = 100.0,
        .notify_enter = true,
        .notify_exit = true,
        .cb = rec_region,
        .user = &rec,
        .exec = mel_executor_inline(),
    };
    MEL_REQUIRE(mel_geo_result_ok(mel_geo_region_add(&region)));
    MEL_EXPECT(mock_geo_streaming());

    Mel_Geo_Fix inside = make_fix(0.0, 0.0, 5.0);
    mock_geo_push_fix(&inside);
    geo_step();
    MEL_EXPECT_EQ(rec.enters, (u32)0);
    MEL_EXPECT_EQ(rec.exits, (u32)0);

    Mel_Geo_Fix out = make_fix(130.0 / GEO_M_PER_DEG_LAT, 0.0, 5.0);
    mock_geo_push_fix(&out);
    geo_step();
    MEL_EXPECT_EQ(rec.exits, (u32)1);

    Mel_Geo_Fix boundary = make_fix(100.0 / GEO_M_PER_DEG_LAT, 0.0, 5.0);
    mock_geo_push_fix(&boundary);
    geo_step();
    MEL_EXPECT_EQ(rec.enters, (u32)0);
    MEL_EXPECT_EQ(rec.exits, (u32)1);

    Mel_Geo_Fix back = make_fix(50.0 / GEO_M_PER_DEG_LAT, 0.0, 5.0);
    mock_geo_push_fix(&back);
    geo_step();
    MEL_EXPECT_EQ(rec.enters, (u32)1);

    mel_geo_region_remove(&region);
    MEL_EXPECT(!mock_geo_streaming());
    geo_teardown();
}

MEL_TEST(geolocation, heading_watch_delivers)
{
    geo_setup();
    Heading_Rec           rec = { 0 };
    Mel_Geo_Heading_Watch w = { .cb = rec_heading, .user = &rec, .exec = mel_executor_inline() };
    MEL_REQUIRE(mel_geo_result_ok(mel_geo_heading_start(&w)));

    Mel_Geo_Heading h = { .magnetic_deg = 42.0, .valid = MEL_GEO_VALID_HEADING_MAGNETIC };
    mock_geo_push_heading(&h);
    geo_step();
    MEL_EXPECT_EQ(rec.calls, (u32)1);
    MEL_EXPECT_EQ(rec.heading.magnetic_deg, 42.0);

    mel_geo_heading_stop(&w);
    geo_teardown();
}

MEL_TEST(geolocation, geocode_forward_fills_places)
{
    geo_setup();
    Mel_Geo_Geocode g = { .query = S8("mock city"), .max_results = 3, .alloc = mel_alloc_heap() };
    mel_geo_geocode_forward(&g);
    MEL_REQUIRE(mock_geo_pending_geocode() == &g);

    mock_geo_fill_geocode(2);
    geo_step();
    MEL_REQUIRE(mel_future_resolved(&g.future));
    MEL_EXPECT(mel_geo_result_ok(mel_geo_future_result(&g.future)));
    MEL_EXPECT_EQ(g.places.count, (usize)2);
    MEL_EXPECT_EQ(g.places.items[1].latitude_deg, 11.0);
    mel_geo_geocode_free(&g);
    geo_teardown();
}

MEL_TEST(geolocation, geocode_cancel_discards_late_fill)
{
    geo_setup();
    Mel_Geo_Geocode g = { .query = S8("mock city"), .max_results = 3, .alloc = mel_alloc_heap() };
    mel_geo_geocode_forward(&g);
    mel_geo_geocode_cancel(&g);
    MEL_REQUIRE(mel_future_resolved(&g.future));
    MEL_EXPECT_EQ(mel_geo_future_result(&g.future), &mel_geo_cancelled);

    mock_geo_fill_geocode(2);
    geo_step();
    MEL_EXPECT_EQ(g.places.count, (usize)0);
    mel_geo_geocode_free(&g);
    geo_teardown();
}

MEL_TEST(geolocation, authorize_resolves_with_provider_auth)
{
    geo_setup();
    mock_geo_set_auth(&mel_geo_auth_granted_always);
    Mel_Future f;
    mel_future_init(&f, NULL, NULL);
    mel_geo_authorize(&mel_geo_scope_always, &f);
    MEL_REQUIRE(mel_future_resolved(&f));
    MEL_EXPECT_EQ(mel_geo_future_auth(&f), &mel_geo_auth_granted_always);
    geo_teardown();
}

MEL_TEST(geolocation, provider_unregister_reports_lost)
{
    geo_setup();
    Fix_Rec       rec = { 0 };
    Mel_Geo_Watch w = { .accuracy_m = 50.0, .cb = rec_fix, .user = &rec, .exec = mel_executor_inline() };
    MEL_REQUIRE(mel_geo_result_ok(mel_geo_watch_start(&w)));

    Mel_Geo_Request req = { .accuracy_m = 50.0, .timeout_ns = MEL_GEO_NEVER };
    mel_geo_request(&req);

    mock_geo_uninstall();
    MEL_EXPECT_EQ(rec.calls, (u32)1);
    MEL_EXPECT_EQ(rec.result, &mel_geo_lost);
    MEL_REQUIRE(mel_future_resolved(&req.future));
    MEL_EXPECT_EQ(mel_geo_future_result(&req.future), &mel_geo_lost);

    mel_geo_watch_stop(&w);
    geo_teardown();
}

MEL_TEST(geolocation, distance_great_circle_sanity)
{
    f64 d = mel_geo_distance_m(0.0, 0.0, 1.0, 0.0);
    MEL_EXPECT(d > 111000.0 && d < 111400.0);
    MEL_EXPECT_EQ(mel_geo_distance_m(45.0, 7.0, 45.0, 7.0), 0.0);
}

MEL_TEST(geolocation, external_provider_survives_reinit)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Vat*         vat = mel_vat_open(a, (Mel_Vat_Desc){ .waiter = mel_vat_waiter_io(a), .driver = mel_vat_driver_fair(a, 64) });

    mock_geo_install();
    mel_geo_init(vat);
    MEL_EXPECT(mel_geo_caps().fixes);
    mel_geo_shutdown();

    mel_geo_init(vat);
    MEL_EXPECT(mel_geo_caps().fixes);
    mel_geo_shutdown();

    mock_geo_uninstall();
    mel_vat_close(vat);
}
