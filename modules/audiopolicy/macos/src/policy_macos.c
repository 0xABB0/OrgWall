#include "../../src/audiopolicy_internal.h"

#include <log/log.h>

#include <CoreAudio/CoreAudio.h>

static const AudioObjectPropertyAddress MEL_POLICY__DEFAULT_OUTPUT = {
    kAudioHardwarePropertyDefaultOutputDevice,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain,
};

static const AudioObjectPropertyAddress MEL_POLICY__DEFAULT_INPUT = {
    kAudioHardwarePropertyDefaultInputDevice,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain,
};

static bool policy_listening;

static OSStatus policy_route_listener(AudioObjectID object, UInt32 address_count, const AudioObjectPropertyAddress* addresses, void* user)
{
    MEL_UNUSED(object);
    MEL_UNUSED(address_count);
    MEL_UNUSED(addresses);
    MEL_UNUSED(user);
    Mel_AudioPolicy_Event ev = { .route_changed = true, .reason = &mel_audiopolicy_route_unknown };
    mel_audiopolicy__emit(&ev);
    return noErr;
}

static void policy_startup(void)
{
    OSStatus out = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &MEL_POLICY__DEFAULT_OUTPUT, policy_route_listener, NULL);
    OSStatus in = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &MEL_POLICY__DEFAULT_INPUT, policy_route_listener, NULL);
    policy_listening = out == noErr && in == noErr;
    if (!policy_listening)
        mel_log_warn("audiopolicy", "default-device listeners failed (out=%d in=%d); route events unavailable", (int)out, (int)in);
    mel_log_info("audiopolicy", "macos: no OS session object; category/mode lower with named warnings, interruptions and focus are honest-absent");
}

static void policy_shutdown(void)
{
    if (!policy_listening)
        return;
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &MEL_POLICY__DEFAULT_OUTPUT, policy_route_listener, NULL);
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &MEL_POLICY__DEFAULT_INPUT, policy_route_listener, NULL);
    policy_listening = false;
}

static Mel_AudioPolicy_Status policy_apply(const Mel_AudioPolicy* requested, Mel_AudioPolicy* in_force)
{
    u32 bits = 0;
    *in_force = *requested;

    if (requested->mode != &mel_audiopolicy_mode_default)
    {
        bits |= MEL_AUDIOPOLICY_WARN_MODE_IGNORED;
        in_force->mode = &mel_audiopolicy_mode_default;
    }
    if (requested->duck_others)
    {
        bits |= MEL_AUDIOPOLICY_WARN_DUCK_IGNORED;
        in_force->duck_others = false;
    }
    if (requested->allow_bluetooth || requested->allow_bluetooth_a2dp)
        bits |= MEL_AUDIOPOLICY_WARN_BLUETOOTH_IGNORED;
    if (requested->default_to_speaker)
    {
        bits |= MEL_AUDIOPOLICY_WARN_OVERRIDE_IGNORED;
        in_force->default_to_speaker = false;
    }

    in_force->mix_with_others = true;
    return bits;
}

static const Mel_AudioPolicy_Backend MACOS_BACKEND = {
    .apply = policy_apply,
    .startup = policy_startup,
    .shutdown = policy_shutdown,
};

const Mel_AudioPolicy_Backend* mel_audiopolicy__backend(void) { return &MACOS_BACKEND; }
