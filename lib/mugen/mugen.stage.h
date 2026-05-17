#pragma once

#include "core.types.h"
#include "str8.fwd.h"
#include "allocator.fwd.h"

#define MUGEN_BG_NORMAL   0
#define MUGEN_BG_PARALLAX 1

#define MUGEN_TRANS_NONE     0
#define MUGEN_TRANS_ADD      1
#define MUGEN_TRANS_ADD1     2
#define MUGEN_TRANS_ADDALPHA 3
#define MUGEN_TRANS_SUB      4

typedef struct {
    u8 type;
    u8 layerno;
    u8 trans;
    bool mask;
    u16 sprite_group;
    u16 sprite_number;
    f32 start_x, start_y;
    f32 delta_x, delta_y;
    f32 velocity_x, velocity_y;
    i32 tile_x, tile_y;
    i32 tilespacing_x, tilespacing_y;
    u16 alpha_src, alpha_dst;
    f32 xscale_top, xscale_bot;
    f32 yscalestart, yscaledelta;
} Mugen_Stage_BG;

typedef struct Mugen_Stage {
    f32 p1startx, p2startx;
    i8  p1facing, p2facing;
    f32 left_bound, right_bound;
    f32 top_bound, bottom_bound;
    f32 zoffset;
    f32 screenleft, screenright;
    f32 camera_startx, camera_starty;
    f32 tension;
    f32 verticalfollow;
    f32 floortension;
    i32 localcoord_w, localcoord_h;
    str8 spr_path;
    Mugen_Stage_BG* bgs;
    u32 bg_count;
} Mugen_Stage;

bool mugen_stage_load(Mugen_Stage* out, str8 data, const Mel_Alloc* alloc);
void mugen_stage_shutdown(Mugen_Stage* s, const Mel_Alloc* alloc);
