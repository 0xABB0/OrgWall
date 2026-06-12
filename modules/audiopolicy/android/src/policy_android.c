#include <core/platform.h>

#if !MEL_PLATFORM_ANDROID
#error "android-only translation unit"
#endif

#include "../../src/audiopolicy_internal.h"
#include "policy_android_internal.h"

#include <log/log.h>

typedef struct
{
    i32  usage;
    i32  content_type;
    bool attrs_stored;
    bool duck_others;
    bool comms;
    bool mode_set;
} APolicy;

static APolicy g_apolicy;

static void policy_startup(void) { mel_log_info("audiopolicy", "android: audio focus first-class; bluetooth routing follows the system, speaker override needs communication mode on API 31+"); }

static void policy_shutdown(void)
{
    if (g_apolicy.mode_set)
    {
        mel_audiopolicy_android__jni_clear_communication_device();
        mel_audiopolicy_android__jni_set_mode(MEL_AUDIOPOLICY_ANDROID_MODE_NORMAL);
    }
    g_apolicy = (APolicy){ 0 };
}

static Mel_AudioPolicy_Status policy_apply(const Mel_AudioPolicy* requested, Mel_AudioPolicy* in_force)
{
    u32 bits = 0;
    *in_force = *requested;

    bool comms_mode = requested->mode == &mel_audiopolicy_mode_voice_chat || requested->mode == &mel_audiopolicy_mode_video_chat;
    bool comms = comms_mode && requested->category == &mel_audiopolicy_duplex;

    if (comms_mode && !comms)
    {
        bits |= MEL_AUDIOPOLICY_WARN_MODE_IGNORED;
        in_force->mode = &mel_audiopolicy_mode_default;
        mel_log_warn("audiopolicy", "android: %s mode needs the duplex category; mode lowered", mel_audiopolicy_mode_name(requested->mode));
    }
    if (requested->mode == &mel_audiopolicy_mode_measurement)
    {
        bits |= MEL_AUDIOPOLICY_WARN_MODE_IGNORED;
        in_force->mode = &mel_audiopolicy_mode_default;
        mel_log_warn("audiopolicy", "android: measurement mode has no session equivalent; mode lowered");
    }

    if (comms && !g_apolicy.mode_set)
    {
        if (mel_audiopolicy_android__jni_set_mode(MEL_AUDIOPOLICY_ANDROID_MODE_IN_COMMUNICATION))
            g_apolicy.mode_set = true;
        else
        {
            bits |= MEL_AUDIOPOLICY_WARN_MODE_IGNORED;
            in_force->mode = &mel_audiopolicy_mode_default;
            comms = false;
            mel_log_warn("audiopolicy", "android: AudioManager.setMode(MODE_IN_COMMUNICATION) failed; mode lowered");
        }
    }
    if (!comms && g_apolicy.mode_set)
    {
        mel_audiopolicy_android__jni_clear_communication_device();
        mel_audiopolicy_android__jni_set_mode(MEL_AUDIOPOLICY_ANDROID_MODE_NORMAL);
        g_apolicy.mode_set = false;
    }

    g_apolicy.comms = comms;
    g_apolicy.duck_others = requested->duck_others;
    g_apolicy.usage = comms ? MEL_AUDIOPOLICY_ANDROID_USAGE_VOICE_COMMUNICATION : MEL_AUDIOPOLICY_ANDROID_USAGE_MEDIA;
    g_apolicy.content_type = comms ? MEL_AUDIOPOLICY_ANDROID_CONTENT_TYPE_SPEECH : MEL_AUDIOPOLICY_ANDROID_CONTENT_TYPE_MUSIC;
    g_apolicy.attrs_stored = true;

    if (requested->default_to_speaker && !(comms && mel_audiopolicy_android__jni_speaker_communication_device()))
    {
        bits |= MEL_AUDIOPOLICY_WARN_OVERRIDE_IGNORED;
        in_force->default_to_speaker = false;
        mel_log_warn("audiopolicy", "android: default_to_speaker needs communication mode and API 31; ignored");
    }
    if (requested->allow_bluetooth || requested->allow_bluetooth_a2dp)
    {
        bits |= MEL_AUDIOPOLICY_WARN_BLUETOOTH_IGNORED;
        mel_log_info("audiopolicy", "android: bluetooth routing follows the system");
    }
    if (requested->duck_others)
        mel_log_info("audiopolicy", "android: duck_others shapes the next focus request (transient-may-duck)");

    in_force->mix_with_others = true;
    return bits;
}

static Mel_AudioPolicy_Status policy_override(const mel_audiopolicy_output* port)
{
    if (port == &mel_audiopolicy_output_speaker)
    {
        if (g_apolicy.mode_set && mel_audiopolicy_android__jni_speaker_communication_device())
            return MEL_AUDIOPOLICY_OK;
        mel_log_warn("audiopolicy", "android: speaker override needs communication mode and API 31; ignored");
        return MEL_AUDIOPOLICY_WARNED | MEL_AUDIOPOLICY_WARN_OVERRIDE_IGNORED;
    }
    if (port == &mel_audiopolicy_output_default)
    {
        mel_audiopolicy_android__jni_clear_communication_device();
        return MEL_AUDIOPOLICY_OK;
    }
    mel_log_warn("audiopolicy", "android: unknown output port; ignored");
    return MEL_AUDIOPOLICY_WARNED | MEL_AUDIOPOLICY_WARN_OVERRIDE_IGNORED;
}

static Mel_AudioPolicy_Status policy_focus_request(Mel_AudioPolicy_Focus_Opt opt)
{
    i32 usage = MEL_AUDIOPOLICY_ANDROID_USAGE_MEDIA;
    i32 content = MEL_AUDIOPOLICY_ANDROID_CONTENT_TYPE_MUSIC;
    if (g_apolicy.attrs_stored)
    {
        usage = g_apolicy.usage;
        content = g_apolicy.content_type;
    }
    else
        mel_log_info("audiopolicy", "android: focus requested before apply; using USAGE_MEDIA attributes");

    i32 gain = MEL_AUDIOPOLICY_ANDROID_FOCUS_GAIN;
    if (g_apolicy.comms)
        gain = MEL_AUDIOPOLICY_ANDROID_FOCUS_GAIN_TRANSIENT;
    else if (g_apolicy.duck_others)
        gain = MEL_AUDIOPOLICY_ANDROID_FOCUS_GAIN_TRANSIENT_MAY_DUCK;

    i32 res = mel_audiopolicy_android__jni_request_focus(gain, usage, content, !opt.may_duck_me);
    if (res == MEL_AUDIOPOLICY_ANDROID_REQUEST_GRANTED)
        return MEL_AUDIOPOLICY_OK;
    if (res == MEL_AUDIOPOLICY_ANDROID_REQUEST_DELAYED)
    {
        mel_log_info("audiopolicy", "android: focus grant delayed by the system; treating as held");
        return MEL_AUDIOPOLICY_OK;
    }
    mel_log_error("audiopolicy", "android: audio focus request denied");
    return MEL_AUDIOPOLICY_ERROR | MEL_AUDIOPOLICY_RESULT_BUSY;
}

static void policy_focus_abandon(void) { mel_audiopolicy_android__jni_abandon_focus(); }

void mel_audiopolicy_android__on_focus_change(i32 change)
{
    Mel_AudioPolicy_Event ev = { 0 };
    if (change == MEL_AUDIOPOLICY_ANDROID_FOCUS_LOSS)
        ev.focus_lost = true;
    else if (change == MEL_AUDIOPOLICY_ANDROID_FOCUS_LOSS_TRANSIENT)
    {
        ev.interruption_began = true;
        ev.focus_lost = true;
    }
    else if (change == MEL_AUDIOPOLICY_ANDROID_FOCUS_LOSS_TRANSIENT_CAN_DUCK)
        ev.should_duck = true;
    else if (change == MEL_AUDIOPOLICY_ANDROID_FOCUS_GAIN)
    {
        ev.focus_gained = true;
        ev.interruption_ended = true;
        ev.should_resume = true;
        ev.duck_ended = true;
    }
    else
    {
        mel_log_warn("audiopolicy", "android: unknown focus change %d", (int)change);
        return;
    }
    mel_audiopolicy__emit(&ev);
}

static const Mel_AudioPolicy_Backend ANDROID_BACKEND = {
    .apply = policy_apply,
    .override_output = policy_override,
    .focus_request = policy_focus_request,
    .focus_abandon = policy_focus_abandon,
    .startup = policy_startup,
    .shutdown = policy_shutdown,
};

const Mel_AudioPolicy_Backend* mel_audiopolicy__backend(void) { return &ANDROID_BACKEND; }
