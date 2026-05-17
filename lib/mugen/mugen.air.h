#pragma once

#include "core.types.h"
#include "str8.h"
#include "allocator.fwd.h"

#define MUGEN_AIR_NO_LOOP UINT32_MAX
#define MUGEN_TICKS_PER_SECOND 60.0f

typedef struct Mugen_Clsn_Box {
    i16 x1, y1, x2, y2;
} Mugen_Clsn_Box;

typedef struct Mugen_Air_Frame {
    u16 group;
    u16 number;
    i16 x_offset;
    i16 y_offset;
    i16 time;
    bool flip_h;
    bool flip_v;

    Mugen_Clsn_Box* clsn1;
    u32 clsn1_count;
    Mugen_Clsn_Box* clsn2;
    u32 clsn2_count;
} Mugen_Air_Frame;

typedef struct Mugen_Air_Action {
    u32 action_number;
    Mugen_Air_Frame* frames;
    u32 frame_count;
    u32 loop_start;
} Mugen_Air_Action;

typedef struct Mugen_Air {
    Mugen_Air_Action* actions;
    u32 action_count;
} Mugen_Air;

bool mugen_air_load(Mugen_Air* out, str8 data, const Mel_Alloc* alloc);
void mugen_air_shutdown(Mugen_Air* air, const Mel_Alloc* alloc);

Mugen_Air_Action* mugen_air_find_action(Mugen_Air* air, u32 action_number);

Mugen_Clsn_Box mugen_clsn_bounding_box(const Mugen_Clsn_Box* boxes, u32 count);
