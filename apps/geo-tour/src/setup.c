#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <boot/boot.h>
#include <executor/executor.h>
#include <future/future.h>
#include <geolocation/geolocation.h>
#include <vat/vat.h>

#include <stdio.h>

#define TOUR_WATCH_FIXES   6
#define TOUR_REQ_ACC_M     100.0
#define TOUR_REQ_TIMEOUT   (20ll * 1000000000ll)
#define TOUR_WATCH_ACC_M   25.0
#define TOUR_REGION_M      75.0
#define TOUR_FORWARD_QUERY "Piazza Castello, Torino"

typedef struct
{
    Mel_Vat* root;

    Mel_Future auth_future;
    Mel_Task   auth_task;

    Mel_Geo_Request request;
    Mel_Task        request_task;

    Mel_Geo_Watch         watch;
    u32                   watch_fixes;
    bool                  watch_running;
    Mel_Geo_Heading_Watch heading;
    bool                  heading_running;

    Mel_Geo_Region region;
    bool           region_armed;

    Mel_Geo_Geocode forward;
    Mel_Task        forward_task;
    Mel_Geo_Geocode reverse;
    Mel_Task        reverse_task;

    Mel_Geo_Request grace;
    Mel_Task        grace_task;
    bool            grace_started;

    u32 pending;
} Tour;

static Tour g_tour;

static void tour_print_fix(const char* tag, const Mel_Geo_Fix* fix)
{
    if (!(fix->valid & MEL_GEO_VALID_POSITION))
    {
        printf("%-10s (no position)\n", tag);
        return;
    }
    printf("%-10s %+.6f, %+.6f", tag, fix->latitude_deg, fix->longitude_deg);
    if (fix->valid & MEL_GEO_VALID_HACC)
        printf("  ±%.0fm", fix->horizontal_accuracy_m);
    if (fix->valid & MEL_GEO_VALID_ALTITUDE)
        printf("  alt %.0fm", fix->altitude_m);
    if (fix->valid & MEL_GEO_VALID_SPEED)
        printf("  %.1fm/s", fix->speed_mps);
    if (fix->valid & MEL_GEO_VALID_COURSE)
        printf("  course %.0f°", fix->course_deg);
    printf("\n");
}

static void tour_print_places(const char* tag, const Mel_Geo_Geocode* g)
{
    if (g->query.len > 0)
        printf("%-10s %u place(s) for \"%.*s\"\n", tag, (u32)g->places.count, (int)g->query.len, (const char*)g->query.data);
    else
        printf("%-10s %u place(s) at %+.5f, %+.5f\n", tag, (u32)g->places.count, g->latitude_deg, g->longitude_deg);
    for (usize i = 0; i < g->places.count; i++)
    {
        const Mel_Geo_Place* p = &g->places.items[i];
        printf("           - %.*s, %.*s, %.*s", (int)p->name.len, p->name.data, (int)p->locality.len, p->locality.data,
               (int)p->country.len, p->country.data);
        if (p->valid & MEL_GEO_VALID_POSITION)
            printf("  (%+.5f, %+.5f)", p->latitude_deg, p->longitude_deg);
        printf("\n");
    }
}

static void tour_watch_finish(void);
static void tour_on_grace(Mel_Task* task);

static void tour_stage_done(void)
{
    g_tour.pending--;
    if (g_tour.pending == 1 && g_tour.watch_running && !g_tour.grace_started)
    {
        g_tour.grace_started = true;
        g_tour.grace = (Mel_Geo_Request){ .accuracy_m = 5000.0, .timeout_ns = 10ll * 1000000000ll };
        mel_geo_request(&g_tour.grace);
        mel_task_init(&g_tour.grace_task, tour_on_grace);
        mel_future_then(&g_tour.grace.future, &g_tour.grace_task, mel_vat_executor(g_tour.root));
    }
    if (g_tour.pending > 0)
        return;
    if (g_tour.grace_started && !mel_future_resolved(&g_tour.grace.future))
        mel_geo_request_cancel(&g_tour.grace);
    if (g_tour.heading_running)
    {
        mel_geo_heading_stop(&g_tour.heading);
        g_tour.heading_running = false;
    }
    if (g_tour.watch_running)
    {
        mel_geo_watch_stop(&g_tour.watch);
        g_tour.watch_running = false;
    }
    if (g_tour.region_armed)
    {
        mel_geo_region_remove(&g_tour.region);
        g_tour.region_armed = false;
        printf("region     removed\n");
    }
    printf("tour       complete\n");
    mel_geo_shutdown();
    mel_vat_release(g_tour.root);
}

static void tour_on_grace(Mel_Task* task)
{
    (void)task;
    if (!g_tour.watch_running)
        return;
    printf("watch      stationary; ending the watch after the grace window\n");
    tour_watch_finish();
}

static void tour_watch_finish(void)
{
    if (!g_tour.watch_running)
        return;
    mel_geo_watch_stop(&g_tour.watch);
    g_tour.watch_running = false;
    if (g_tour.heading_running)
    {
        mel_geo_heading_stop(&g_tour.heading);
        g_tour.heading_running = false;
    }
    tour_stage_done();
}

static void tour_on_watch_fix(const Mel_Geo_Fix* fix, const mel_geo_result* result, void* user)
{
    (void)user;
    if (!mel_geo_result_ok(result))
    {
        printf("watch      %s\n", mel_geo_result_name(result));
        return;
    }
    tour_print_fix("watch", fix);
    if (++g_tour.watch_fixes >= TOUR_WATCH_FIXES)
        tour_watch_finish();
}

static void tour_on_heading(const Mel_Geo_Heading* h, const mel_geo_result* result, void* user)
{
    (void)user;
    if (!mel_geo_result_ok(result))
    {
        printf("heading    %s\n", mel_geo_result_name(result));
        return;
    }
    if (h->valid & MEL_GEO_VALID_HEADING_MAGNETIC)
        printf("heading    %.0f° magnetic\n", h->magnetic_deg);
}

static void tour_on_region(const Mel_Geo_Region_Event* ev, void* user)
{
    (void)user;
    if (!mel_geo_result_ok(ev->result))
    {
        printf("region     %s\n", mel_geo_result_name(ev->result));
        return;
    }
    printf("region     %s\n", ev->entered ? "entered" : "exited");
}

static void tour_on_reverse(Mel_Task* task)
{
    (void)task;
    const mel_geo_result* r = mel_geo_future_result(&g_tour.reverse.future);
    if (mel_geo_result_ok(r))
        tour_print_places("reverse", &g_tour.reverse);
    else
        printf("reverse    %s\n", mel_geo_result_name(r));
    mel_geo_geocode_free(&g_tour.reverse);
    tour_stage_done();
}

static void tour_on_forward(Mel_Task* task)
{
    (void)task;
    const mel_geo_result* r = mel_geo_future_result(&g_tour.forward.future);
    if (mel_geo_result_ok(r))
        tour_print_places("forward", &g_tour.forward);
    else
        printf("forward    %s\n", mel_geo_result_name(r));
    mel_geo_geocode_free(&g_tour.forward);
    tour_stage_done();
}

static void tour_forward_geocode(void)
{
    g_tour.pending++;
    g_tour.forward = (Mel_Geo_Geocode){
        .query = S8(TOUR_FORWARD_QUERY),
        .max_results = 3,
        .alloc = mel_alloc_heap(),
    };
    mel_geo_geocode_forward(&g_tour.forward);
    mel_task_init(&g_tour.forward_task, tour_on_forward);
    mel_future_then(&g_tour.forward.future, &g_tour.forward_task, mel_vat_executor(g_tour.root));
}

static void tour_reverse_geocode(const Mel_Geo_Fix* fix)
{
    g_tour.pending++;
    g_tour.reverse = (Mel_Geo_Geocode){
        .latitude_deg = fix->latitude_deg,
        .longitude_deg = fix->longitude_deg,
        .max_results = 1,
        .alloc = mel_alloc_heap(),
    };
    mel_geo_geocode_reverse(&g_tour.reverse);
    mel_task_init(&g_tour.reverse_task, tour_on_reverse);
    mel_future_then(&g_tour.reverse.future, &g_tour.reverse_task, mel_vat_executor(g_tour.root));
}

static void tour_arm_region(const Mel_Geo_Fix* fix)
{
    g_tour.region = (Mel_Geo_Region){
        .latitude_deg = fix->latitude_deg,
        .longitude_deg = fix->longitude_deg,
        .radius_m = TOUR_REGION_M,
        .notify_enter = true,
        .notify_exit = true,
        .cb = tour_on_region,
        .exec = mel_vat_executor(g_tour.root),
    };
    const mel_geo_result* r = mel_geo_region_add(&g_tour.region);
    if (mel_geo_result_ok(r))
    {
        g_tour.region_armed = true;
        printf("region     armed: %.0fm around the fix (move to trigger; %s)\n", TOUR_REGION_M,
               mel_geo_caps().regions_native ? "OS-monitored" : "software-evaluated");
    }
    else
        printf("region     %s\n", mel_geo_result_name(r));
}

static void tour_on_request(Mel_Task* task)
{
    (void)task;
    const mel_geo_result* r = mel_geo_future_result(&g_tour.request.future);
    if (mel_geo_result_ok(r))
    {
        tour_print_fix("one-shot", &g_tour.request.fix);
        tour_arm_region(&g_tour.request.fix);
        tour_reverse_geocode(&g_tour.request.fix);
    }
    else
    {
        printf("one-shot   %s\n", mel_geo_result_name(r));
        tour_watch_finish();
    }
    tour_stage_done();
}

static void tour_start_live(void)
{
    Mel_Geo_Fix last;
    if (mel_geo_result_ok(mel_geo_last_known(&last)))
        tour_print_fix("last-known", &last);
    else
        printf("last-known unavailable\n");

    g_tour.pending++;
    g_tour.request = (Mel_Geo_Request){ .accuracy_m = TOUR_REQ_ACC_M, .timeout_ns = TOUR_REQ_TIMEOUT };
    mel_geo_request(&g_tour.request);
    mel_task_init(&g_tour.request_task, tour_on_request);
    mel_future_then(&g_tour.request.future, &g_tour.request_task, mel_vat_executor(g_tour.root));

    g_tour.pending++;
    g_tour.watch = (Mel_Geo_Watch){
        .accuracy_m = TOUR_WATCH_ACC_M,
        .min_interval_ns = 1000000000ll,
        .cb = tour_on_watch_fix,
        .exec = mel_vat_executor(g_tour.root),
    };
    const mel_geo_result* wr = mel_geo_watch_start(&g_tour.watch);
    g_tour.watch_running = true;
    printf("watch      started (%s), %u fixes wanted\n", mel_geo_result_name(wr), (u32)TOUR_WATCH_FIXES);
    if (!mel_geo_result_ok(wr))
        tour_watch_finish();

    if (mel_geo_caps().heading)
    {
        g_tour.heading = (Mel_Geo_Heading_Watch){
            .cb = tour_on_heading,
            .exec = mel_vat_executor(g_tour.root),
        };
        const mel_geo_result* hr = mel_geo_heading_start(&g_tour.heading);
        g_tour.heading_running = mel_geo_result_ok(hr);
        printf("heading    %s\n", mel_geo_result_name(hr));
    }
    else
        printf("heading    unsupported here\n");
}

static void tour_on_auth(Mel_Task* task)
{
    (void)task;
    const mel_geo_auth* auth = mel_geo_future_auth(&g_tour.auth_future);
    printf("authorize  %s\n", mel_geo_auth_name(auth));
    if (mel_geo_auth_is_granted(auth))
        tour_start_live();
    else
        printf("live tour  skipped (not granted) — geocoding only\n");
    tour_forward_geocode();
    tour_stage_done();
}

void mel_app_setup(Mel_Vat* root)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    g_tour.root = root;
    mel_vat_retain(root);

    mel_geo_init(root);

    Mel_Geo_Caps caps = mel_geo_caps();
    printf("geo-tour   melody geolocation module demo\n");
    printf("caps       fixes:%d heading:%d regions-native:%d geocoding:%d background:%d\n", caps.fixes, caps.heading,
           caps.regions_native, caps.geocoding, caps.background);
    printf("auth now   %s\n", mel_geo_auth_name(mel_geo_authorization()));

    g_tour.pending = 1;
    mel_future_init(&g_tour.auth_future, NULL, NULL);
    mel_task_init(&g_tour.auth_task, tour_on_auth);
    mel_future_then(&g_tour.auth_future, &g_tour.auth_task, mel_vat_executor(root));
    mel_geo_authorize(&mel_geo_scope_in_use, &g_tour.auth_future);
}
