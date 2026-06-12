#include "../../src/audiopolicy_internal.h"

#include <log/log.h>

static void policy_startup(void) { mel_log_info("audiopolicy", "win32: communications-role ducking engages when streams open with the communications role under voice_chat; other knobs lower with named warnings"); }

static Mel_AudioPolicy_Status policy_apply(const Mel_AudioPolicy* requested, Mel_AudioPolicy* in_force)
{
    u32 bits = 0;
    *in_force = *requested;

    bool comms = requested->mode == &mel_audiopolicy_mode_voice_chat || requested->mode == &mel_audiopolicy_mode_video_chat;

    if (requested->mode != &mel_audiopolicy_mode_default && !comms)
    {
        bits |= MEL_AUDIOPOLICY_WARN_MODE_IGNORED;
        in_force->mode = &mel_audiopolicy_mode_default;
    }
    if (requested->duck_others && !comms)
    {
        bits |= MEL_AUDIOPOLICY_WARN_DUCK_IGNORED;
        in_force->duck_others = false;
    }
    if (requested->duck_others && comms)
        mel_log_info("audiopolicy", "duck_others honored through the OS communications role once streams open");
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

static const Mel_AudioPolicy_Backend WIN32_BACKEND = {
    .apply = policy_apply,
    .startup = policy_startup,
};

const Mel_AudioPolicy_Backend* mel_audiopolicy__backend(void) { return &WIN32_BACKEND; }
