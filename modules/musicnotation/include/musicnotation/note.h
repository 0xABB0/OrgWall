#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <musictheory/pitch.h>
#include <musictheory/interval.h>

typedef struct Mel_Note         Mel_Note;
typedef struct Mel_NoteInterval Mel_NoteInterval;

struct Mel_Note
{
    Mel_Pitch pitch;
    i32       nat_class;
    i32       nat_bi_index;
    i32       acc_value;
};

struct Mel_NoteInterval
{
    Mel_Interval pitch_interval;
    i32          nat_diff;
    i32          number;
    i32          quality_value;
};

MEL_NODISCARD static inline Mel_Pitch mel_note_pitch(Mel_Note n) { return n.pitch; }

MEL_NODISCARD static inline i32 mel_note_bi_index(Mel_Note n) { return n.nat_bi_index; }

MEL_NODISCARD static inline i32 mel_note_acc_value(Mel_Note n) { return n.acc_value; }

MEL_NODISCARD Mel_Note mel_note_transpose_bi(Mel_Note n, i32 bi_diff);

MEL_NODISCARD u8 mel_note_eq(Mel_Note a, Mel_Note b);
