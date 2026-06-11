#pragma once

#include <audioin/audioin.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_AudioIn             device;
    const mel_audioin_kind* kind;
    bool                    added;
    bool                    removed;
    bool                    changed;
    bool                    default_changed;
} Mel_AudioIn_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_AudioIn_Hotplug_Sub;

#define MEL_AUDIOIN_HOTPLUG_SUB_NULL ((Mel_AudioIn_Hotplug_Sub){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_AudioIn_Event_Callback)(const Mel_AudioIn_Event* ev, void* user);

Mel_AudioIn_Hotplug_Sub mel_audioin_subscribe(Mel_Executor* exec, Mel_AudioIn_Event_Callback cb, void* user);
void                    mel_audioin_unsubscribe(Mel_AudioIn_Hotplug_Sub sub);

#ifdef __cplusplus
}
#endif
