#include <core/platform.h>

#if !MEL_PLATFORM_OSX
#error "macos-only translation unit"
#endif

#import <AppKit/AppKit.h>

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

static MelBootLifecycleObserver* g_observer;

void mel_boot__lifecycle_platform_start(void)
{
    if (g_observer != nil)
        return;
    g_observer = [[MelBootLifecycleObserver alloc] init];
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:g_observer selector:@selector(willTerminate:) name:NSApplicationWillTerminateNotification object:nil];
    [nc addObserver:g_observer selector:@selector(didBecomeActive:) name:NSApplicationDidBecomeActiveNotification object:nil];
    [nc addObserver:g_observer selector:@selector(willResignActive:) name:NSApplicationWillResignActiveNotification object:nil];
    [nc addObserver:g_observer selector:@selector(didHide:) name:NSApplicationDidHideNotification object:nil];
    [nc addObserver:g_observer selector:@selector(willUnhide:) name:NSApplicationWillUnhideNotification object:nil];
}

void mel_boot__lifecycle_platform_stop(void)
{
    if (g_observer == nil)
        return;
    [[NSNotificationCenter defaultCenter] removeObserver:g_observer];
    g_observer = nil;
}
