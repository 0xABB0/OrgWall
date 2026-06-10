#include <core/platform.h>

#if !MEL_PLATFORM_IOS
#error "ios-only translation unit"
#endif

#import <UIKit/UIKit.h>

#include "../boot_internal.h"

#include <allocator/heap.h>
#include <boot/boot.h>
#include <vat/vat.h>

#include <stdatomic.h>
#include <stdlib.h>

static Mel_Vat_Embedder g_host;
static Mel_Vat*         g_root;
static Mel_Vat_Waiter*  g_waiter;
static Mel_Vat_Driver*  g_driver;
static atomic_bool      g_informed;
static int              g_argc;
static char**           g_argv;

static void boot_ios__drive(void);

static void boot_ios__schedule_work(Mel_Vat_Embedder* embedder)
{
    MEL_UNUSED(embedder);
    atomic_store_explicit(&g_informed, true, memory_order_seq_cst);
    dispatch_async(dispatch_get_main_queue(), ^{
        boot_ios__drive();
    });
}

static void boot_ios__schedule_delayed_work(Mel_Vat_Embedder* embedder, i64 delay_ns)
{
    MEL_UNUSED(embedder);
    atomic_store_explicit(&g_informed, true, memory_order_seq_cst);
    if (delay_ns < 0)
        return;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay_ns), dispatch_get_main_queue(), ^{
        boot_ios__drive();
    });
}

static const Mel_Vat_Embedder_Vtbl boot_ios__embedder_vtbl = {
    boot_ios__schedule_work,
    boot_ios__schedule_delayed_work,
    NULL,
};

static void boot_ios__drive(void)
{
    atomic_store_explicit(&g_informed, false, memory_order_seq_cst);
    bool live = mel_vat_step(g_root);
    if (!live)
    {
        int code = mel_boot__finish();
        mel_boot__lifecycle_shutdown();
        mel_vat_close(g_root);
        g_driver->vt->close(g_driver);
        g_waiter->vt->close(g_waiter);
        exit(code);
    }
    if (!atomic_load_explicit(&g_informed, memory_order_seq_cst))
        dispatch_async(dispatch_get_main_queue(), ^{
            boot_ios__drive();
        });
}

@interface MelBootAppDelegate: UIResponder <UIApplicationDelegate>
@end

@implementation MelBootAppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options
{
    (void)application;
    (void)options;
    const Mel_Alloc* alloc = mel_alloc_heap();
    mel_boot__init(g_argc, g_argv, alloc);

    g_host.vt = &boot_ios__embedder_vtbl;
    atomic_init(&g_informed, false);
    g_waiter = mel_vat_waiter_guest(alloc, &g_host);
    g_driver = mel_vat_driver_fair(alloc, 64);
    g_root = mel_vat_open(alloc, (Mel_Vat_Desc){ .waiter = g_waiter, .driver = g_driver });

    mel_boot__lifecycle_init(g_root, alloc);
    mel_app_setup(g_root);
    boot_ios__drive();
    return YES;
}
@end

int main(int argc, char** argv)
{
    g_argc = argc;
    g_argv = argv;
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([MelBootAppDelegate class]));
    }
}
