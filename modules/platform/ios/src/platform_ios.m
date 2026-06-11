#include <core/platform.h>

#if !MEL_PLATFORM_IOS
#error "ios-only translation unit"
#endif

#include <platform/platform.h>
#include <platform/ios/ios.h>
#include "../../src/platform_internal.h"

#import <UIKit/UIKit.h>

static const char* ios_name(void) { return "ios"; }

static u32 ios_device_class(void)
{
    UIUserInterfaceIdiom idiom = [[UIDevice currentDevice] userInterfaceIdiom];
    switch (idiom)
    {
    case UIUserInterfaceIdiomPad:
        return MEL_PLATFORM_DEVICE_TABLET;
    case UIUserInterfaceIdiomTV:
        return MEL_PLATFORM_DEVICE_TV;
    default:
        return MEL_PLATFORM_DEVICE_PHONE;
    }
}

static Mel_Platform_Sandbox ios_sandbox(void) { return (Mel_Platform_Sandbox){ MEL_PLATFORM_SANDBOX_APPLE, NULL }; }

static Mel_Platform_Inhibit_Native ios_inhibit(const char* reason)
{
    (void)reason;
    [UIApplication sharedApplication].idleTimerDisabled = YES;
    return (Mel_Platform_Inhibit_Native){ MEL_PLATFORM_OK, 1 };
}

static Mel_Platform_Status ios_uninhibit(u64 native)
{
    (void)native;
    [UIApplication sharedApplication].idleTimerDisabled = NO;
    return MEL_PLATFORM_OK;
}

const Mel_Platform_Backend* mel_platform__backend(void)
{
    static const Mel_Platform_Backend backend = {
        .name = ios_name,
        .device_class = ios_device_class,
        .sandbox = ios_sandbox,
        .screensaver_inhibit = ios_inhibit,
        .screensaver_uninhibit = ios_uninhibit,
    };
    return &backend;
}

@interface                                         MelPlatformDisplayLink: NSObject
@property(nonatomic) Mel_Platform_iOS_Animation_Cb cb;
@property(nonatomic) void*                         user;
@property(nonatomic, strong) CADisplayLink*        link;
- (void)tick:(CADisplayLink*)sender;
@end

@implementation MelPlatformDisplayLink
- (void)tick:(CADisplayLink*)sender
{
    if (self.cb)
        self.cb(sender.timestamp, self.user);
}
@end

static MelPlatformDisplayLink* g_display_link;
static bool                    g_event_pump = true;

Mel_Platform_Status mel_platform_ios_set_animation_callback(u32 interval_frames, Mel_Platform_iOS_Animation_Cb cb, void* user)
{
    if (cb == NULL)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_INVALID;
    if (g_display_link)
        mel_platform_ios_clear_animation_callback();
    g_display_link = [[MelPlatformDisplayLink alloc] init];
    g_display_link.cb = cb;
    g_display_link.user = user;
    g_display_link.link = [CADisplayLink displayLinkWithTarget:g_display_link selector:@selector(tick:)];
    g_display_link.link.preferredFramesPerSecond = interval_frames ? (NSInteger)(60 / interval_frames) : 0;
    [g_display_link.link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    return MEL_PLATFORM_OK;
}

Mel_Platform_Status mel_platform_ios_clear_animation_callback(void)
{
    if (g_display_link == nil)
        return MEL_PLATFORM_ERROR | MEL_PLATFORM_INVALID;
    [g_display_link.link invalidate];
    g_display_link = nil;
    return MEL_PLATFORM_OK;
}

Mel_Platform_Status mel_platform_ios_set_event_pump(bool enabled)
{
    g_event_pump = enabled;
    return MEL_PLATFORM_OK;
}

bool mel_platform_ios_event_pump_enabled(void) { return g_event_pump; }
