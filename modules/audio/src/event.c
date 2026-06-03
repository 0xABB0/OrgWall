#include <audio/event.h>

#include "audio_internal.h"

#include <core/types.h>
#include <allocator/allocator.h>
#include <log/log.h>

Mel_Future* mel_audio_voice_end_future(Mel_Audio* eng, Mel_Audio_Voice v)
{
    assert(eng != NULL);

    if (!mel_audio__api_enter(eng))
        return NULL;

    Mel_Future* fut = mel_alloc(eng->alloc, sizeof(*fut));
    if (fut == NULL)
    {
        mel_log_error("audio", "voice_end_future: future alloc failed");
        mel_audio__api_leave(eng);
        return NULL;
    }
    mel_future_init(fut, NULL, eng->alloc);

    if (eng->online == 0u)
    {
        Mel_Audio__Command cmd = { .apply = mel_audio__cmd_attach_end_future, .handle = v.slot, .fut = fut };
        mel_audio__cmd_attach_end_future(eng, &cmd);
        mel_audio__api_leave(eng);
        return fut;
    }

    Mel_Audio__Command cmd = { .apply = mel_audio__cmd_attach_end_future, .handle = v.slot, .fut = fut };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
    return fut;
}

Mel_Event* mel_audio_device_events(Mel_Audio* eng)
{
    assert(eng != NULL);
    return eng->device_events;
}
