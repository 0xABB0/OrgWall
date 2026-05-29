#include <midi/midi_convert.h>

#include <musictheory/pitch.h>
#include <musictheory/tuning.h>

Mel_Pitch mel_midi_note_to_pitch(uint8_t midi_note, const Mel_Tuning* tuning)
{
    int64_t pitch_index = (int64_t)midi_note - MEL_MIDI_MIDDLE_C;
    return mel_pitch_make(tuning, pitch_index);
}

uint8_t mel_midi_pitch_to_note(Mel_Pitch pitch)
{
    int64_t idx = mel_pitch_pc_index(pitch);
    int64_t midi = idx + MEL_MIDI_MIDDLE_C;
    if (midi < MEL_MIDI_NOTE_MIN) return MEL_MIDI_NOTE_MIN;
    if (midi > MEL_MIDI_NOTE_MAX) return MEL_MIDI_NOTE_MAX;
    return (uint8_t)midi;
}

Mel_Pitch mel_midi_note_with_bend(uint8_t midi_note, int16_t bend,
                                   float bend_range_semitones,
                                   const Mel_Tuning* tuning)
{
    float bend_semitones = ((float)bend / 8191.0f) * bend_range_semitones;
    int64_t base_index = (int64_t)midi_note - MEL_MIDI_MIDDLE_C;
    int64_t bend_steps = (int64_t)(bend_semitones + (bend_semitones >= 0 ? 0.5f : -0.5f));
    return mel_pitch_make(tuning, base_index + bend_steps);
}
