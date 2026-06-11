#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include "../../src/boot_internal.h"

#include <allocator/heap.h>
#include <boot/boot.h>
#include <boot/lifecycle.h>
#include <core/types.h>
#include <vat/vat.h>

#include <android/looper.h>
#include <jni.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

static Mel_Vat_Embedder g_host;
static Mel_Vat*         g_root;
static Mel_Vat_Waiter*  g_waiter;
static Mel_Vat_Driver*  g_driver;
static atomic_bool      g_informed;
static int              g_wake_fd = -1;
static int              g_timer_fd = -1;

static void boot_android__drive(void);

static int boot_android__fd_cb(int fd, int events, void* data)
{
    (void)events;
    (void)data;
    u64 v;
    while (read(fd, &v, sizeof v) == (ssize_t)sizeof v)
        ;
    boot_android__drive();
    return 1;
}

static void boot_android__ring(void)
{
    u64     one = 1;
    ssize_t n = write(g_wake_fd, &one, sizeof one);
    (void)n;
}

static void boot_android__schedule_work(Mel_Vat_Embedder* embedder)
{
    (void)embedder;
    atomic_store_explicit(&g_informed, true, memory_order_seq_cst);
    boot_android__ring();
}

static void boot_android__schedule_delayed_work(Mel_Vat_Embedder* embedder, i64 delay_ns)
{
    (void)embedder;
    atomic_store_explicit(&g_informed, true, memory_order_seq_cst);
    if (delay_ns < 0)
        return;
    if (delay_ns == 0)
        delay_ns = 1;
    struct itimerspec it = { 0 };
    it.it_value.tv_sec = delay_ns / 1000000000ll;
    it.it_value.tv_nsec = delay_ns % 1000000000ll;
    timerfd_settime(g_timer_fd, 0, &it, NULL);
}

static const Mel_Vat_Embedder_Vtbl boot_android__embedder_vtbl = {
    boot_android__schedule_work,
    boot_android__schedule_delayed_work,
    NULL,
};

static void boot_android__drive(void)
{
    if (g_root == NULL)
        return;
    atomic_store_explicit(&g_informed, false, memory_order_seq_cst);
    bool live = mel_vat_step(g_root);
    if (!live)
    {
        int code = mel_boot__finish();
        mel_boot__lifecycle_shutdown();
        mel_vat_close(g_root);
        g_driver->vt->close(g_driver);
        g_waiter->vt->close(g_waiter);
        g_root = NULL;
        exit(code);
    }
    if (!atomic_load_explicit(&g_informed, memory_order_seq_cst))
        boot_android__ring();
}

void mel_boot__lifecycle_platform_start(void) {}
void mel_boot__lifecycle_platform_stop(void) {}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeStart(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    if (g_root != NULL)
        return;

    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_boot__init(0, NULL, alloc);

    g_wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    g_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    ALooper* looper = ALooper_forThread();
    ALooper_addFd(looper, g_wake_fd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, boot_android__fd_cb, NULL);
    ALooper_addFd(looper, g_timer_fd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, boot_android__fd_cb, NULL);

    g_host.vt = &boot_android__embedder_vtbl;
    atomic_init(&g_informed, false);
    g_waiter = mel_vat_waiter_guest(alloc, &g_host);
    g_driver = mel_vat_driver_fair(alloc, 64);
    g_root = mel_vat_open(alloc, (Mel_Vat_Desc){ .waiter = g_waiter, .driver = g_driver });

    mel_boot__lifecycle_init(g_root, alloc);
    mel_app_setup(g_root);
    boot_android__drive();
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeStop(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    if (g_root == NULL)
        return;
    mel_vat_quit(g_root);
    boot_android__drive();
}

static void boot_android__emit(u32 phase)
{
    if (g_root == NULL)
        return;
    mel_app__emit(phase);
    boot_android__drive();
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnResume(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    boot_android__emit(MEL_APP_PHASE_WILL_ENTER_FOREGROUND | MEL_APP_PHASE_DID_BECOME_ACTIVE);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnPause(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    boot_android__emit(MEL_APP_PHASE_WILL_RESIGN_ACTIVE);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnStop(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    boot_android__emit(MEL_APP_PHASE_DID_ENTER_BACKGROUND);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnDestroy(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    boot_android__emit(MEL_APP_PHASE_WILL_TERMINATE);
}

JNIEXPORT void JNICALL Java_orgwall_melody_platform_MelGui_nativeOnLowMemory(JNIEnv* env, jclass cls)
{
    (void)env;
    (void)cls;
    boot_android__emit(MEL_APP_PHASE_LOW_MEMORY);
}
