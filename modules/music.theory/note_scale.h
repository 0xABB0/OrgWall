#pragma once

#include <core/compiler.h>

#include "notation.h"
#include "note.h"
#include "scale.h"

typedef struct Mel_NoteScale Mel_NoteScale;

struct Mel_NoteScale
{
  const Mel_Notation* notation;
  Mel_Scale pitch_scale;
};

void mel_note_scale_free(Mel_NoteScale* ns);
static inline void mel_note_scale_cleanup(Mel_NoteScale* ns) { mel_note_scale_free(ns); }
#define Mel_NoteScale_AUTO MEL_CLEANUP(mel_note_scale_cleanup) Mel_NoteScale

Mel_NoteScale mel_note_scale_make(const Mel_Notation* n, Mel_Note* notes, int32_t count);

int32_t mel_note_scale_count(const Mel_NoteScale* ns);

Mel_Note mel_note_scale_get(const Mel_NoteScale* ns, int32_t idx);

Mel_NoteScale mel_note_scale_copy(const Mel_NoteScale* ns);

Mel_NoteScale mel_note_scale_transpose(const Mel_NoteScale* ns, int64_t diff);
