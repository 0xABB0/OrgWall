#pragma once

#include "core.types.h"
#include "fighter.h"
#include "mugen.sff.h"
#include "mugen.air.h"
#include "mugen.cmd.h"
#include "mugen.cns.h"
#include "gpu.texture.h"
#include "texture.pool.fwd.h"
#include "gpu.device.fwd.h"
#include "allocator.fwd.h"
#include "vfs.fwd.h"
#include "anim.clip.fwd.h"
#include "collection.slotmap.h"
#include "string.str8.fwd.h"
#include "sprite.pass.fwd.h"

#define MUGEN_CHAR_MAX_FILES 16

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
    u8* file_data[MUGEN_CHAR_MAX_FILES];
    u32 file_data_count;
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
    str8 def_path;
    str8 stcommon_path;
    const Mel_Alloc* alloc;
} Mugen_Char_Load_Opt;

bool mugen_char_load_opt(Mugen_Char* mc, Mugen_Char_Load_Opt opt);
#define mugen_char_load(mc, ...) mugen_char_load_opt((mc), (Mugen_Char_Load_Opt){__VA_ARGS__})

void mugen_char_shutdown(Mugen_Char* mc, Mel_Gpu_Device* dev, const Mel_Alloc* alloc);
