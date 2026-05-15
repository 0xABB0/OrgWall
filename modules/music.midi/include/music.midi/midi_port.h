#pragma once

#include <core/compiler.h>

#include <stdint.h>
#include <stdbool.h>

#include "midi.h"

// ── Opaque port handle ─────────────────────────────────────────────────────

typedef struct Mel_Midi_Port Mel_Midi_Port;

// ── Device descriptor ──────────────────────────────────────────────────────

typedef struct Mel_Midi_Port_Info Mel_Midi_Port_Info;
struct Mel_Midi_Port_Info
{
    int32_t     id;
    const char* name;
    bool        is_input;
};

// ── API ────────────────────────────────────────────────────────────────────

int32_t        mel_midi_port_enumerate_inputs(Mel_Midi_Port_Info* out_infos, int32_t max_count);
Mel_Midi_Port* mel_midi_port_open_input(int32_t id);
void           mel_midi_port_close(Mel_Midi_Port* port);
bool           mel_midi_port_poll(Mel_Midi_Port* port, Mel_Midi_Chunk* out_chunk);
bool           mel_midi_port_read_blocking(Mel_Midi_Port* port, Mel_Midi_Chunk* out_chunk);
int32_t        mel_midi_port_id(const Mel_Midi_Port* port);
const char*    mel_midi_port_name(const Mel_Midi_Port* port);
