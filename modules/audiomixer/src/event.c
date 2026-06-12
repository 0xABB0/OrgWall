#include <audiomixer/event.h>

#include "mixer_internal.h"

#include <core/types.h>
#include <allocator/allocator.h>
#include <log/log.h>

Mel_Future* mel_mixer_voice_end_future(Mel_Mixer* eng, Mel_Mixer_Voice v)
{
    assert(eng != NULL);

    if (!mel_mixer__api_enter(eng))
        return NULL;

    Mel_Future* fut = mel_alloc(eng->alloc, sizeof(*fut));
    if (fut == NULL)
    {
        mel_log_error("audio", "voice_end_future: future alloc failed");
        mel_mixer__api_leave(eng);
        return NULL;
    }
    mel_future_init(fut, NULL, eng->alloc);

    if (eng->online == 0u)
    {
        Mel_Mixer__Command cmd = { .apply = mel_mixer__cmd_attach_end_future, .handle = v.slot, .fut = fut };
        mel_mixer__cmd_attach_end_future(eng, &cmd);
        mel_mixer__api_leave(eng);
        return fut;
    }

    Mel_Mixer__Command cmd = { .apply = mel_mixer__cmd_attach_end_future, .handle = v.slot, .fut = fut };
    mel_mixer__command_push(eng, &cmd);
    mel_mixer__api_leave(eng);
    return fut;
}

void mel_mixer_voice_end_future_free(Mel_Mixer* eng, Mel_Future* fut)
{
    assert(eng != NULL);
    if (fut == NULL)
        return;

    if (!mel_mixer__api_enter(eng))
        return;

    if (eng->online == 0u)
    {
        Mel_Mixer__Command cmd = { .apply = mel_mixer__cmd_release_end_future, .fut = fut };
        mel_mixer__cmd_release_end_future(eng, &cmd);
        mel_mixer__api_leave(eng);
        return;
    }

    Mel_Mixer__Command cmd = { .apply = mel_mixer__cmd_release_end_future, .fut = fut };
    mel_mixer__command_push(eng, &cmd);
    mel_mixer__api_leave(eng);
}

Mel_Event* mel_mixer_device_events(Mel_Mixer* eng)
{
    assert(eng != NULL);
    return eng->device_events;
}
