#pragma once

#include <core/compiler.h>

#include "pitch.h"
#include "interval.h"

typedef struct Mel_Notation Mel_Notation;
typedef struct Mel_Note Mel_Note;
typedef struct Mel_NoteInterval Mel_NoteInterval;

struct Mel_Note
{
  const Mel_Notation* notation;
  Mel_Pitch pitch;
  char* symbol;
  int32_t nat_bi_index;
  int32_t* acc_vector;
  int32_t acc_count;
};

struct Mel_NoteInterval
{
  const Mel_Notation* notation;
  Mel_Interval pitch_interval;
  int32_t nat_diff;
  int32_t* acc_vector;
  int32_t acc_count;
  char* symbol;
  int32_t number;
};

void mel_note_free(Mel_Note* n);
static inline void mel_note_cleanup(Mel_Note* n) { mel_note_free(n); }
#define Mel_Note_AUTO MEL_CLEANUP(mel_note_cleanup) Mel_Note

void mel_note_interval_free(Mel_NoteInterval* ni);
static inline void mel_note_interval_cleanup(Mel_NoteInterval* ni) { mel_note_interval_free(ni); }
#define Mel_NoteInterval_AUTO MEL_CLEANUP(mel_note_interval_cleanup) Mel_NoteInterval

Mel_Note mel_note_copy(Mel_Note n);

Mel_Pitch mel_note_pitch(Mel_Note n);

int32_t mel_note_bi_index(Mel_Note n);

int32_t mel_note_acc_value(Mel_Note n);

Mel_Note mel_note_transpose_bi(Mel_Note n, int32_t bi_diff);

uint8_t mel_note_eq(Mel_Note a, Mel_Note b);

Mel_NoteInterval mel_note_interval_copy(Mel_NoteInterval ni);

int32_t mel_note_interval_acc_value(Mel_NoteInterval ni);
