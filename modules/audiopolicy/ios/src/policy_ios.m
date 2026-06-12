#include "../../src/audiopolicy_internal.h"

#include <log/log.h>

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

static id g_policy_interruption_observer;
static id g_policy_route_observer;
static id g_policy_secondary_observer;

static const char* policy_nscstr(NSString* s) { return s != nil && s.UTF8String != NULL ? s.UTF8String : ""; }

static const char* policy_errstr(NSError* err) { return err != nil ? policy_nscstr(err.localizedDescription) : "unknown"; }

static NSString* policy_category_to_av(const mel_audiopolicy_category* c)
{
    if (c == &mel_audiopolicy_playback)
        return AVAudioSessionCategoryPlayback;
    if (c == &mel_audiopolicy_record)
        return AVAudioSessionCategoryRecord;
    if (c == &mel_audiopolicy_duplex)
        return AVAudioSessionCategoryPlayAndRecord;
    if (c == &mel_audiopolicy_ambient)
        return AVAudioSessionCategoryAmbient;
    return nil;
}

static const mel_audiopolicy_category* policy_category_from_av(NSString* cat)
{
    if ([cat isEqualToString:AVAudioSessionCategoryPlayback])
        return &mel_audiopolicy_playback;
    if ([cat isEqualToString:AVAudioSessionCategoryRecord])
        return &mel_audiopolicy_record;
    if ([cat isEqualToString:AVAudioSessionCategoryPlayAndRecord] || [cat isEqualToString:AVAudioSessionCategoryMultiRoute])
        return &mel_audiopolicy_duplex;
    if ([cat isEqualToString:AVAudioSessionCategoryAmbient] || [cat isEqualToString:AVAudioSessionCategorySoloAmbient])
        return &mel_audiopolicy_ambient;
    return NULL;
}

static NSString* policy_mode_to_av(const mel_audiopolicy_mode* m)
{
    if (m == &mel_audiopolicy_mode_default)
        return AVAudioSessionModeDefault;
    if (m == &mel_audiopolicy_mode_voice_chat)
        return AVAudioSessionModeVoiceChat;
    if (m == &mel_audiopolicy_mode_video_chat)
        return AVAudioSessionModeVideoChat;
    if (m == &mel_audiopolicy_mode_measurement)
        return AVAudioSessionModeMeasurement;
    if (m == &mel_audiopolicy_mode_media)
        return AVAudioSessionModeMoviePlayback;
    return nil;
}

static const mel_audiopolicy_mode* policy_mode_from_av(NSString* mode)
{
    if ([mode isEqualToString:AVAudioSessionModeDefault])
        return &mel_audiopolicy_mode_default;
    if ([mode isEqualToString:AVAudioSessionModeVoiceChat])
        return &mel_audiopolicy_mode_voice_chat;
    if ([mode isEqualToString:AVAudioSessionModeVideoChat])
        return &mel_audiopolicy_mode_video_chat;
    if ([mode isEqualToString:AVAudioSessionModeMeasurement])
        return &mel_audiopolicy_mode_measurement;
    if ([mode isEqualToString:AVAudioSessionModeMoviePlayback])
        return &mel_audiopolicy_mode_media;
    return NULL;
}

static u32 policy_readback(AVAudioSession* session, const Mel_AudioPolicy* requested, Mel_AudioPolicy* in_force)
{
    u32 bits = 0;

    const mel_audiopolicy_category* cat = policy_category_from_av(session.category);
    if (cat == NULL)
    {
        mel_log_warn("audiopolicy", "ios: session reports unmapped category %s; reporting requested category as lowered", policy_nscstr(session.category));
        bits |= MEL_AUDIOPOLICY_WARN_CATEGORY_LOWERED;
        cat = requested->category;
    }
    else if (cat != requested->category)
        bits |= MEL_AUDIOPOLICY_WARN_CATEGORY_LOWERED;
    in_force->category = cat;

    const mel_audiopolicy_mode* mode = policy_mode_from_av(session.mode);
    if (mode == NULL)
    {
        mel_log_warn("audiopolicy", "ios: session reports unmapped mode %s; reporting default mode", policy_nscstr(session.mode));
        mode = &mel_audiopolicy_mode_default;
    }
    in_force->mode = mode;
    if (mode != requested->mode)
        bits |= MEL_AUDIOPOLICY_WARN_MODE_IGNORED;

    AVAudioSessionCategoryOptions got = session.categoryOptions;
    bool                          ambient = cat == &mel_audiopolicy_ambient;
    bool                          a2dp_inherent = cat == &mel_audiopolicy_playback || ambient;
    in_force->mix_with_others = (got & AVAudioSessionCategoryOptionMixWithOthers) != 0 || ambient;
    in_force->duck_others = (got & AVAudioSessionCategoryOptionDuckOthers) != 0;
    in_force->default_to_speaker = (got & AVAudioSessionCategoryOptionDefaultToSpeaker) != 0;
    in_force->allow_bluetooth = (got & AVAudioSessionCategoryOptionAllowBluetoothHFP) != 0;
    in_force->allow_bluetooth_a2dp = (got & AVAudioSessionCategoryOptionAllowBluetoothA2DP) != 0 || a2dp_inherent;

    if (requested->mix_with_others && !in_force->mix_with_others)
        bits |= MEL_AUDIOPOLICY_WARN_MIX_IGNORED;
    if (requested->duck_others && !in_force->duck_others)
        bits |= MEL_AUDIOPOLICY_WARN_DUCK_IGNORED;
    if (requested->default_to_speaker && !in_force->default_to_speaker)
        bits |= MEL_AUDIOPOLICY_WARN_OVERRIDE_IGNORED;
    if ((requested->allow_bluetooth && !in_force->allow_bluetooth) || (requested->allow_bluetooth_a2dp && !in_force->allow_bluetooth_a2dp))
        bits |= MEL_AUDIOPOLICY_WARN_BLUETOOTH_IGNORED;

    return bits;
}

static Mel_AudioPolicy_Status policy_apply(const Mel_AudioPolicy* requested, Mel_AudioPolicy* in_force)
{
    @autoreleasepool
    {
        AVAudioSession* session = [AVAudioSession sharedInstance];

        NSString* cat = policy_category_to_av(requested->category);
        if (cat == nil)
        {
            mel_log_error("audiopolicy", "ios: category %s has no AVAudioSession mapping", mel_audiopolicy_category_name(requested->category));
            return MEL_AUDIOPOLICY_ERROR | MEL_AUDIOPOLICY_RESULT_UNSUPPORTED;
        }
        NSString* mode = policy_mode_to_av(requested->mode);
        if (mode == nil)
        {
            mel_log_warn("audiopolicy", "ios: mode %s has no AVAudioSession mapping; applying default mode", mel_audiopolicy_mode_name(requested->mode));
            mode = AVAudioSessionModeDefault;
        }

        bool duplex = requested->category == &mel_audiopolicy_duplex;
        if (requested->default_to_speaker && !duplex)
            mel_log_warn("audiopolicy", "ios: default_to_speaker is a PlayAndRecord option; category %s cannot carry it", mel_audiopolicy_category_name(requested->category));

        AVAudioSessionCategoryOptions opts = 0;
        if (requested->mix_with_others)
            opts |= AVAudioSessionCategoryOptionMixWithOthers;
        if (requested->duck_others)
            opts |= AVAudioSessionCategoryOptionDuckOthers;
        if (requested->default_to_speaker && duplex)
            opts |= AVAudioSessionCategoryOptionDefaultToSpeaker;
        if (requested->allow_bluetooth)
            opts |= AVAudioSessionCategoryOptionAllowBluetoothHFP;
        if (requested->allow_bluetooth_a2dp)
            opts |= AVAudioSessionCategoryOptionAllowBluetoothA2DP;

        NSError* err = nil;
        bool     set = [session setCategory:cat mode:mode options:opts error:&err];
        if (!set && ![mode isEqualToString:AVAudioSessionModeDefault])
        {
            mel_log_warn("audiopolicy", "ios: setCategory %s mode %s options 0x%lx rejected: %s; retrying with default mode", policy_nscstr(cat), policy_nscstr(mode), (unsigned long)opts, policy_errstr(err));
            mode = AVAudioSessionModeDefault;
            err = nil;
            set = [session setCategory:cat mode:mode options:opts error:&err];
        }
        static const struct
        {
            AVAudioSessionCategoryOptions option;
            const char*                   name;
        } drops[] = {
            { AVAudioSessionCategoryOptionAllowBluetoothHFP, "allow_bluetooth" },   { AVAudioSessionCategoryOptionAllowBluetoothA2DP, "allow_bluetooth_a2dp" },
            { AVAudioSessionCategoryOptionDefaultToSpeaker, "default_to_speaker" }, { AVAudioSessionCategoryOptionDuckOthers, "duck_others" },
            { AVAudioSessionCategoryOptionMixWithOthers, "mix_with_others" },
        };
        for (usize i = 0; !set && i < sizeof drops / sizeof drops[0]; i++)
        {
            if ((opts & drops[i].option) == 0)
                continue;
            mel_log_warn("audiopolicy", "ios: setCategory %s options 0x%lx rejected: %s; retrying without %s", policy_nscstr(cat), (unsigned long)opts, policy_errstr(err), drops[i].name);
            opts &= ~drops[i].option;
            err = nil;
            set = [session setCategory:cat mode:mode options:opts error:&err];
        }
        if (!set)
        {
            mel_log_error("audiopolicy", "ios: setCategory %s failed with every lowering: %s", policy_nscstr(cat), policy_errstr(err));
            return MEL_AUDIOPOLICY_ERROR | MEL_AUDIOPOLICY_RESULT_UNSUPPORTED;
        }

        err = nil;
        if (![session setActive:YES error:&err])
        {
            mel_log_error("audiopolicy", "ios: session activation failed: %s", policy_errstr(err));
            return MEL_AUDIOPOLICY_ERROR | MEL_AUDIOPOLICY_RESULT_BUSY;
        }

        return policy_readback(session, requested, in_force);
    }
}

static Mel_AudioPolicy_Status policy_override_output(const mel_audiopolicy_output* port)
{
    @autoreleasepool
    {
        AVAudioSessionPortOverride ov;
        if (port == &mel_audiopolicy_output_speaker)
            ov = AVAudioSessionPortOverrideSpeaker;
        else if (port == &mel_audiopolicy_output_default)
            ov = AVAudioSessionPortOverrideNone;
        else
        {
            mel_log_error("audiopolicy", "ios: output port %s has no AVAudioSession override mapping", port->name);
            return MEL_AUDIOPOLICY_ERROR | MEL_AUDIOPOLICY_RESULT_UNSUPPORTED;
        }
        NSError* err = nil;
        if (![[AVAudioSession sharedInstance] overrideOutputAudioPort:ov error:&err])
        {
            mel_log_error("audiopolicy", "ios: overrideOutputAudioPort %s failed: %s", port->name, policy_errstr(err));
            return MEL_AUDIOPOLICY_ERROR | MEL_AUDIOPOLICY_RESULT_UNSUPPORTED;
        }
        return MEL_AUDIOPOLICY_OK;
    }
}

static Mel_AudioPolicy_Status policy_focus_request(Mel_AudioPolicy_Focus_Opt opt)
{
    MEL_UNUSED(opt);
    mel_log_debug("audiopolicy", "ios: no explicit focus API; session activation already arbitrates, loss arrives as interruption events");
    return MEL_AUDIOPOLICY_OK;
}

static void policy_focus_abandon(void) {}

static void policy_handle_interruption(NSNotification* n)
{
    NSNumber* type_num = n.userInfo[AVAudioSessionInterruptionTypeKey];
    if (type_num == nil)
    {
        mel_log_warn("audiopolicy", "ios: interruption notification without type; dropped");
        return;
    }
    NSUInteger type = type_num.unsignedIntegerValue;
    if (type == AVAudioSessionInterruptionTypeBegan)
    {
        mel_log_info("audiopolicy", "ios: interruption began");
        Mel_AudioPolicy_Event ev = { .interruption_began = true };
        mel_audiopolicy__emit(&ev);
    }
    else if (type == AVAudioSessionInterruptionTypeEnded)
    {
        NSUInteger options = [n.userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];
        bool       resume = (options & AVAudioSessionInterruptionOptionShouldResume) != 0;
        mel_log_info("audiopolicy", "ios: interruption ended (should_resume=%d)", resume ? 1 : 0);
        Mel_AudioPolicy_Event ev = { .interruption_ended = true, .should_resume = resume };
        mel_audiopolicy__emit(&ev);
    }
}

static const mel_audiopolicy_route_reason* policy_route_reason_from_av(NSUInteger reason)
{
    if (reason == AVAudioSessionRouteChangeReasonNewDeviceAvailable)
        return &mel_audiopolicy_route_device_added;
    if (reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable)
        return &mel_audiopolicy_route_device_removed;
    if (reason == AVAudioSessionRouteChangeReasonCategoryChange)
        return &mel_audiopolicy_route_category_changed;
    if (reason == AVAudioSessionRouteChangeReasonOverride)
        return &mel_audiopolicy_route_override;
    return &mel_audiopolicy_route_unknown;
}

static void policy_handle_route_change(NSNotification* n)
{
    NSUInteger                          reason = [n.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];
    const mel_audiopolicy_route_reason* r = policy_route_reason_from_av(reason);
    mel_log_debug("audiopolicy", "ios: route changed (%s)", mel_audiopolicy_route_reason_name(r));
    Mel_AudioPolicy_Event ev = { .route_changed = true, .reason = r };
    mel_audiopolicy__emit(&ev);
}

static void policy_handle_secondary_audio(NSNotification* n)
{
    NSUInteger type = [n.userInfo[AVAudioSessionSilenceSecondaryAudioHintTypeKey] unsignedIntegerValue];
    if (type == AVAudioSessionSilenceSecondaryAudioHintTypeBegin)
    {
        mel_log_debug("audiopolicy", "ios: secondary audio silence hint began; emitting should_duck");
        Mel_AudioPolicy_Event ev = { .should_duck = true };
        mel_audiopolicy__emit(&ev);
    }
    else
        mel_log_debug("audiopolicy", "ios: secondary audio silence hint ended; the event stream carries no duck-end, app volume policy decides recovery");
}

static void policy_startup(void)
{
    @autoreleasepool
    {
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        AVAudioSession*       session = [AVAudioSession sharedInstance];
        g_policy_interruption_observer = [center addObserverForName:AVAudioSessionInterruptionNotification
                                                             object:session
                                                              queue:nil
                                                         usingBlock:^(NSNotification* n) {
                                                             policy_handle_interruption(n);
                                                         }];
        g_policy_route_observer = [center addObserverForName:AVAudioSessionRouteChangeNotification
                                                      object:session
                                                       queue:nil
                                                  usingBlock:^(NSNotification* n) {
                                                      policy_handle_route_change(n);
                                                  }];
        g_policy_secondary_observer = [center addObserverForName:AVAudioSessionSilenceSecondaryAudioHintNotification
                                                          object:session
                                                           queue:nil
                                                      usingBlock:^(NSNotification* n) {
                                                          policy_handle_secondary_audio(n);
                                                      }];
        mel_log_info("audiopolicy", "ios: AVAudioSession backend up; category/mode/options map 1:1, focus rides session activation and interruptions");
    }
}

static void policy_shutdown(void)
{
    @autoreleasepool
    {
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        if (g_policy_interruption_observer != nil)
        {
            [center removeObserver:g_policy_interruption_observer];
            g_policy_interruption_observer = nil;
        }
        if (g_policy_route_observer != nil)
        {
            [center removeObserver:g_policy_route_observer];
            g_policy_route_observer = nil;
        }
        if (g_policy_secondary_observer != nil)
        {
            [center removeObserver:g_policy_secondary_observer];
            g_policy_secondary_observer = nil;
        }
    }
}

static const Mel_AudioPolicy_Backend IOS_BACKEND = {
    .apply = policy_apply,
    .override_output = policy_override_output,
    .focus_request = policy_focus_request,
    .focus_abandon = policy_focus_abandon,
    .startup = policy_startup,
    .shutdown = policy_shutdown,
};

const Mel_AudioPolicy_Backend* mel_audiopolicy__backend(void) { return &IOS_BACKEND; }
