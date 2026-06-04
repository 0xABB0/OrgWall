#pragma once

#include <vibration/vibration.h>
#include <vibration/provider.h>
#include <allocator/allocator.fwd.h>
#include <collection.slotmap/slotmap.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_Vib_Provider_Desc desc;
    u32                   generation;
    bool                  active;
} Mel_Vib_Provider_Entry;

typedef struct
{
    u32                provider_idx;
    u64                stable_id;
    Mel_Vib_Descriptor desc;
} Mel_Vib_Device_Slot;

const Mel_Alloc*        mel_vib__alloc(void);
bool                    mel_vib__ready(void);
Mel_Vib_Device_Slot*    mel_vib__device_slot(Mel_SlotMap_Handle h);
Mel_Vib_Provider_Entry* mel_vib__provider(u32 idx);
u64                     mel_vib__next_token(void);

void mel_vib_ff__shutdown(void);

typedef void (*Mel_Vib_Host_Register_Fn)(void);
void mel_vib__set_host_register(Mel_Vib_Host_Register_Fn fn);

#ifdef __cplusplus
}
#endif
