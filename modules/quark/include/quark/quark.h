#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <string/str8.fwd.h>

typedef u32 Mel_Quark;

#define MEL_QUARK_NONE ((Mel_Quark)0)

typedef struct Mel_Quark_Table Mel_Quark_Table;

Mel_Quark_Table* mel_quark_table_create(const Mel_Alloc* alloc);
void             mel_quark_table_destroy(Mel_Quark_Table* t);

Mel_Quark mel_quark_intern(Mel_Quark_Table* t, str8 s);
Mel_Quark mel_quark_lookup(const Mel_Quark_Table* t, str8 s);
str8      mel_quark_get(const Mel_Quark_Table* t, Mel_Quark q);
u32       mel_quark_count(const Mel_Quark_Table* t);
