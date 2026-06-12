#pragma once

#include <audiopolicy/audiopolicy.h>
#include <audiopolicy/events.h>

struct mel_audiopolicy_category
{
    const char* name;
};

struct mel_audiopolicy_mode
{
    const char* name;
};

struct mel_audiopolicy_output
{
    const char* name;
};

struct mel_audiopolicy_route_reason
{
    const char* name;
};

typedef struct
{
    Mel_AudioPolicy_Status (*apply)(const Mel_AudioPolicy* requested, Mel_AudioPolicy* in_force);
    Mel_AudioPolicy_Status (*override_output)(const mel_audiopolicy_output* port);
    Mel_AudioPolicy_Status (*focus_request)(Mel_AudioPolicy_Focus_Opt opt);
    void (*focus_abandon)(void);
    void (*startup)(void);
    void (*shutdown)(void);
} Mel_AudioPolicy_Backend;

const Mel_AudioPolicy_Backend* mel_audiopolicy__backend(void);

void mel_audiopolicy__emit(const Mel_AudioPolicy_Event* ev);
