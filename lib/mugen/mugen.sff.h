#pragma once

#include "sprite.sheet.h"
#include "allocator.fwd.h"
#include "string.str8.fwd.h"

typedef struct {
    u16 group;
    u16 number;
    u32 frame_index;
    i16 offset_x;
    i16 offset_y;
    u16 width;
    u16 height;
} Mugen_Sff_Entry;

typedef struct {
    Mel_Sprite_Sheet sheet;
    Mugen_Sff_Entry* entries;
    u32 entry_count;
    u8* atlas_pixels;
    u32 atlas_width;
    u32 atlas_height;
} Mugen_Sff;

bool mugen_sff_load(Mugen_Sff* sff, str8 path, const Mel_Alloc* alloc);
u32  mugen_sff_find_frame(Mugen_Sff* sff, u16 group, u16 number);
void mugen_sff_shutdown(Mugen_Sff* sff, const Mel_Alloc* alloc);
