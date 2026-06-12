#pragma once

#include <audiopolicy/audiopolicy.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct mel_audiopolicy_route_reason mel_audiopolicy_route_reason;

extern const mel_audiopolicy_route_reason mel_audiopolicy_route_device_added;
extern const mel_audiopolicy_route_reason mel_audiopolicy_route_device_removed;
extern const mel_audiopolicy_route_reason mel_audiopolicy_route_category_changed;
extern const mel_audiopolicy_route_reason mel_audiopolicy_route_override;
extern const mel_audiopolicy_route_reason mel_audiopolicy_route_unknown;

const char* mel_audiopolicy_route_reason_name(const mel_audiopolicy_route_reason* r);

typedef struct
{
    bool                                interruption_began;
    bool                                interruption_ended;
    bool                                should_resume;
    bool                                should_duck;
    bool                                duck_ended;
    bool                                focus_lost;
    bool                                focus_gained;
    bool                                route_changed;
    const mel_audiopolicy_route_reason* reason;
} Mel_AudioPolicy_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_AudioPolicy_Sub;

#define MEL_AUDIOPOLICY_SUB_NULL ((Mel_AudioPolicy_Sub){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_AudioPolicy_Event_Callback)(const Mel_AudioPolicy_Event* ev, void* user);

Mel_AudioPolicy_Sub mel_audiopolicy_subscribe(Mel_Executor* exec, Mel_AudioPolicy_Event_Callback cb, void* user);
void                mel_audiopolicy_unsubscribe(Mel_AudioPolicy_Sub sub);

#ifdef __cplusplus
}
#endif
