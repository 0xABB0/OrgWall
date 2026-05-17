#pragma once

#include "core.types.h"
#include "mugen.char.h"
#include "collection.slotmap.h"
#include "str8.h"
#include "allocator.fwd.h"

typedef Mel_SlotMap_Handle Mugen_Char_Handle;

typedef struct {
    str8 name;
    Mugen_Char ch;
} Mugen_Roster_Entry;

typedef struct {
    Mel_SlotMap entries;
    str8 stcommon_path;
    const Mel_Alloc* alloc;
} Mugen_Roster;

typedef struct {
    str8 stcommon_path;
    const Mel_Alloc* alloc;
} Mugen_Roster_Init_Opt;

void mugen_roster_init_opt(Mugen_Roster* r, Mugen_Roster_Init_Opt opt);
#define mugen_roster_init(r, ...) mugen_roster_init_opt((r), (Mugen_Roster_Init_Opt){__VA_ARGS__})

void mugen_roster_shutdown(Mugen_Roster* r);

void mugen_roster_load(Mugen_Roster* r, str8 folder_path);

Mugen_Char*     mugen_roster_find(Mugen_Roster* r, str8 name);
Mugen_Char*     mugen_roster_at(Mugen_Roster* r, u32 index);
str8            mugen_roster_name_at(Mugen_Roster* r, u32 index);
Mugen_Char*     mugen_roster_get(Mugen_Roster* r, Mugen_Char_Handle h);
u32             mugen_roster_count(Mugen_Roster* r);
