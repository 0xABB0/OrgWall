#pragma once

#include <core/compiler.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ── MIDI protocol constants ────────────────────────────────────────────────

#define MEL_MIDI_NOTE_MAX      127
#define MEL_MIDI_NOTE_MIN      0
#define MEL_MIDI_NOTE_COUNT    128
#define MEL_MIDI_MIDDLE_C      60
#define MEL_MIDI_VELOCITY_MAX  127
#define MEL_MIDI_CC_MAX        127

// ── Status byte nibbles ────────────────────────────────────────────────────

typedef enum
{
    MEL_MIDI_STATUS_NOTE_OFF         = 0x80,
    MEL_MIDI_STATUS_NOTE_ON          = 0x90,
    MEL_MIDI_STATUS_KEY_PRESSURE     = 0xA0,
    MEL_MIDI_STATUS_CONTROL_CHANGE   = 0xB0,
    MEL_MIDI_STATUS_PROGRAM_CHANGE   = 0xC0,
    MEL_MIDI_STATUS_CHANNEL_PRESSURE = 0xD0,
    MEL_MIDI_STATUS_PITCH_BEND       = 0xE0,
    MEL_MIDI_STATUS_SYSTEM           = 0xF0,
} Mel_Midi_Status;

// ── Parsed MIDI message (tagged union) ─────────────────────────────────────

typedef enum
{
    MEL_MIDI_MSG_NONE,
    MEL_MIDI_MSG_NOTE_OFF,
    MEL_MIDI_MSG_NOTE_ON,
    MEL_MIDI_MSG_KEY_PRESSURE,
    MEL_MIDI_MSG_CONTROL_CHANGE,
    MEL_MIDI_MSG_PROGRAM_CHANGE,
    MEL_MIDI_MSG_CHANNEL_PRESSURE,
    MEL_MIDI_MSG_PITCH_BEND,
    MEL_MIDI_MSG_SYSTEM_CLOCK,
    MEL_MIDI_MSG_SYSTEM_START,
    MEL_MIDI_MSG_SYSTEM_CONTINUE,
    MEL_MIDI_MSG_SYSTEM_STOP,
    MEL_MIDI_MSG_SYSTEM_ACTIVE_SENSING,
    MEL_MIDI_MSG_SYSTEM_RESET,
} Mel_Midi_Msg_Kind;

typedef struct Mel_Midi_Msg Mel_Midi_Msg;

struct Mel_Midi_Msg
{
    Mel_Midi_Msg_Kind kind;
    uint8_t channel;
    union
    {
        struct { uint8_t note;        uint8_t velocity; } note_on;
        struct { uint8_t note;        uint8_t velocity; } note_off;
        struct { uint8_t note;        uint8_t pressure; } key_pressure;
        struct { uint8_t controller;  uint8_t value;    } control_change;
        struct { uint8_t program;                       } program_change;
        struct { uint8_t pressure;                      } channel_pressure;
        struct { int16_t  bend;                         } pitch_bend;
    };
};

// ── Raw MIDI chunk ─────────────────────────────────────────────────────────

typedef struct Mel_Midi_Chunk Mel_Midi_Chunk;
struct Mel_Midi_Chunk
{
    uint64_t timestamp_us;
    uint8_t  data[3];
    int32_t  length;
};

// ── Message parsing ────────────────────────────────────────────────────────

bool mel_midi_parse(const Mel_Midi_Chunk* chunk, Mel_Midi_Msg* out_msg);
bool mel_midi_encode(const Mel_Midi_Msg* msg, Mel_Midi_Chunk* out_chunk);
const char* mel_midi_msg_kind_name(Mel_Midi_Msg_Kind kind);
