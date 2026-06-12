#pragma once

#include <stt/stt.h>
#include <stt/provider.h>

struct mel_stt_auth
{
    const char* name;
    bool        granted;
    u32         restrictiveness;
};
