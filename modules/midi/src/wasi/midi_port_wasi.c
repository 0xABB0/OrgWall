#include <midi/midi_port_priv.h>

#include <core/platform.h>

#if !MEL_PLATFORM_WASI
    #error "This file should only be compiled for the wasi runtime"
#endif

// wasi is a DOM-less compute sandbox: there is no Web MIDI API and no host MIDI
// device access, so the port backend enumerates nothing. midi_convert.c and the
// theory stack still work — only live hardware input is unavailable here.

int32_t mel_midi_port_platform_enumerate(Mel_Midi_Port_Info* out_infos, int32_t max_count)
{
    (void)out_infos;
    (void)max_count;
    return 0;
}

Mel_Midi_Port* mel_midi_port_platform_open_input(int32_t id, Mel_Midi_Port* port)
{
    (void)id;
    (void)port;
    return NULL;
}

void mel_midi_port_platform_close(Mel_Midi_Port* port)
{
    (void)port;
}
