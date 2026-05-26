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
    // UIApplicationMain owns the main thread; the reactor (and the screen
    // construction it drives) runs on a dedicated thread. The GUI backend
    // marshals every UIKit touch back to the main thread.
    [NSThread detachNewThreadWithBlock:^{
        mel_reactor_spawn(MEL_REACTOR_THREADED, app_init, NULL);
    }];
    return YES;
}
@end

int mel_ios_app_main(int argc, char** argv)
{
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([MelAppDelegate class]));
    }
}
