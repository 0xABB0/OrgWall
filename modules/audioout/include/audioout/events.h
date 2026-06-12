#pragma once

#include <audioout/audioout.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_AudioOut             device;
    const mel_audioout_kind* kind;
    bool                     added;
    bool                     removed;
    bool                     changed;
    bool                     default_changed;
} Mel_AudioOut_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_AudioOut_Hotplug_Sub;

#define MEL_AUDIOOUT_HOTPLUG_SUB_NULL ((Mel_AudioOut_Hotplug_Sub){ MEL_SLOTMAP_HANDLE_NULL })

typedef void (*Mel_AudioOut_Event_Callback)(const Mel_AudioOut_Event* ev, void* user);

Mel_AudioOut_Hotplug_Sub mel_audioout_subscribe(Mel_Executor* exec, Mel_AudioOut_Event_Callback cb, void* user);
void                     mel_audioout_unsubscribe(Mel_AudioOut_Hotplug_Sub sub);

#ifdef __cplusplus
}
#endif
