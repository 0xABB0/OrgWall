#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <allocator/allocator.h>
#include <musictuning/tuning.h>
#include <musictheory/pattern.h>

#include "nat_acc.h"
#include "chord_id.h"

MEL_NODISCARD Mel_Tuning mel_tuning_western(const Mel_Alloc* alloc, Mel_Hz a4);

MEL_NODISCARD Mel_NatAccNotation mel_notation_western(const Mel_Alloc* alloc, const Mel_Tuning* tuning);

MEL_NODISCARD Mel_Note mel_western_note(const Mel_NatAccNotation* nn, i32 nat_class, i32 octave, i32 acc_value);

MEL_NODISCARD static inline i64 mel_western_midi_to_index(i32 midi_note) { return (i64)midi_note - 60; }
MEL_NODISCARD static inline i32 mel_western_index_to_midi(i64 index) { return (i32)(index + 60); }

MEL_NODISCARD Mel_Pattern mel_western_pattern_major(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_western_pattern_natural_minor(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_western_pattern_harmonic_minor(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_western_pattern_melodic_minor(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_western_pattern_mode(const Mel_Alloc* alloc, const Mel_Tuning* tuning, i32 degree);
MEL_NODISCARD Mel_Pattern mel_western_pattern_major_pentatonic(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_western_pattern_minor_pentatonic(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_western_pattern_blues(const Mel_Alloc* alloc, const Mel_Tuning* tuning);
MEL_NODISCARD Mel_Pattern mel_western_pattern_chromatic(const Mel_Alloc* alloc, const Mel_Tuning* tuning);

MEL_NODISCARD Mel_Chord_Catalog mel_chord_catalog_western(const Mel_Alloc* alloc);
