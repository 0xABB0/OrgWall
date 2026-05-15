#include <music.midi/midi_port.h>

#include <core/platform.h>

#if !MEL_PLATFORM_OSX
    #error "This file should only be compiled on macOS"
#endif

#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>

#include <stdlib.h>
#include <string.h>

// ── Forward from midi_port.c ───────────────────────────────────────────────

extern void mel_midi_port_push_chunk(Mel_Midi_Port* port, const Mel_Midi_Chunk* chunk);

// ── Per-port platform data ────────────────────────────────────────────────

typedef struct Mel_Midi_Port_Mac Mel_Midi_Port_Mac;
struct Mel_Midi_Port_Mac
{
    MIDIClientRef   client;
    MIDIPortRef     input_port;
    MIDIEndpointRef source;
    volatile bool   closed;
};

// ── Callback ───────────────────────────────────────────────────────────────

static void mel__midi_read_proc(const MIDIPacketList* pktlist,
                                 void* readProcRefCon,
                                 void* srcConnRefCon)
{
    (void)srcConnRefCon;
    Mel_Midi_Port* port = (Mel_Midi_Port*)readProcRefCon;
    if (!port) return;

    Mel_Midi_Port_Mac* pm = (Mel_Midi_Port_Mac*)port->platform_handle;
    if (!pm || pm->closed) return;

    uint64_t now = mach_absolute_time();
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    now = now * timebase.numer / timebase.denom / 1000; // nanoseconds → microseconds

    const MIDIPacket* packet = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; i++)
    {
        Mel_Midi_Chunk chunk = {0};
        chunk.timestamp_us = now;

        if (packet->length >= 1)
        {
            chunk.data[0] = packet->data[0];
            chunk.length   = 1;

            if (packet->length >= 2 && !(packet->data[0] >= 0xF8))
            {
                chunk.data[1] = packet->data[1];
                chunk.length   = 2;
            }
            if (packet->length >= 3 && !(packet->data[0] >= 0xF8))
            {
                chunk.data[2] = packet->data[2];
                chunk.length   = 3;
            }

            mel_midi_port_push_chunk(port, &chunk);
        }

        packet = MIDIPacketNext(packet);
    }
}

// ── Enumerate ──────────────────────────────────────────────────────────────

int32_t mel_midi_port_platform_enumerate(Mel_Midi_Port_Info* out_infos, int32_t max_count)
{
    ItemCount count = MIDIGetNumberOfSources();
    int32_t n = 0;

    for (ItemCount i = 0; i < count && n < max_count; i++)
    {
        MIDIEndpointRef src = MIDIGetSource(i);
        if (!src) continue;

        CFStringRef cfname = NULL;
        if (MIDIObjectGetStringProperty(src, kMIDIPropertyName, &cfname) != noErr)
            continue;

        if (out_infos)
        {
            out_infos[n].id       = (int32_t)i;
            out_infos[n].is_input = true;

            static char name_buf[256];
            CFStringGetCString(cfname, name_buf, sizeof(name_buf), kCFStringEncodingUTF8);
            out_infos[n].name = name_buf;
        }

        CFRelease(cfname);
        n++;
    }

    return n;
}

// ── Open ───────────────────────────────────────────────────────────────────

Mel_Midi_Port* mel_midi_port_platform_open_input(int32_t id, Mel_Midi_Port* port)
{
    Mel_Midi_Port_Mac* pm = calloc(1, sizeof(*pm));
    if (!pm) return NULL;

    CFStringRef client_name = CFSTR("Melody MIDI Input");
    if (MIDIClientCreate(client_name, NULL, NULL, &pm->client) != noErr)
    {
        free(pm);
        return NULL;
    }

    if (MIDIInputPortCreate(pm->client, CFSTR("Melody Input"),
                             mel__midi_read_proc, port, &pm->input_port) != noErr)
    {
        MIDIClientDispose(pm->client);
        free(pm);
        return NULL;
    }

    pm->source = MIDIGetSource((ItemCount)id);
    if (!pm->source)
    {
        MIDIPortDispose(pm->input_port);
        MIDIClientDispose(pm->client);
        free(pm);
        return NULL;
    }

    if (MIDIPortConnectSource(pm->input_port, pm->source, NULL) != noErr)
    {
        MIDIPortDispose(pm->input_port);
        MIDIClientDispose(pm->client);
        free(pm);
        return NULL;
    }

    // Grab the device name
    CFStringRef cfname = NULL;
    if (MIDIObjectGetStringProperty(pm->source, kMIDIPropertyName, &cfname) == noErr)
    {
        static char name_buf[256];
        CFStringGetCString(cfname, name_buf, sizeof(name_buf), kCFStringEncodingUTF8);
        port->name = strdup(name_buf);
        CFRelease(cfname);
    }

    port->platform_handle = pm;
    return port;
}

// ── Close ──────────────────────────────────────────────────────────────────

void mel_midi_port_platform_close(Mel_Midi_Port* port)
{
    if (!port) return;

    Mel_Midi_Port_Mac* pm = (Mel_Midi_Port_Mac*)port->platform_handle;
    if (!pm) return;

    pm->closed = true;

    MIDIPortDisconnectSource(pm->input_port, pm->source);
    MIDIPortDispose(pm->input_port);
    MIDIClientDispose(pm->client);
    free(pm);
    port->platform_handle = NULL;
}
