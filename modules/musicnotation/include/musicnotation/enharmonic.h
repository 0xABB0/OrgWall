#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <allocator/allocator.h>
#include <musictheory/pitch.h>

#include "note.h"

typedef struct Mel_Notation Mel_Notation;

typedef Mel_Note (*Mel_EnharmonicGuessNote)(void* ctx, const Mel_Notation* n, Mel_Pitch pitch);
typedef Mel_Note (*Mel_EnharmonicNoteTranspose)(void* ctx, Mel_Note note, i64 pitch_diff);
typedef void (*Mel_EnharmonicDestroy)(void* ctx, const Mel_Alloc* alloc);

typedef struct Mel_EnharmonicStrategy Mel_EnharmonicStrategy;

struct Mel_EnharmonicStrategy
{
    void*                       ctx;
    const Mel_Alloc*            alloc;
    Mel_EnharmonicGuessNote     guess_note;
    Mel_EnharmonicNoteTranspose note_transpose;
    Mel_EnharmonicDestroy       destroy;
};

MEL_NODISCARD Mel_EnharmonicStrategy mel_enharmonic_pc_blueprint(const Mel_Alloc* alloc, const Mel_Note* blueprint, i32 count);
