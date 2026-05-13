#pragma once

#include <core/compiler.h>

#include "notation.h"
#include "note.h"
#include "interval_seq.h"

typedef struct Mel_NoteIntervalSeq Mel_NoteIntervalSeq;

struct Mel_NoteIntervalSeq
{
  const Mel_Notation* notation;
  Mel_IntervalSeq interval_seq;
};

void mel_note_interval_seq_free(Mel_NoteIntervalSeq* nis);
static inline void mel_note_interval_seq_cleanup(Mel_NoteIntervalSeq* nis) { mel_note_interval_seq_free(nis); }
#define Mel_NoteIntervalSeq_AUTO MEL_CLEANUP(mel_note_interval_seq_cleanup) Mel_NoteIntervalSeq

Mel_NoteIntervalSeq mel_note_interval_seq_make(const Mel_Notation* n, Mel_NoteInterval* intervals, int32_t count);
