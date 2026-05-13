#pragma once

#include <core/compiler.h>

#include "tuning.h"
#include "enharmonic.h"

typedef struct Mel_Note Mel_Note;
typedef struct Mel_NoteInterval Mel_NoteInterval;
typedef struct Mel_NoteScale Mel_NoteScale;
typedef struct Mel_NoteIntervalSeq Mel_NoteIntervalSeq;

typedef struct Mel_Notation Mel_Notation;

struct Mel_Notation
{
  const Mel_Tuning* tuning;
  Mel_EnharmonicStrategy enharmonic;
};

void mel_notation_free(Mel_Notation* n);
static inline void mel_notation_cleanup(Mel_Notation* n) { mel_notation_free(n); }
#define Mel_Notation_AUTO MEL_CLEANUP(mel_notation_cleanup) Mel_Notation

Mel_Notation mel_notation_make(const Mel_Tuning* tuning);

void mel_notation_set_enharmonic(Mel_Notation* n, Mel_EnharmonicStrategy strategy);

Mel_Note mel_notation_guess_note(const Mel_Notation* n, Mel_Pitch pitch);

Mel_NoteInterval mel_notation_interval(const Mel_Notation* n, Mel_Note source, Mel_Note target);

Mel_NoteScale mel_notation_scale(const Mel_Notation* n, Mel_Note* notes, int32_t count);

Mel_NoteIntervalSeq mel_notation_interval_seq(const Mel_Notation* n, Mel_NoteInterval* intervals, int32_t count);
