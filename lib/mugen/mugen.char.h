#pragma once

#include "core.types.h"
#include "mugen.sff.h"
#include "mugen.air.h"
#include "mugen.cmd.h"
#include "mugen.cns.h"
#include "allocator.fwd.h"
// ASYNC_V2: VFS removed
typedef struct Mel_Vfs Mel_Vfs;
#include "string.str8.fwd.h"

typedef struct {
    Mugen_Sff sff;
    Mugen_Air air;
    Mugen_Cmd cmd;
    Mugen_Cns cns;
    Mugen_Cns common_cns;
    Mugen_Cns cmd_cns;
    u8** file_data;
    u32 file_data_count;
    u32 file_data_capacity;
    bool loaded;
    bool cns_loaded;
} Mugen_Char;

typedef struct {
    Mel_Vfs* vfs;
    str8 def_path;
    str8 stcommon_path;
    const Mel_Alloc* alloc;
} Mugen_Char_Load_Opt;

bool mugen_char_load_opt(Mugen_Char* mc, Mugen_Char_Load_Opt opt);
#define mugen_char_load(mc, ...) mugen_char_load_opt((mc), (Mugen_Char_Load_Opt){__VA_ARGS__})

void mugen_char_shutdown(Mugen_Char* mc, const Mel_Alloc* alloc);
