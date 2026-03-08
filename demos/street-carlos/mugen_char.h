#pragma once

#include "core.types.h"
#include "fighter.h"
#include "mugen_sff.h"
#include "mugen_air.h"
#include "mugen_cmd.h"
#include "mugen_cns.h"
#include "gpu.texture.h"
#include "texture.pool.fwd.h"
#include "gpu.device.fwd.h"
#include "allocator.fwd.h"
#include "vfs.fwd.h"
#include "anim.clip.fwd.h"
#include "collection.slotmap.h"
#include "string.str8.fwd.h"
#include "sprite.pass.fwd.h"

typedef struct {
    Mugen_Sff sff;
    Mugen_Air air;
    Mugen_Cmd cmd;
    Mugen_Cns cns;
    Mugen_Cns common_cns;
    Mugen_Cns cmd_cns;
    Mel_Anim_Clip_Pool clip_pool;
    Fighter_Action_Map* action_map;
    u32 action_map_count;
    u8* cmd_file_data;
    u8* cns_file_data;
    u8* common_cns_file_data;
    Mel_Gpu_Texture tex;
    Mel_Texture_Handle tex_handle;
    bool loaded;
    bool cns_loaded;
} Mugen_Char;

typedef struct {
    Mel_Gpu_Device* dev;
    Mel_Sprite_Pass* sprite_pass;
    Mel_Texture_Pool* tex_pool;
    Mel_Vfs* vfs;
    str8 sff_path;
    str8 air_path;
    str8 cmd_path;
    str8 cns_path;
    str8 common_cns_path;
    const Mel_Alloc* alloc;
} Mugen_Char_Load_Opt;

bool mugen_char_load_opt(Mugen_Char* mc, Mugen_Char_Load_Opt opt);
#define mugen_char_load(mc, ...) mugen_char_load_opt((mc), (Mugen_Char_Load_Opt){__VA_ARGS__})

void mugen_char_shutdown(Mugen_Char* mc, Mel_Gpu_Device* dev, const Mel_Alloc* alloc);
