#include <midi/midi_port_priv.h>

#include <core/platform.h>

#if !MEL_PLATFORM_WINDOWS
    #error "This file should only be compiled on Windows"
#endif

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

#include <stdlib.h>
#include <string.h>

// ── Forward from midi_port.c ───────────────────────────────────────────────

extern void mel_midi_port_push_chunk(Mel_Midi_Port* port, const Mel_Midi_Chunk* chunk);

// ── Per-port platform data ────────────────────────────────────────────────

typedef struct Mel_Midi_Port_Win32 Mel_Midi_Port_Win32;
struct Mel_Midi_Port_Win32
{
    HMIDIIN         handle;
    CRITICAL_SECTION lock;
    HANDLE          signal;     // event for blocking reads
    volatile bool   closed;
    UINT            device_id;
};

// ── Callback ───────────────────────────────────────────────────────────────

static void CALLBACK mel__midi_in_callback(HMIDIIN hMidiIn, UINT wMsg,
                                            DWORD_PTR dwInstance,
                                            DWORD_PTR dwParam1,
                                            DWORD_PTR dwParam2)
{
    (void)hMidiIn;
    (void)dwParam2;

    Mel_Midi_Port* port = (Mel_Midi_Port*)dwInstance;
    if (!port) return;

    Mel_Midi_Port_Win32* pw = (Mel_Midi_Port_Win32*)port->platform_handle;
    if (!pw || pw->closed) return;

    Mel_Midi_Chunk chunk = {0};

    switch (wMsg)
    {
        case MIM_DATA:
        {
            chunk.data[0] = (uint8_t)(dwParam1 & 0xFF);
            chunk.data[1] = (uint8_t)((dwParam1 >> 8) & 0xFF);
            chunk.data[2] = (uint8_t)((dwParam1 >> 16) & 0xFF);
            chunk.length  = 3;

            // System real-time messages (status >= 0xF8) are single-byte;
            // standard channel messages are 3 bytes.
            if (chunk.data[0] >= 0xF8)
            {
                chunk.length = 1;
            }
            break;
        }

        case MIM_LONGDATA:
        case MIM_LONGERROR:
            // Sysex — not handled yet, skip
            return;

        case MIM_OPEN:
        case MIM_CLOSE:
        case MIM_ERROR:
        case MIM_MOREDATA:
        default:
            return;
    }

    // Timestamp
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    chunk.timestamp_us = (uint64_t)((counter.QuadPart * 1000000ULL) / freq.QuadPart);

    EnterCriticalSection(&pw->lock);
    mel_midi_port_push_chunk(port, &chunk);
    SetEvent(pw->signal);
    LeaveCriticalSection(&pw->lock);
}

// ── Enumerate ──────────────────────────────────────────────────────────────

int32_t mel_midi_port_platform_enumerate(Mel_Midi_Port_Info* out_infos, int32_t max_count)
{
    UINT count = midiInGetNumDevs();
    int32_t n = 0;

    for (UINT i = 0; i < count && n < max_count; i++)
    {
        MIDIINCAPSA caps;
        if (midiInGetDevCapsA(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
            continue;

        if (out_infos)
        {
            out_infos[n].id       = (int32_t)i;
            out_infos[n].name     = caps.szPname;
            out_infos[n].is_input = true;
        }
        n++;
    }

    return n;
}

// ── Open ───────────────────────────────────────────────────────────────────

Mel_Midi_Port* mel_midi_port_platform_open_input(int32_t id, Mel_Midi_Port* port)
{
    Mel_Midi_Port_Win32* pw = calloc(1, sizeof(*pw));
    if (!pw) return NULL;

    pw->device_id = (UINT)id;

    InitializeCriticalSection(&pw->lock);
    pw->signal = CreateEventA(NULL, FALSE, FALSE, NULL);

    MMRESULT res = midiInOpen(&pw->handle, pw->device_id,
                               (DWORD_PTR)mel__midi_in_callback,
                               (DWORD_PTR)port,
                               CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR)
    {
        CloseHandle(pw->signal);
        DeleteCriticalSection(&pw->lock);
        free(pw);
        return NULL;
    }

    midiInStart(pw->handle);

    // Grab the device name
    MIDIINCAPSA caps;
    if (midiInGetDevCapsA(pw->device_id, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
    {
        port->name = _strdup(caps.szPname);
    }

    port->platform_handle = pw;
    return port;
}

// ── Close ──────────────────────────────────────────────────────────────────

void mel_midi_port_platform_close(Mel_Midi_Port* port)
{
    if (!port) return;

    Mel_Midi_Port_Win32* pw = (Mel_Midi_Port_Win32*)port->platform_handle;
    if (!pw) return;

    pw->closed = true;

    midiInStop(pw->handle);
    midiInReset(pw->handle);
    midiInClose(pw->handle);

    CloseHandle(pw->signal);
    DeleteCriticalSection(&pw->lock);
    free(pw);
    port->platform_handle = NULL;
}
