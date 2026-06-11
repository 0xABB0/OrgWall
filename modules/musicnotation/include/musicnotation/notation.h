#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <musictuning/tuning.h>
#include <musictheory/pitch.h>

#include "note.h"
#include "enharmonic.h"

struct Mel_Notation
{
    const Mel_Tuning*      tuning;
    Mel_EnharmonicStrategy enharmonic;
};

void               mel_notation_free(Mel_Notation* n);
static inline void mel_notation_cleanup(Mel_Notation* n) { mel_notation_free(n); }
#define Mel_Notation_AUTO MEL_CLEANUP(mel_notation_cleanup) Mel_Notation

MEL_NODISCARD Mel_Notation mel_notation_make(const Mel_Tuning* tuning);

void mel_notation_set_enharmonic(Mel_Notation* n, Mel_EnharmonicStrategy strategy);

MEL_NODISCARD Mel_Note mel_notation_guess_note(const Mel_Notation* n, Mel_Pitch pitch);

MEL_NODISCARD Mel_Note mel_notation_note_transpose(const Mel_Notation* n, Mel_Note note, i64 pitch_diff);
