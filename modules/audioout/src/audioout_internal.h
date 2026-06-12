#pragma once

#include <audioout/audioout.h>
#include <audioout/events.h>
#include <audioout/os.h>
#include <audioout/provider.h>

struct mel_audioout_kind
{
    const char* name;
};

void mel_audioout__publish_register_provider(const Mel_Alloc* alloc);
