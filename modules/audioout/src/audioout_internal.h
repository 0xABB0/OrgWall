#pragma once

#include <audioout/audioout.h>
#include <audioout/events.h>
#include <audioout/os.h>
#include <audioout/provider.h>

struct mel_audioout_kind
{
    const char* name;
};

Mel_AudioOut_Status mel_audioout__open(Mel_AudioOut d, Mel_AudioOut_Format req, Mel_AudioOut_Format* granted, Mel_AudioOut_Pull_Fn pull, void* token);
void                mel_audioout__start(Mel_AudioOut d, void* token);
void                mel_audioout__stop(Mel_AudioOut d, void* token);
void                mel_audioout__close(Mel_AudioOut d, void* token);

void mel_audioout__publish_register_provider(const Mel_Alloc* alloc);
