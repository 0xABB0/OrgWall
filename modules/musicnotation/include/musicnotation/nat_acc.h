#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <musictuning/tuning.h>

#include "notation.h"
#include "note.h"
#include "symbol.h"

typedef struct Mel_NatEntry Mel_NatEntry;

struct Mel_NatEntry
{
    str8 symbol;
    i32  pitch_index;
};

typedef Mel_Array(Mel_NatEntry) Mel_NatEntry_Array;

typedef struct Mel_IntervalCode Mel_IntervalCode;

struct Mel_IntervalCode
{
    Mel_SymbolCode code;
    i32            reference_steps;
};

typedef Mel_Array(Mel_IntervalCode) Mel_IntervalCode_Array;

typedef struct Mel_NatAccNotation Mel_NatAccNotation;

struct Mel_NatAccNotation
{
    Mel_Notation           base;
    const Mel_Alloc*       alloc;
    Mel_NatEntry_Array     naturals;
    Mel_SymbolCode         acc_code;
    Mel_IntervalCode_Array interval_codes;
};

void               mel_nat_acc_notation_free(Mel_NatAccNotation* nn);
static inline void mel_nat_acc_notation_cleanup(Mel_NatAccNotation* nn) { mel_nat_acc_notation_free(nn); }
#define Mel_NatAccNotation_AUTO MEL_CLEANUP(mel_nat_acc_notation_cleanup) Mel_NatAccNotation

MEL_NODISCARD Mel_NatAccNotation mel_nat_acc_notation_make(const Mel_Alloc* alloc, const Mel_Tuning* tuning);

void mel_nat_acc_add_natural(Mel_NatAccNotation* nn, str8 symbol, i32 pitch_index);

void mel_nat_acc_add_accidental(Mel_NatAccNotation* nn, str8 symbol, i32 value);

void mel_nat_acc_add_accidental_at(Mel_NatAccNotation* nn, str8 symbol, i32 value, i32 position);

void mel_nat_acc_set_interval_code(Mel_NatAccNotation* nn, i32 nat_diff_class, Mel_SymbolCode code, i32 reference_steps);

MEL_NODISCARD bool mel_nat_acc_note(Mel_Note* out, const Mel_NatAccNotation* nn, str8 symbol, i32 nat_bi_index);

MEL_NODISCARD Mel_Note mel_nat_acc_note_by_class(const Mel_NatAccNotation* nn, i32 nat_class, i32 nat_bi_index, i32 acc_value);

MEL_NODISCARD Mel_NoteInterval mel_nat_acc_interval(const Mel_NatAccNotation* nn, Mel_Note source, Mel_Note target);

MEL_NODISCARD str8 mel_nat_acc_note_symbol(const Mel_NatAccNotation* nn, Mel_Note note, const Mel_Alloc* alloc);

MEL_NODISCARD str8 mel_nat_acc_interval_symbol(const Mel_NatAccNotation* nn, Mel_NoteInterval ni, const Mel_Alloc* alloc);
