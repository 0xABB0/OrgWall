#pragma once

#include <display/display.h>
#include <allocator/allocator.fwd.h>

typedef struct {
    u64                             stable_id;
    Mel_Display_Descriptor desc;
} Mel_Display_Raw;

u32 mel_display__enumerate(const Mel_Alloc* alloc, Mel_Display_Raw* out, u32 cap);
