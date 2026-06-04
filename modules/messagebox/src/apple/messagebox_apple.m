#include <messagebox/backend.h>
#include <log/log.h>

#import <TargetConditionals.h>
#import <Foundation/Foundation.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#else
#import <UIKit/UIKit.h>
#endif

static NSString* ns_from_str8(str8 s)
{
    if (s.len <= 0 || !s.data)
        return @"";
    return [[NSString alloc] initWithBytes:s.data length:(NSUInteger)s.len encoding:NSUTF8StringEncoding] ?: @"";
}

bool mel_msgbox__plat_available(void) { return true; }

#if TARGET_OS_OSX

static NSAlertStyle alert_style(Mel_Msgbox_Severity sev)
{
    switch (sev)
    {
        case MEL_MSGBOX_SEVERITY_WARN:  return NSAlertStyleWarning;
        case MEL_MSGBOX_SEVERITY_ERROR: return NSAlertStyleCritical;
        default:                        return NSAlertStyleInformational;
    }
}

Mel_Msgbox_Status mel_msgbox__plat_show(const Mel_Msgbox_Request* req, i32* out_chosen_id)
{
    Mel_Msgbox_Status warn = 0;
    __block i32       chosen = req->default_id;
    __block Mel_Msgbox_Status st = MEL_MSGBOX_OK;

    void (^body)(void) = ^{
        @autoreleasepool
        {
            [NSApplication sharedApplication];
            NSAlert* alert = [[NSAlert alloc] init];
            alert.alertStyle = alert_style(req->severity);
            alert.messageText = ns_from_str8(req->title.len > 0 ? req->title : req->message);
            alert.informativeText = req->title.len > 0 ? ns_from_str8(req->message) : @"";

            NSInteger base = NSAlertFirstButtonReturn;
            for (u32 i = 0; i < req->button_count; i++)
            {
                NSString*  label = req->buttons[i].label.len > 0 ? ns_from_str8(req->buttons[i].label) : @"OK";
                NSButton*  bt = [alert addButtonWithTitle:label];
                if (req->buttons[i].id == req->escape_id)
                    bt.keyEquivalent = @"\033";
                else if (req->buttons[i].id == req->default_id)
                    bt.keyEquivalent = @"\r";
            }

            NSInteger response = [alert runModal];
            NSInteger idx = response - base;
            if (idx >= 0 && (u32)idx < req->button_count)
                chosen = req->buttons[idx].id;
            else
                st |= MEL_MSGBOX_RESULT_DISMISSED;
        }
    };

    if ([NSThread isMainThread])
        body();
    else
        dispatch_sync(dispatch_get_main_queue(), body);

    if (req->accent.has_value || req->text.has_value || req->background.has_value)
        warn |= MEL_MSGBOX_WARN_COLOR_DROPPED;
    if (req->right_to_left)
        warn |= MEL_MSGBOX_WARN_RTL_DROPPED;

    *out_chosen_id = chosen;
    return st | warn | (warn ? MEL_MSGBOX_WARNED : MEL_MSGBOX_OK);
}

#else

static UIViewController* top_view_controller(void)
{
    UIWindow* key = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
    {
        if (scene.activationState == UISceneActivationStateForegroundActive && [scene isKindOfClass:[UIWindowScene class]])
        {
            for (UIWindow* w in ((UIWindowScene*)scene).windows)
                if (w.isKeyWindow)
                {
                    key = w;
                    break;
                }
        }
        if (key)
            break;
    }
    UIViewController* vc = key.rootViewController;
    while (vc.presentedViewController)
        vc = vc.presentedViewController;
    return vc;
}

Mel_Msgbox_Status mel_msgbox__plat_show(const Mel_Msgbox_Request* req, i32* out_chosen_id)
{
    Mel_Msgbox_Status warn = 0;
    __block i32       chosen = req->default_id;
    __block Mel_Msgbox_Status st = MEL_MSGBOX_OK;
    __block bool      done = false;

    UIViewController* host = top_view_controller();
    if (!host)
    {
        mel_log_error("messagebox", "no foreground view controller to present alert");
        *out_chosen_id = req->escape_id;
        return MEL_MSGBOX_ERROR | MEL_MSGBOX_RESULT_NO_BACKEND;
    }

    UIAlertController* alert = [UIAlertController alertControllerWithTitle:ns_from_str8(req->title)
                                                                 message:ns_from_str8(req->message)
                                                          preferredStyle:UIAlertControllerStyleAlert];
    for (u32 i = 0; i < req->button_count; i++)
    {
        i32                 id = req->buttons[i].id;
        NSString*           label = req->buttons[i].label.len > 0 ? ns_from_str8(req->buttons[i].label) : @"OK";
        UIAlertActionStyle  style = (id == req->escape_id) ? UIAlertActionStyleCancel
                                  : (req->severity == MEL_MSGBOX_SEVERITY_ERROR ? UIAlertActionStyleDestructive : UIAlertActionStyleDefault);
        UIAlertAction*      act = [UIAlertAction actionWithTitle:label
                                                          style:style
                                                        handler:^(UIAlertAction* a) {
                                                            (void)a;
                                                            chosen = id;
                                                            done = true;
                                                        }];
        [alert addAction:act];
        if (id == req->default_id)
            alert.preferredAction = act;
    }

    if (req->accent.has_value)
        alert.view.tintColor = [UIColor colorWithRed:req->accent.value.r / 255.0 green:req->accent.value.g / 255.0 blue:req->accent.value.b / 255.0 alpha:req->accent.value.a / 255.0];
    if (req->text.has_value || req->background.has_value)
        warn |= MEL_MSGBOX_WARN_COLOR_DROPPED;
    if (req->right_to_left)
        alert.view.semanticContentAttribute = UISemanticContentAttributeForceRightToLeft;

    [host presentViewController:alert animated:NO completion:nil];
    while (!done)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];

    *out_chosen_id = chosen;
    return st | warn | (warn ? MEL_MSGBOX_WARNED : MEL_MSGBOX_OK);
}

#endif
