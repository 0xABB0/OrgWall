#pragma once

#include <core/compiler.h>

#include "notation.h"
#include "note.h"
#include "symbol.h"

typedef struct Mel_NatAccNotation Mel_NatAccNotation;

struct Mel_NatAccNotation
{
  Mel_Notation base;

  struct
  {
    char** symbols;
    int32_t* pitch_indices;
    int32_t count;
  } naturals;

  Mel_SymbolCode acc_code;

  struct
  {
    Mel_SymbolCode* codes;
    int32_t count;
  } interval_codes;
};

void mel_nat_acc_notation_free(Mel_NatAccNotation* nn);
static inline void mel_nat_acc_notation_cleanup(Mel_NatAccNotation* nn) { mel_nat_acc_notation_free(nn); }
#define Mel_NatAccNotation_AUTO MEL_CLEANUP(mel_nat_acc_notation_cleanup) Mel_NatAccNotation

Mel_NatAccNotation mel_nat_acc_notation_make(const Mel_Tuning* tuning);

void mel_nat_acc_add_natural(Mel_NatAccNotation* nn, const char* symbol, int32_t pitch_index);

void mel_nat_acc_add_accidental(Mel_NatAccNotation* nn, const char* symbol, int32_t value);

void mel_nat_acc_add_accidental_at(Mel_NatAccNotation* nn, const char* symbol, int32_t value, int32_t position);

void mel_nat_acc_set_interval_code(Mel_NatAccNotation* nn, int32_t nat_diff_class, Mel_SymbolCode code);

Mel_Note mel_nat_acc_note(Mel_NatAccNotation* nn, const char* symbol, int32_t nat_bi_index);

Mel_Note mel_nat_acc_note_by_index(Mel_NatAccNotation* nn, int32_t nat_index, int32_t* acc_vector, int32_t acc_count);

Mel_NoteInterval mel_nat_acc_interval(Mel_NatAccNotation* nn, Mel_Note source, Mel_Note target);

char* mel_nat_acc_note_symbol(Mel_NatAccNotation* nn, Mel_Note note);
