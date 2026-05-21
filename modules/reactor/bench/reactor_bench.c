#include <reactor/reactor.h>
#include <core/types.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define BENCH_ITERS 2000000u
#define BENCH_REPS  11

static volatile u64 g_sink;

static void drain_queue(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void bench_native(u64 iters)
{
    MSG  msg;
    u64  count   = 0;
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (++count >= iters) running = false;
    }
    g_sink += count;
}

static u64 g_reactor_count;
static u64 g_reactor_limit;

static bool reactor_frame(void* user)
{
    if (++g_reactor_count >= g_reactor_limit) mel_reactor_quit((Mel_Reactor*)user);
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

int main(void)
{
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadAffinityMask(GetCurrentThread(), 1);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    double tick_ns = 1.0e9 / (double)freq.QuadPart;

    if (!mel_reactor_init()) { printf("reactor init failed\n"); return 1; }
    Mel_Reactor* r = mel_reactor_system();
    g_reactor_limit = BENCH_ITERS;
    Mel_Reactor_Source* idle = mel_reactor_idle_new(reactor_frame, r);
    mel_reactor_source_attach(r, idle);

    bench_native(BENCH_ITERS);
    g_reactor_count = 0;
    mel_reactor_run(r);

    double native_ns[BENCH_REPS];
    double reactor_ns[BENCH_REPS];

    for (int rep = 0; rep < BENCH_REPS; rep++) {
        LARGE_INTEGER t0, t1;

        drain_queue();
        QueryPerformanceCounter(&t0);
        bench_native(BENCH_ITERS);
        QueryPerformanceCounter(&t1);
        native_ns[rep] = (double)(t1.QuadPart - t0.QuadPart) * tick_ns / (double)BENCH_ITERS;

        g_reactor_count = 0;
        drain_queue();
        QueryPerformanceCounter(&t0);
        mel_reactor_run(r);
        QueryPerformanceCounter(&t1);
        reactor_ns[rep] = (double)(t1.QuadPart - t0.QuadPart) * tick_ns / (double)BENCH_ITERS;
    }

    mel_reactor_shutdown();

    double nmin, nmean, nmax, rmin, rmean, rmax;
    stats(native_ns,  BENCH_REPS, &nmin, &nmean, &nmax);
    stats(reactor_ns, BENCH_REPS, &rmin, &rmean, &rmax);

    double overhead = rmin - nmin;
    double overhead_pct = overhead / nmin * 100.0;

    printf("Reactor abstraction vs native Win32 loop\n");
    printf("  workload : uncapped fast loop, trivial per-iteration work\n");
    printf("  per run  : %u iterations   runs: %d\n", BENCH_ITERS, BENCH_REPS);
    printf("  build    : -O2, no LTO; reactor is one TU, called across the library boundary\n");
    printf("  fairness : both loops drain the Win32 message queue once per iteration\n\n");

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
