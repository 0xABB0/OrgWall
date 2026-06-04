#include <core/platform.h>

#if !MEL_PLATFORM_OSX
#error "macos-only translation unit"
#endif

#import <AppKit/AppKit.h>

#include <app/provider.h>

@interface MelAppLifecycleObserver: NSObject
@end

@implementation MelAppLifecycleObserver
- (void)willTerminate:(NSNotification*)note
{
    (void)note;
    mel_app__emit(MEL_APP_PHASE_WILL_TERMINATE);
}
- (void)didBecomeActive:(NSNotification*)note
{
    (void)note;
    mel_app__emit(MEL_APP_PHASE_DID_BECOME_ACTIVE);
}
- (void)willResignActive:(NSNotification*)note
{
    (void)note;
    mel_app__emit(MEL_APP_PHASE_WILL_RESIGN_ACTIVE);
}
- (void)didHide:(NSNotification*)note
{
    (void)note;
    mel_app__emit(MEL_APP_PHASE_DID_ENTER_BACKGROUND);
}
- (void)willUnhide:(NSNotification*)note
{
    (void)note;
    mel_app__emit(MEL_APP_PHASE_WILL_ENTER_FOREGROUND);
}
@end

static MelAppLifecycleObserver* g_observer;

static void plat_start(void* user)
{
    (void)user;
    if (g_observer != nil)
        return;
    g_observer = [[MelAppLifecycleObserver alloc] init];
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:g_observer selector:@selector(willTerminate:) name:NSApplicationWillTerminateNotification object:nil];
    [nc addObserver:g_observer selector:@selector(didBecomeActive:) name:NSApplicationDidBecomeActiveNotification object:nil];
    [nc addObserver:g_observer selector:@selector(willResignActive:) name:NSApplicationWillResignActiveNotification object:nil];
    [nc addObserver:g_observer selector:@selector(didHide:) name:NSApplicationDidHideNotification object:nil];
    [nc addObserver:g_observer selector:@selector(willUnhide:) name:NSApplicationWillUnhideNotification object:nil];
}

static void plat_stop(void* user)
{
    (void)user;
    if (g_observer == nil)
        return;
    [[NSNotificationCenter defaultCenter] removeObserver:g_observer];
    g_observer = nil;
}

void mel_app__register_platform_provider(void)
{
    Mel_App_Provider_Desc desc = { .name = "macos-appkit", .start = plat_start, .stop = plat_stop };
    mel_app_provider_register(&desc);
}
