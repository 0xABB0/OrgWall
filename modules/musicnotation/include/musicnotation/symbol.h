#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>

typedef struct Mel_SymbolCodeEntry Mel_SymbolCodeEntry;

struct Mel_SymbolCodeEntry
{
    str8 symbol;
    i32  value;
    i32  position;
};

typedef Mel_Array(Mel_SymbolCodeEntry) Mel_SymbolCodeEntry_Array;

typedef struct Mel_SymbolCode Mel_SymbolCode;

struct Mel_SymbolCode
{
    Mel_SymbolCodeEntry_Array entries;
};

void               mel_symbol_code_free(Mel_SymbolCode* sc);
static inline void mel_symbol_code_cleanup(Mel_SymbolCode* sc) { mel_symbol_code_free(sc); }
#define Mel_SymbolCode_AUTO MEL_CLEANUP(mel_symbol_code_cleanup) Mel_SymbolCode

MEL_NODISCARD Mel_SymbolCode mel_symbol_code_make(const Mel_Alloc* alloc);

void mel_symbol_code_add(Mel_SymbolCode* sc, str8 symbol, i32 value);

void mel_symbol_code_add_at(Mel_SymbolCode* sc, str8 symbol, i32 value, i32 position);

MEL_NODISCARD bool mel_symbol_code_parse(const Mel_SymbolCode* sc, str8 text, i32* out_value);

MEL_NODISCARD bool mel_symbol_code_generate(const Mel_SymbolCode* sc, i32 value, const Mel_Alloc* alloc, str8* out);
