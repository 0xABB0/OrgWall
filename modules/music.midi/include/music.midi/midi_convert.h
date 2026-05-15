#pragma once

#include <core/compiler.h>

#include <stdint.h>
#include "midi.h"

typedef struct Mel_Tuning Mel_Tuning;
typedef struct Mel_Pitch  Mel_Pitch;

Mel_Pitch mel_midi_note_to_pitch(uint8_t midi_note, const Mel_Tuning* tuning);
uint8_t   mel_midi_pitch_to_note(Mel_Pitch pitch);

static inline float mel_midi_velocity_normalised(uint8_t velocity)
{
    return (float)velocity / 127.0f;
}

static inline uint8_t mel_midi_velocity_from_normalised(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 127;
    return (uint8_t)(v * 127.0f + 0.5f);
}

static inline float mel_midi_bend_normalised(int16_t bend)
{
    return (float)bend / 8191.0f;
}

Mel_Pitch mel_midi_note_with_bend(uint8_t midi_note, int16_t bend,
                                   float bend_range_semitones,
                                   const Mel_Tuning* tuning);
