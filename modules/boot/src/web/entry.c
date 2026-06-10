#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
#error "web/wasm-only translation unit"
#endif

#include "../boot_internal.h"

#include <allocator/heap.h>
#include <boot/boot.h>
#include <vat/vat.h>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <stdatomic.h>

static Mel_Vat_Embedder g_host;
static Mel_Vat*         g_root;
static Mel_Vat_Waiter*  g_waiter;
static Mel_Vat_Driver*  g_driver;
static atomic_bool      g_informed;

static void boot_web__drive(void* user);

#ifdef __EMSCRIPTEN_PTHREADS__
#include <emscripten/threading.h>

static void boot_web__drive_main(void) { boot_web__drive(NULL); }
#endif

static void boot_web__schedule_work(Mel_Vat_Embedder* embedder)
{
    MEL_UNUSED(embedder);
    atomic_store_explicit(&g_informed, true, memory_order_seq_cst);
#ifdef __EMSCRIPTEN_PTHREADS__
    if (!emscripten_is_main_runtime_thread())
    {
        emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_V, boot_web__drive_main);
        return;
    }
#endif
    emscripten_async_call(boot_web__drive, NULL, 0);
}

static void boot_web__schedule_delayed_work(Mel_Vat_Embedder* embedder, i64 delay_ns)
{
    MEL_UNUSED(embedder);
    atomic_store_explicit(&g_informed, true, memory_order_seq_cst);
    if (delay_ns >= 0)
        emscripten_set_timeout(boot_web__drive, (double)delay_ns / 1e6, NULL);
}

static const Mel_Vat_Embedder_Vtbl boot_web__embedder_vtbl = {
    boot_web__schedule_work,
    boot_web__schedule_delayed_work,
    NULL,
};

static void boot_web__drive(void* user)
{
    MEL_UNUSED(user);
    atomic_store_explicit(&g_informed, false, memory_order_seq_cst);
    bool live = mel_vat_step(g_root);
    if (!live)
    {
        int code = mel_boot__finish();
        mel_vat_close(g_root);
        g_driver->vt->close(g_driver);
        g_waiter->vt->close(g_waiter);
        emscripten_force_exit(code);
        return;
    }
    if (!atomic_load_explicit(&g_informed, memory_order_seq_cst))
        emscripten_async_call(boot_web__drive, NULL, 0);
}

int main(int argc, char** argv)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_boot__init(argc, argv, alloc);

    g_host.vt = &boot_web__embedder_vtbl;
    atomic_init(&g_informed, false);
    g_waiter = mel_vat_waiter_guest(alloc, &g_host);
    g_driver = mel_vat_driver_fair(alloc, 64);
    g_root = mel_vat_open(alloc, (Mel_Vat_Desc){ .waiter = g_waiter, .driver = g_driver });

    mel_app_setup(g_root);
    boot_web__drive(NULL);
    emscripten_exit_with_live_runtime();
    return 0;
}
