#include <notification/notification.h>
#include <notification/provider.h>

#include <log/log.h>

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include "../notification_internal.h"

static const mel_notif_auth* volatile apple_auth_cache = &mel_notif_auth_not_determined;
static NSMutableSet<UNNotificationCategory*>* apple_categories;
static bool apple_delegate_installed;

@interface MelNotifDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

static u64 request_token(NSDictionary* user_info)
{
    NSNumber* n = user_info[@"melody.token"];
    return n != nil ? n.unsignedLongLongValue : 0;
}

static str8 nsdata_view(NSData* d)
{
    if (d == nil || d.length == 0)
        return STR8_EMPTY;
    return (str8){ (u8*)d.bytes, (size)d.length };
}

static str8 nsstring_view(NSString* s)
{
    if (s == nil)
        return STR8_EMPTY;
    const char* utf8 = s.UTF8String;
    return (str8){ (u8*)utf8, (size)strlen(utf8) };
}

@implementation MelNotifDelegate

- (void)userNotificationCenter:(UNUserNotificationCenter*)center willPresentNotification:(UNNotification*)notification withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
{
    u64 token = request_token(notification.request.content.userInfo);
    dispatch_async(dispatch_get_main_queue(), ^{
        mel_notif__dispatch_presented(token);
    });
    UNNotificationPresentationOptions opts;
    if (@available(macOS 11.0, iOS 14.0, *))
        opts = UNNotificationPresentationOptionList | UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionSound;
    else
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        opts = UNNotificationPresentationOptionAlert | UNNotificationPresentationOptionSound;
#pragma clang diagnostic pop
    }
    completionHandler(opts);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center didReceiveNotificationResponse:(UNNotificationResponse*)response withCompletionHandler:(void (^)(void))completionHandler
{
    NSDictionary* info = response.notification.request.content.userInfo;
    u64           token = request_token(info);
    NSData*       payload = info[@"melody.payload"];
    NSString*     action = response.actionIdentifier;
    NSString*     reply = nil;
    if ([response isKindOfClass:[UNTextInputNotificationResponse class]])
        reply = ((UNTextInputNotificationResponse*)response).userText;

    bool dismissed = [action isEqualToString:UNNotificationDismissActionIdentifier];
    bool plain_tap = [action isEqualToString:UNNotificationDefaultActionIdentifier];
    NSString* action_kept = (dismissed || plain_tap) ? nil : action;
    NSString* reply_kept = reply;
    NSData*   payload_kept = payload;
    u64       token_kept = token;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (dismissed)
        {
            mel_notif__dispatch_dismissed(token_kept);
        }
        else
        {
            str8 reply_s = reply_kept != nil ? nsstring_view(reply_kept) : STR8_EMPTY;
            mel_notif__dispatch_activated(token_kept, nsstring_view(action_kept), reply_s, nsdata_view(payload_kept));
        }
    });
    completionHandler();
}

@end

static MelNotifDelegate* apple_delegate;

static bool apple_supported(void* user)
{
    MEL_UNUSED(user);
    return NSBundle.mainBundle.bundleIdentifier != nil;
}

static Mel_Notif_Caps apple_caps(void* user)
{
    MEL_UNUSED(user);
    return MEL_NOTIF_CAP_ACTIONS | MEL_NOTIF_CAP_REPLY | MEL_NOTIF_CAP_ATTACHMENT | MEL_NOTIF_CAP_BADGE | MEL_NOTIF_CAP_SOUND | MEL_NOTIF_CAP_SCHEDULE | MEL_NOTIF_CAP_SCHEDULE_PERSISTS | MEL_NOTIF_CAP_REPEAT | MEL_NOTIF_CAP_UPDATE | MEL_NOTIF_CAP_AUTH;
}

static const mel_notif_auth* map_settings_status(UNAuthorizationStatus s)
{
    if (s == UNAuthorizationStatusAuthorized)
        return &mel_notif_auth_granted;
    if (s == UNAuthorizationStatusProvisional)
        return &mel_notif_auth_provisional;
    if (s == UNAuthorizationStatusDenied)
        return &mel_notif_auth_denied;
    return &mel_notif_auth_not_determined;
}

static void apple_refresh_auth(void)
{
    [[UNUserNotificationCenter currentNotificationCenter] getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* settings) {
        const mel_notif_auth* a = map_settings_status(settings.authorizationStatus);
        apple_auth_cache = a;
        dispatch_async(dispatch_get_main_queue(), ^{
            mel_notif__dispatch_auth_changed(a);
        });
    }];
}

static const mel_notif_auth* apple_authorization(void* user)
{
    MEL_UNUSED(user);
    return apple_auth_cache;
}

static void apple_authorize(void* user, Mel_Notif_Sink sink)
{
    MEL_UNUSED(user);
    UNAuthorizationOptions opts = UNAuthorizationOptionAlert | UNAuthorizationOptionSound | UNAuthorizationOptionBadge;
    [[UNUserNotificationCenter currentNotificationCenter] requestAuthorizationWithOptions:opts
                                                                        completionHandler:^(BOOL granted, NSError* error) {
                                                                            if (error != nil)
                                                                                mel_log_error("notification", "requestAuthorization failed: %s", error.localizedDescription.UTF8String);
                                                                            const mel_notif_auth* a = granted ? &mel_notif_auth_granted : &mel_notif_auth_denied;
                                                                            apple_auth_cache = a;
                                                                            dispatch_async(dispatch_get_main_queue(), ^{
                                                                                sink.on_auth(sink.token, a);
                                                                            });
                                                                        }];
}

static NSString* category_identifier(const Mel_Notif_Content* c)
{
    NSMutableString* ident = [NSMutableString stringWithString:@"melody"];
    for (u32 i = 0; i < c->action_count; i++)
    {
        const Mel_Notif_Action* a = &c->actions[i];
        [ident appendFormat:@"|%.*s;%.*s;%u", (int)a->id.len, a->id.data, (int)a->label.len, a->label.data, a->flags];
    }
    return ident;
}

static NSString* ensure_category(const Mel_Notif_Content* c)
{
    NSString* ident = category_identifier(c);
    if (apple_categories == nil)
        apple_categories = [NSMutableSet set];
    for (UNNotificationCategory* cat in apple_categories)
        if ([cat.identifier isEqualToString:ident])
            return ident;

    NSMutableArray<UNNotificationAction*>* actions = [NSMutableArray array];
    for (u32 i = 0; i < c->action_count; i++)
    {
        const Mel_Notif_Action* a = &c->actions[i];
        NSString*               aid = [[NSString alloc] initWithBytes:a->id.data length:(NSUInteger)a->id.len encoding:NSUTF8StringEncoding];
        NSString*               label = [[NSString alloc] initWithBytes:a->label.data length:(NSUInteger)a->label.len encoding:NSUTF8StringEncoding];
        UNNotificationActionOptions opts = 0;
        if ((a->flags & MEL_NOTIF_ACTION_FOREGROUND) != 0)
            opts |= UNNotificationActionOptionForeground;
        if ((a->flags & MEL_NOTIF_ACTION_DESTRUCTIVE) != 0)
            opts |= UNNotificationActionOptionDestructive;
        if ((a->flags & MEL_NOTIF_ACTION_TEXT_INPUT) != 0)
        {
            NSString* placeholder = a->input_placeholder.len > 0 ? [[NSString alloc] initWithBytes:a->input_placeholder.data length:(NSUInteger)a->input_placeholder.len encoding:NSUTF8StringEncoding] : @"";
            [actions addObject:[UNTextInputNotificationAction actionWithIdentifier:aid title:label options:opts textInputButtonTitle:label textInputPlaceholder:placeholder]];
        }
        else
        {
            [actions addObject:[UNNotificationAction actionWithIdentifier:aid title:label options:opts]];
        }
    }
    UNNotificationCategory* cat = [UNNotificationCategory categoryWithIdentifier:ident actions:actions intentIdentifiers:@[] options:UNNotificationCategoryOptionCustomDismissAction];
    [apple_categories addObject:cat];
    [[UNUserNotificationCenter currentNotificationCenter] setNotificationCategories:apple_categories];
    return ident;
}

static NSString* mel_nsstring(str8 s)
{
    if (s.len == 0 || s.data == NULL)
        return nil;
    return [[NSString alloc] initWithBytes:s.data length:(NSUInteger)s.len encoding:NSUTF8StringEncoding];
}

static Mel_Notif_Status apple_post(void* user, const Mel_Notif_Lowered* lw)
{
    MEL_UNUSED(user);
    const Mel_Notif_Content* c = lw->content;
    Mel_Notif_Status         warn = 0;

    UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
    content.title = mel_nsstring(c->title) ?: @"";
    if (c->subtitle.len > 0)
        content.subtitle = mel_nsstring(c->subtitle);
    if (c->body.len > 0)
        content.body = mel_nsstring(c->body);
    if (c->group.len > 0)
        content.threadIdentifier = mel_nsstring(c->group);
    if (c->has_badge)
        content.badge = @(c->badge);
    if (c->silent)
        content.sound = nil;
    else if (c->sound_path.len > 0)
        content.sound = [UNNotificationSound soundNamed:mel_nsstring(c->sound_path).lastPathComponent];
    else
        content.sound = UNNotificationSound.defaultSound;

    NSMutableDictionary* info = [NSMutableDictionary dictionary];
    info[@"melody.token"] = @(lw->token);
    if (c->payload.len > 0)
        info[@"melody.payload"] = [NSData dataWithBytes:c->payload.data length:(NSUInteger)c->payload.len];
    content.userInfo = info;

    if (c->action_count > 0)
        content.categoryIdentifier = ensure_category(c);

    if (c->attachment.path.len > 0)
    {
        NSURL*                    url = [NSURL fileURLWithPath:mel_nsstring(c->attachment.path)];
        NSError*                  err = nil;
        UNNotificationAttachment* att = [UNNotificationAttachment attachmentWithIdentifier:@"melody.attachment" URL:url options:nil error:&err];
        if (att != nil)
            content.attachments = @[ att ];
        else
        {
            mel_log_warn("notification", "attachment '%.*s' rejected: %s", (int)c->attachment.path.len, c->attachment.path.data, err != nil ? err.localizedDescription.UTF8String : "?");
            warn |= MEL_NOTIF_WARN_IMAGE_DROPPED;
        }
    }
    else if (c->attachment.rgba != NULL)
    {
        mel_log_warn("notification", "raw rgba attachments unsupported on apple; pass a file path");
        warn |= MEL_NOTIF_WARN_IMAGE_DROPPED;
    }
    if (c->icon.path.len > 0 || c->icon.rgba != NULL)
        warn |= MEL_NOTIF_WARN_IMAGE_DROPPED;
    if (c->progress.present)
        warn |= MEL_NOTIF_WARN_PROGRESS_DROPPED;

    UNNotificationTrigger* trigger = nil;
    if (lw->scheduled)
    {
        if (lw->trigger.interval_ms > 0)
        {
            f64 seconds = (f64)lw->trigger.interval_ms / 1000.0;
            if (seconds < 60.0)
            {
                seconds = 60.0;
                warn |= MEL_NOTIF_WARN_REPEAT_CLAMPED;
                mel_log_warn("notification", "repeat interval clamped to 60s (apple minimum)");
            }
            if (lw->trigger.at_unix_ms > 0)
                mel_log_warn("notification", "apple triggers cannot combine absolute time and repeat; using repeat interval only");
            trigger = [UNTimeIntervalNotificationTrigger triggerWithTimeInterval:seconds repeats:YES];
        }
        else
        {
            f64 now = [NSDate date].timeIntervalSince1970;
            f64 delta = (f64)lw->trigger.at_unix_ms / 1000.0 - now;
            if (delta < 0.1)
                delta = 0.1;
            trigger = [UNTimeIntervalNotificationTrigger triggerWithTimeInterval:delta repeats:NO];
        }
    }

    NSString*              ident = [NSString stringWithFormat:@"melody.%llu", (unsigned long long)lw->token];
    UNNotificationRequest* request = [UNNotificationRequest requestWithIdentifier:ident content:content trigger:trigger];
    [[UNUserNotificationCenter currentNotificationCenter] addNotificationRequest:request
                                                           withCompletionHandler:^(NSError* error) {
                                                               if (error != nil)
                                                                   mel_log_error("notification", "addNotificationRequest failed: %s", error.localizedDescription.UTF8String);
                                                           }];
    return warn != 0 ? (MEL_NOTIF_WARNED | warn) : MEL_NOTIF_OK;
}

static void apple_cancel(void* user, u64 token)
{
    MEL_UNUSED(user);
    NSString* ident = [NSString stringWithFormat:@"melody.%llu", (unsigned long long)token];
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    [center removePendingNotificationRequestsWithIdentifiers:@[ ident ]];
    [center removeDeliveredNotificationsWithIdentifiers:@[ ident ]];
}

static void apple_cancel_all(void* user)
{
    MEL_UNUSED(user);
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    [center removeAllPendingNotificationRequests];
    [center removeAllDeliveredNotifications];
}

static void apple_shutdown(void* user)
{
    MEL_UNUSED(user);
    if (apple_delegate_installed)
    {
        [UNUserNotificationCenter currentNotificationCenter].delegate = nil;
        apple_delegate = nil;
        apple_delegate_installed = false;
    }
    apple_categories = nil;
}

void mel_notif__register_host_providers(void)
{
    static const Mel_Notif_Provider_Desc desc = {
        .name = "apple-usernotifications",
        .supported = apple_supported,
        .caps = apple_caps,
        .authorization = apple_authorization,
        .authorize = apple_authorize,
        .post = apple_post,
        .update = apple_post,
        .cancel = apple_cancel,
        .cancel_all = apple_cancel_all,
        .shutdown = apple_shutdown,
    };
    mel_notif_provider_register(&desc);
    if (!apple_supported(NULL))
        return;
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    if (center.delegate != nil)
        mel_log_warn("notification", "UNUserNotificationCenter already has a delegate; replacing it");
    apple_delegate = [MelNotifDelegate new];
    center.delegate = apple_delegate;
    apple_delegate_installed = true;
    apple_refresh_auth();
}
