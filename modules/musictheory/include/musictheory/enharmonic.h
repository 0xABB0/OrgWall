#pragma once

#include <stdint.h>

typedef struct Mel_Notation Mel_Notation;
typedef struct Mel_Note Mel_Note;
typedef struct Mel_NoteScale Mel_NoteScale;
typedef struct Mel_NoteInterval Mel_NoteInterval;
typedef struct Mel_Pitch Mel_Pitch;

typedef Mel_Note (*Mel_EnharmonicGuessNote)(void* ctx, Mel_Notation* n, Mel_Pitch pitch);
typedef Mel_Note (*Mel_EnharmonicNoteTranspose)(void* ctx, Mel_Note note, int64_t pitch_diff);
typedef Mel_NoteScale (*Mel_EnharmonicNoteScaleTranspose)(void* ctx, Mel_NoteScale* scale, int64_t pitch_diff);
typedef Mel_NoteScale (*Mel_EnharmonicNoteScaleComplement)(void* ctx, Mel_NoteScale* scale);

typedef struct Mel_EnharmonicStrategy Mel_EnharmonicStrategy;

struct Mel_EnharmonicStrategy
{
  void* ctx;
  Mel_EnharmonicGuessNote guess_note;
  Mel_EnharmonicNoteTranspose note_transpose;
  Mel_EnharmonicNoteScaleTranspose note_scale_transpose;
  Mel_EnharmonicNoteScaleComplement note_scale_complement;
};

Mel_EnharmonicStrategy mel_enharmonic_pc_blueprint(Mel_Note* blueprint, int32_t count);
