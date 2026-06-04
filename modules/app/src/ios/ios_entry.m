#import <UIKit/UIKit.h>

#include <app/app.h>
#include <app/subsystem.h>
#include <reactor/reactor.h>

static bool app_init(Mel_Reactor* reactor, void* user)
{
    (void)user;
    mel_app_init(.reactor = reactor);
    mel_app_setup(reactor);
    return true;
}

@interface MelAppDelegate: UIResponder <UIApplicationDelegate>
@end

@implementation MelAppDelegate
- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options
{
    (void)application;
    (void)options;
    mel_reactor_spawn(MEL_REACTOR_ATTACHED, app_init, NULL);
    return YES;
}
@end

int mel_ios_app_main(int argc, char** argv)
{
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([MelAppDelegate class]));
    }
}
