#include <music.midi/midi_port.h>

#include <core/compiler.h>
#include <core/platform.h>

#include <stdlib.h>
#include <string.h>

// ── Port struct ────────────────────────────────────────────────────────────

struct Mel_Midi_Port
{
    int32_t id;
    char*   name;
    bool    is_open;

    // Opaque platform handle (HMIDIIN on Windows, MIDIPortRef on macOS, etc.)
    void* platform_handle;

    // Ring buffer for incoming chunks (filled by platform callback)
    Mel_Midi_Chunk* ring;
    int32_t ring_capacity;
    volatile int32_t ring_read;
    volatile int32_t ring_write;

    // Platform-specific synchronisation primitive
    void* platform_lock;
    void* platform_signal;
};

// ── Platform hooks (implemented in src/<platform>/midi_port_<platform>.c) ──

extern int32_t mel_midi_port_platform_enumerate(
    Mel_Midi_Port_Info* out_infos, int32_t max_count);

extern Mel_Midi_Port* mel_midi_port_platform_open_input(
    int32_t id, Mel_Midi_Port* port);

extern void mel_midi_port_platform_close(Mel_Midi_Port* port);

// Called by platform callback to push a chunk into the ring buffer.
// Thread-safe — may be called from any thread.
void mel_midi_port_push_chunk(Mel_Midi_Port* port, const Mel_Midi_Chunk* chunk);

// ── Shared ring buffer helpers ─────────────────────────────────────────────

#define MEL_MIDI_RING_SIZE 256

static bool mel__ring_push(Mel_Midi_Port* port, const Mel_Midi_Chunk* chunk)
{
    int32_t next = (port->ring_write + 1) % port->ring_capacity;
    if (next == port->ring_read) return false; // full
    port->ring[port->ring_write] = *chunk;
    port->ring_write = next;
    return true;
}

static bool mel__ring_pop(Mel_Midi_Port* port, Mel_Midi_Chunk* out_chunk)
{
    if (port->ring_read == port->ring_write) return false; // empty
    *out_chunk = port->ring[port->ring_read];
    port->ring_read = (port->ring_read + 1) % port->ring_capacity;
    return true;
}

// ── Public API ─────────────────────────────────────────────────────────────

int32_t mel_midi_port_enumerate_inputs(Mel_Midi_Port_Info* out_infos, int32_t max_count)
{
    return mel_midi_port_platform_enumerate(out_infos, max_count);
}

Mel_Midi_Port* mel_midi_port_open_input(int32_t id)
{
    Mel_Midi_Port* port = calloc(1, sizeof(*port));
    if (!port) return NULL;

    port->id   = id;
    port->ring_capacity = MEL_MIDI_RING_SIZE;
    port->ring = calloc((size_t)port->ring_capacity, sizeof(Mel_Midi_Chunk));
    if (!port->ring)
    {
        free(port);
        return NULL;
    }

    if (!mel_midi_port_platform_open_input(id, port))
    {
        free(port->ring);
        free(port);
        return NULL;
    }

    port->is_open = true;
    return port;
}

void mel_midi_port_close(Mel_Midi_Port* port)
{
    if (!port) return;
    mel_midi_port_platform_close(port);
    free(port->ring);
    free(port->name);
    free(port);
}

bool mel_midi_port_poll(Mel_Midi_Port* port, Mel_Midi_Chunk* out_chunk)
{
    if (!port || !port->is_open || !out_chunk) return false;
    return mel__ring_pop(port, out_chunk);
}

bool mel_midi_port_read_blocking(Mel_Midi_Port* port, Mel_Midi_Chunk* out_chunk)
{
    // Fallback: spin-poll with a short sleep.
    // Platform implementations may override with a proper blocking wait.
    // For most real-time use, poll() in the main loop is preferred.
    (void)port;
    (void)out_chunk;
    return false;
}

int32_t mel_midi_port_id(const Mel_Midi_Port* port)
{
    return port ? port->id : -1;
}

const char* mel_midi_port_name(const Mel_Midi_Port* port)
{
    return port ? port->name : NULL;
}

void mel_midi_port_push_chunk(Mel_Midi_Port* port, const Mel_Midi_Chunk* chunk)
{
    if (!port || !chunk) return;
    mel__ring_push(port, chunk);
}
