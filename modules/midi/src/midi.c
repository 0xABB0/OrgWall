#include <midi/midi.h>

#include <string.h>

bool mel_midi_parse(const Mel_Midi_Chunk* chunk, Mel_Midi_Msg* out_msg)
{
    if (!chunk || !out_msg || chunk->length < 1 || chunk->length > 3)
        return false;

    memset(out_msg, 0, sizeof(*out_msg));

    uint8_t status = chunk->data[0];

    if (status >= 0xF8)
    {
        switch (status)
        {
            case 0xF8: out_msg->kind = MEL_MIDI_MSG_SYSTEM_CLOCK;          return true;
            case 0xFA: out_msg->kind = MEL_MIDI_MSG_SYSTEM_START;          return true;
            case 0xFB: out_msg->kind = MEL_MIDI_MSG_SYSTEM_CONTINUE;       return true;
            case 0xFC: out_msg->kind = MEL_MIDI_MSG_SYSTEM_STOP;           return true;
            case 0xFE: out_msg->kind = MEL_MIDI_MSG_SYSTEM_ACTIVE_SENSING; return true;
            case 0xFF: out_msg->kind = MEL_MIDI_MSG_SYSTEM_RESET;          return true;
            default:   return false;
        }
    }

    if (chunk->length < 3)
        return false;

    uint8_t hi_nibble = status & 0xF0;
    uint8_t channel   = status & 0x0F;
    uint8_t data1     = chunk->data[1];
    uint8_t data2     = chunk->data[2];

    if (data1 & 0x80 || data2 & 0x80)
        return false;

    out_msg->channel = channel;

    switch (hi_nibble)
    {
        case MEL_MIDI_STATUS_NOTE_OFF:
            out_msg->kind              = MEL_MIDI_MSG_NOTE_OFF;
            out_msg->note_off.note     = data1;
            out_msg->note_off.velocity = data2;
            return true;

        case MEL_MIDI_STATUS_NOTE_ON:
            out_msg->kind = MEL_MIDI_MSG_NOTE_ON;
            out_msg->note_on.note     = data1;
            out_msg->note_on.velocity = data2;
            if (data2 == 0)
                out_msg->kind = MEL_MIDI_MSG_NOTE_OFF;
            return true;

        case MEL_MIDI_STATUS_KEY_PRESSURE:
            out_msg->kind                 = MEL_MIDI_MSG_KEY_PRESSURE;
            out_msg->key_pressure.note     = data1;
            out_msg->key_pressure.pressure = data2;
            return true;

        case MEL_MIDI_STATUS_CONTROL_CHANGE:
            out_msg->kind                    = MEL_MIDI_MSG_CONTROL_CHANGE;
            out_msg->control_change.controller = data1;
            out_msg->control_change.value      = data2;
            return true;

        case MEL_MIDI_STATUS_PROGRAM_CHANGE:
            out_msg->kind               = MEL_MIDI_MSG_PROGRAM_CHANGE;
            out_msg->program_change.program = data1;
            return true;

        case MEL_MIDI_STATUS_CHANNEL_PRESSURE:
            out_msg->kind                   = MEL_MIDI_MSG_CHANNEL_PRESSURE;
            out_msg->channel_pressure.pressure = data1;
            return true;

        case MEL_MIDI_STATUS_PITCH_BEND:
        {
            out_msg->kind = MEL_MIDI_MSG_PITCH_BEND;
            int32_t raw = (int32_t)data1 | ((int32_t)data2 << 7);
            out_msg->pitch_bend.bend = (int16_t)(raw - 8192);
            return true;
        }

        default:
            return false;
    }
}

bool mel_midi_encode(const Mel_Midi_Msg* msg, Mel_Midi_Chunk* out_chunk)
{
    if (!msg || !out_chunk) return false;

    memset(out_chunk, 0, sizeof(*out_chunk));

    switch (msg->kind)
    {
        case MEL_MIDI_MSG_NOTE_OFF:
            out_chunk->data[0] = MEL_MIDI_STATUS_NOTE_OFF | (msg->channel & 0x0F);
            out_chunk->data[1] = msg->note_off.note & 0x7F;
            out_chunk->data[2] = msg->note_off.velocity & 0x7F;
            out_chunk->length  = 3;
            return true;

        case MEL_MIDI_MSG_NOTE_ON:
            out_chunk->data[0] = MEL_MIDI_STATUS_NOTE_ON | (msg->channel & 0x0F);
            out_chunk->data[1] = msg->note_on.note & 0x7F;
            out_chunk->data[2] = msg->note_on.velocity & 0x7F;
            out_chunk->length  = 3;
            return true;

        case MEL_MIDI_MSG_KEY_PRESSURE:
            out_chunk->data[0] = MEL_MIDI_STATUS_KEY_PRESSURE | (msg->channel & 0x0F);
            out_chunk->data[1] = msg->key_pressure.note & 0x7F;
            out_chunk->data[2] = msg->key_pressure.pressure & 0x7F;
            out_chunk->length  = 3;
            return true;

        case MEL_MIDI_MSG_CONTROL_CHANGE:
            out_chunk->data[0] = MEL_MIDI_STATUS_CONTROL_CHANGE | (msg->channel & 0x0F);
            out_chunk->data[1] = msg->control_change.controller & 0x7F;
            out_chunk->data[2] = msg->control_change.value & 0x7F;
            out_chunk->length  = 3;
            return true;

        case MEL_MIDI_MSG_PROGRAM_CHANGE:
            out_chunk->data[0] = MEL_MIDI_STATUS_PROGRAM_CHANGE | (msg->channel & 0x0F);
            out_chunk->data[1] = msg->program_change.program & 0x7F;
            out_chunk->length  = 2;
            return true;

        case MEL_MIDI_MSG_CHANNEL_PRESSURE:
            out_chunk->data[0] = MEL_MIDI_STATUS_CHANNEL_PRESSURE | (msg->channel & 0x0F);
            out_chunk->data[1] = msg->channel_pressure.pressure & 0x7F;
            out_chunk->length  = 2;
            return true;

        case MEL_MIDI_MSG_PITCH_BEND:
        {
            int32_t raw = (int32_t)msg->pitch_bend.bend + 8192;
            if (raw < 0) raw = 0;
            if (raw > 16383) raw = 16383;
            out_chunk->data[0] = MEL_MIDI_STATUS_PITCH_BEND | (msg->channel & 0x0F);
            out_chunk->data[1] = (uint8_t)(raw & 0x7F);
            out_chunk->data[2] = (uint8_t)((raw >> 7) & 0x7F);
            out_chunk->length  = 3;
            return true;
        }

        case MEL_MIDI_MSG_SYSTEM_CLOCK:          out_chunk->data[0] = 0xF8; out_chunk->length = 1; return true;
        case MEL_MIDI_MSG_SYSTEM_START:          out_chunk->data[0] = 0xFA; out_chunk->length = 1; return true;
        case MEL_MIDI_MSG_SYSTEM_CONTINUE:       out_chunk->data[0] = 0xFB; out_chunk->length = 1; return true;
        case MEL_MIDI_MSG_SYSTEM_STOP:           out_chunk->data[0] = 0xFC; out_chunk->length = 1; return true;
        case MEL_MIDI_MSG_SYSTEM_ACTIVE_SENSING: out_chunk->data[0] = 0xFE; out_chunk->length = 1; return true;
        case MEL_MIDI_MSG_SYSTEM_RESET:          out_chunk->data[0] = 0xFF; out_chunk->length = 1; return true;

        case MEL_MIDI_MSG_NONE:
        default:
            return false;
    }
}

const char* mel_midi_msg_kind_name(Mel_Midi_Msg_Kind kind)
{
    switch (kind)
    {
        case MEL_MIDI_MSG_NONE:               return "none";
        case MEL_MIDI_MSG_NOTE_OFF:           return "note_off";
        case MEL_MIDI_MSG_NOTE_ON:            return "note_on";
        case MEL_MIDI_MSG_KEY_PRESSURE:       return "key_pressure";
        case MEL_MIDI_MSG_CONTROL_CHANGE:     return "control_change";
        case MEL_MIDI_MSG_PROGRAM_CHANGE:     return "program_change";
        case MEL_MIDI_MSG_CHANNEL_PRESSURE:   return "channel_pressure";
        case MEL_MIDI_MSG_PITCH_BEND:         return "pitch_bend";
        case MEL_MIDI_MSG_SYSTEM_CLOCK:       return "system_clock";
        case MEL_MIDI_MSG_SYSTEM_START:       return "system_start";
        case MEL_MIDI_MSG_SYSTEM_CONTINUE:    return "system_continue";
        case MEL_MIDI_MSG_SYSTEM_STOP:        return "system_stop";
        case MEL_MIDI_MSG_SYSTEM_ACTIVE_SENSING: return "system_active_sensing";
        case MEL_MIDI_MSG_SYSTEM_RESET:       return "system_reset";
        default:                              return "unknown";
    }
}
