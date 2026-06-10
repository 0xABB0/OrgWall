#include <core/platform.h>

#if !MEL_PLATFORM_IOS
#error "ios-only translation unit"
#endif

#import <UIKit/UIKit.h>

#include "../boot_internal.h"

#include <boot/lifecycle.h>

@interface MelBootLifecycleObserver: NSObject
@end

@implementation MelBootLifecycleObserver
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
- (void)didEnterBackground:(NSNotification*)note
{
    (void)note;
    mel_app__emit(MEL_APP_PHASE_DID_ENTER_BACKGROUND);
}
- (void)willEnterForeground:(NSNotification*)note
{
    (void)note;
    mel_app__emit(MEL_APP_PHASE_WILL_ENTER_FOREGROUND);
}
- (void)didReceiveMemoryWarning:(NSNotification*)note
{
    (void)note;
    mel_app__emit(MEL_APP_PHASE_LOW_MEMORY);
}
@end

static MelBootLifecycleObserver* g_observer;

void mel_boot__lifecycle_platform_start(void)
{
    if (g_observer != nil)
        return;
    g_observer = [[MelBootLifecycleObserver alloc] init];
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:g_observer selector:@selector(willTerminate:) name:UIApplicationWillTerminateNotification object:nil];
    [nc addObserver:g_observer selector:@selector(didBecomeActive:) name:UIApplicationDidBecomeActiveNotification object:nil];
    [nc addObserver:g_observer selector:@selector(willResignActive:) name:UIApplicationWillResignActiveNotification object:nil];
    [nc addObserver:g_observer selector:@selector(didEnterBackground:) name:UIApplicationDidEnterBackgroundNotification object:nil];
    [nc addObserver:g_observer selector:@selector(willEnterForeground:) name:UIApplicationWillEnterForegroundNotification object:nil];
    [nc addObserver:g_observer selector:@selector(didReceiveMemoryWarning:) name:UIApplicationDidReceiveMemoryWarningNotification object:nil];
}

void mel_boot__lifecycle_platform_stop(void)
{
    if (g_observer == nil)
        return;
    [[NSNotificationCenter defaultCenter] removeObserver:g_observer];
    g_observer = nil;
}
