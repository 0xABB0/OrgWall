#import <UIKit/UIKit.h>

#include <app/app.h>
#include <reactor/reactor.h>

static bool app_init(Mel_Reactor* reactor, void* user)
{
    (void)user;
    mel_app_setup(reactor);
    return true;
}

@interface MelAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation MelAppDelegate
- (BOOL)application:(UIApplication*)application
        didFinishLaunchingWithOptions:(NSDictionary*)options
{
    (void)application; (void)options;
    // Attach the reactor to the main run loop UIApplicationMain is already
    // running: it installs a tick and returns, so the reactor and all UIKit
    // work share the main thread. No extra thread, no cross-thread marshalling.
    mel_reactor_spawn(MEL_REACTOR_ATTACHED, app_init, NULL);
    return YES;
}
@end

int mel_ios_app_main(int argc, char** argv)
{
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([MelAppDelegate class]));
    }
}
