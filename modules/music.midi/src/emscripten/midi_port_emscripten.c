#include <music.midi/midi_port_priv.h>

#include <core/platform.h>

#if !MEL_PLATFORM_EMSCRIPTEN
    #error "This file should only be compiled for the emscripten runtime"
#endif

#include <emscripten.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void mel_midi_port_push_chunk(Mel_Midi_Port* port, const Mel_Midi_Chunk* chunk);

#define MEL_WEB_MIDI_MAX  64
#define MEL_WEB_MIDI_NAME 128

// Web MIDI access resolves asynchronously; we kick the request off once and let
// the JS side keep a live snapshot of the input ports. The synchronous C API
// reads that snapshot — it reports nothing until access resolves, then tracks
// device hot-plug through onstatechange. onmidimessage forwards each event to
// the exported dispatcher with the C port pointer it was opened against.
EM_JS(void, mel_midi_web__ensure_init, (void), {
    if (globalThis.MelMidi) return;
    globalThis.MelMidi = { access: null, inputs: [], open: {}, ready: false };
    if (!navigator.requestMIDIAccess) { MelMidi.ready = true; return; }
    navigator.requestMIDIAccess({ sysex: false }).then((access) => {
        MelMidi.access = access;
        const refresh = () => { MelMidi.inputs = Array.from(access.inputs.values()); };
        refresh();
        access.onstatechange = refresh;
        MelMidi.ready = true;
    }, () => { MelMidi.ready = true; });
});

EM_JS(int, mel_midi_web__count, (void), {
    return (globalThis.MelMidi && MelMidi.ready) ? MelMidi.inputs.length : 0;
});

EM_JS(void, mel_midi_web__name, (int idx, char* buf, int cap), {
    const inp = globalThis.MelMidi && MelMidi.inputs[idx];
    stringToUTF8(inp && inp.name ? inp.name : '', buf, cap);
});

EM_JS(int, mel_midi_web__open, (int idx, void* port), {
    const inp = globalThis.MelMidi && MelMidi.inputs[idx];
    if (!inp) return 0;
    MelMidi.open[port] = inp;
    inp.onmidimessage = (e) => {
        const d = e.data;
        _mel_midi_web__on_message(port, d.length > 0 ? d[0] : 0, d.length > 1 ? d[1] : 0,
                                  d.length > 2 ? d[2] : 0, d.length, e.timeStamp);
    };
    return 1;
});

EM_JS(void, mel_midi_web__close, (void* port), {
    const inp = globalThis.MelMidi && MelMidi.open[port];
    if (inp) { inp.onmidimessage = null; delete MelMidi.open[port]; }
});

EMSCRIPTEN_KEEPALIVE void mel_midi_web__on_message(void* port_p, int b0, int b1, int b2,
                                                    int len, double ts_ms)
{
    Mel_Midi_Port* port = (Mel_Midi_Port*)port_p;
    if (!port) return;

    if (len < 0) len = 0;
    if (len > 3) len = 3;

    Mel_Midi_Chunk chunk = {0};
    chunk.timestamp_us = (uint64_t)(ts_ms * 1000.0);
    if (len > 0) chunk.data[0] = (uint8_t)b0;
    if (len > 1) chunk.data[1] = (uint8_t)b1;
    if (len > 2) chunk.data[2] = (uint8_t)b2;
    chunk.length = len;

    mel_midi_port_push_chunk(port, &chunk);
}

int32_t mel_midi_port_platform_enumerate(Mel_Midi_Port_Info* out_infos, int32_t max_count)
{
    static char names[MEL_WEB_MIDI_MAX][MEL_WEB_MIDI_NAME];

    mel_midi_web__ensure_init();

    int32_t count = mel_midi_web__count();
    if (count > MEL_WEB_MIDI_MAX) count = MEL_WEB_MIDI_MAX;

    int32_t n = 0;
    for (int32_t i = 0; i < count && n < max_count; i++)
    {
        if (out_infos)
        {
            mel_midi_web__name(i, names[n], MEL_WEB_MIDI_NAME);
            out_infos[n].id       = i;
            out_infos[n].name     = names[n];
            out_infos[n].is_input = true;
        }
        n++;
    }
    return n;
}

Mel_Midi_Port* mel_midi_port_platform_open_input(int32_t id, Mel_Midi_Port* port)
{
    mel_midi_web__ensure_init();

    if (!mel_midi_web__open(id, port)) return NULL;

    char name[MEL_WEB_MIDI_NAME];
    mel_midi_web__name(id, name, sizeof(name));
    port->name = strdup(name);

    return port;
}

void mel_midi_port_platform_close(Mel_Midi_Port* port)
{
    if (!port) return;
    mel_midi_web__close(port);
}
