#include <reactor/reactor.h>
#include <core/types.h>

#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <stdio.h>

#define BENCH_ITERS 2000000u
#define BENCH_REPS  11

static volatile u64 g_sink;
static double       g_tick_ns;

static u64 mono_ns(void)
{
    return (u64)((double)mach_absolute_time() * g_tick_ns);
}

static void native_wake_perform(void* info) { (void)info; }

static void bench_native(u64 iters)
{
    CFRunLoopRef           loop = CFRunLoopGetCurrent();
    CFRunLoopSourceContext ctx  = {0};
    ctx.perform = native_wake_perform;
    CFRunLoopSourceRef wake = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx);
    CFRunLoopAddSource(loop, wake, kCFRunLoopCommonModes);

    u64 count = 0;
    while (count < iters) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.0, true);
        count++;
    }

    CFRunLoopRemoveSource(loop, wake, kCFRunLoopCommonModes);
    CFRunLoopSourceInvalidate(wake);
    CFRelease(wake);

    g_sink += count;
}

static u64 g_reactor_count;
static u64 g_reactor_limit;

static bool reactor_frame(void* user)
{
    if (++g_reactor_count >= g_reactor_limit) mel_reactor_quit((Mel_Reactor*)user);
    return true;
}

static bool bench_setup(Mel_Reactor* r, void* user)
{
    (void)user;
    Mel_Reactor_Source* idle = mel_reactor_idle_new(reactor_frame, r);
    mel_reactor_source_attach(r, idle);
    return true;
}

static void stats(const double* v, int n, double* out_min, double* out_mean, double* out_max)
{
    double lo = v[0], hi = v[0], sum = 0.0;
    for (int i = 0; i < n; i++) {
        if (v[i] < lo) lo = v[i];
        if (v[i] > hi) hi = v[i];
        sum += v[i];
    }
    *out_min  = lo;
    *out_max  = hi;
    *out_mean = sum / (double)n;
}

static void pin_and_boost(void)
{
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    thread_affinity_policy_data_t aff = { 1 };
    thread_policy_set(pthread_mach_thread_np(pthread_self()),
                      THREAD_AFFINITY_POLICY,
                      (thread_policy_t)&aff,
                      THREAD_AFFINITY_POLICY_COUNT);
}

int main(void)
{
    pin_and_boost();

    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    g_tick_ns = (double)tb.numer / (double)tb.denom;

    g_reactor_limit = BENCH_ITERS;

    bench_native(BENCH_ITERS);
    g_reactor_count = 0;
    mel_reactor_spawn(MEL_REACTOR_THREADED, bench_setup, NULL);

    double native_ns[BENCH_REPS];
    double reactor_ns[BENCH_REPS];

    for (int rep = 0; rep < BENCH_REPS; rep++) {
        u64 t0, t1;

        t0 = mono_ns();
        bench_native(BENCH_ITERS);
        t1 = mono_ns();
        native_ns[rep] = (double)(t1 - t0) / (double)BENCH_ITERS;

        g_reactor_count = 0;
        t0 = mono_ns();
        mel_reactor_spawn(MEL_REACTOR_THREADED, bench_setup, NULL);
        t1 = mono_ns();
        reactor_ns[rep] = (double)(t1 - t0) / (double)BENCH_ITERS;
    }

    double nmin, nmean, nmax, rmin, rmean, rmax;
    stats(native_ns,  BENCH_REPS, &nmin, &nmean, &nmax);
    stats(reactor_ns, BENCH_REPS, &rmin, &rmean, &rmax);

    double overhead     = rmin - nmin;
    double overhead_pct = overhead / nmin * 100.0;

    printf("Reactor abstraction vs native CFRunLoop\n");
    printf("  workload : uncapped fast loop, trivial per-iteration work\n");
    printf("  per run  : %u iterations   runs: %d\n", BENCH_ITERS, BENCH_REPS);
    printf("  build    : -O2, no LTO; reactor is one TU, called across the library boundary\n");
    printf("  fairness : both loops call CFRunLoopRunInMode(default, 0.0, true) once per\n");
    printf("             iteration against a loop with one no-op wake source attached\n");
    printf("  caveats  : macOS affinity is advisory; QoS raised to USER_INTERACTIVE\n\n");

    printf("                  min        mean         max\n");
    printf("  native     %8.2f   %9.2f   %9.2f   ns/iter\n", nmin, nmean, nmax);
    printf("  reactor    %8.2f   %9.2f   %9.2f   ns/iter\n\n", rmin, rmean, rmax);

    printf("  abstraction overhead : %.2f ns/iter  (%.1f%% over native)\n", overhead, overhead_pct);
    printf("  native  throughput   : %.1f M iters/s\n", 1000.0 / nmin);
    printf("  reactor throughput   : %.1f M iters/s\n\n", 1000.0 / rmin);

    printf("  overhead as a share of one frame:\n");
    printf("       60 Hz (16.667 ms) : %.5f %%\n", overhead / 16.667e6 * 100.0);
    printf("      240 Hz ( 4.167 ms) : %.5f %%\n", overhead / 4.167e6  * 100.0);
    printf("     1000 Hz ( 1.000 ms) : %.5f %%\n", overhead / 1.000e6  * 100.0);

    g_sink += g_reactor_count;
    return 0;
}
