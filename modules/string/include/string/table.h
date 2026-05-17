#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include "str8.fwd.h"

typedef u32 Mel_Atom;

#define MEL_ATOM_NONE ((Mel_Atom)0)

typedef struct Mel_Atom_Table Mel_Atom_Table;

Mel_Atom_Table* mel_atom_table_create(const Mel_Alloc* alloc);
void            mel_atom_table_destroy(Mel_Atom_Table* t);

Mel_Atom mel_atom_intern(Mel_Atom_Table* t, str8 s);
Mel_Atom mel_atom_lookup(const Mel_Atom_Table* t, str8 s);
str8     mel_atom_get(const Mel_Atom_Table* t, Mel_Atom atom);
u32      mel_atom_count(const Mel_Atom_Table* t);
