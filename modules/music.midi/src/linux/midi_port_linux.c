#include <music.midi/midi_port.h>

#include <core/platform.h>

#if !MEL_PLATFORM_LINUX
    #error "This file should only be compiled on Linux"
#endif

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <time.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ── Forward from midi_port.c ───────────────────────────────────────────────

extern void mel_midi_port_push_chunk(Mel_Midi_Port* port, const Mel_Midi_Chunk* chunk);

// ── Per-port platform data ────────────────────────────────────────────────

typedef struct Mel_Midi_Port_Linux Mel_Midi_Port_Linux;
struct Mel_Midi_Port_Linux
{
    snd_seq_t*       seq;
    int              port_id;
    int              src_client;
    int              src_port;
    pthread_t        thread;
    volatile bool    running;
    volatile bool    closed;
};

// ── Reader thread ──────────────────────────────────────────────────────────

static void* mel__midi_reader_thread(void* arg)
{
    Mel_Midi_Port* port = (Mel_Midi_Port*)arg;
    if (!port) return NULL;

    Mel_Midi_Port_Linux* pl = (Mel_Midi_Port_Linux*)port->platform_handle;
    if (!pl) return NULL;

    struct timespec ts;
    ts.tv_sec  = 0;
    ts.tv_nsec = 1000000; // 1ms poll interval

    while (pl->running)
    {
        snd_seq_event_t* ev = NULL;
        int res = snd_seq_event_input(pl->seq, &ev);

        if (res > 0 && ev)
        {
            Mel_Midi_Chunk chunk = {0};

            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            chunk.timestamp_us = (uint64_t)now.tv_sec * 1000000ULL
                               + (uint64_t)now.tv_nsec / 1000ULL;

            switch (ev->type)
            {
                case SND_SEQ_EVENT_NOTEON:
                    chunk.data[0] = MEL_MIDI_STATUS_NOTE_ON | (ev->data.note.channel & 0x0F);
                    chunk.data[1] = ev->data.note.note & 0x7F;
                    chunk.data[2] = ev->data.note.velocity & 0x7F;
                    chunk.length  = 3;
                    break;

                case SND_SEQ_EVENT_NOTEOFF:
                    chunk.data[0] = MEL_MIDI_STATUS_NOTE_OFF | (ev->data.note.channel & 0x0F);
                    chunk.data[1] = ev->data.note.note & 0x7F;
                    chunk.data[2] = ev->data.note.velocity & 0x7F;
                    chunk.length  = 3;
                    break;

                case SND_SEQ_EVENT_KEYPRESS:
                    chunk.data[0] = MEL_MIDI_STATUS_KEY_PRESSURE | (ev->data.note.channel & 0x0F);
                    chunk.data[1] = ev->data.note.note & 0x7F;
                    chunk.data[2] = ev->data.note.velocity & 0x7F;
                    chunk.length  = 3;
                    break;

                case SND_SEQ_EVENT_CONTROLLER:
                    chunk.data[0] = MEL_MIDI_STATUS_CONTROL_CHANGE | (ev->data.control.channel & 0x0F);
                    chunk.data[1] = ev->data.control.param & 0x7F;
                    chunk.data[2] = ev->data.control.value & 0x7F;
                    chunk.length  = 3;
                    break;

                case SND_SEQ_EVENT_PGMCHANGE:
                    chunk.data[0] = MEL_MIDI_STATUS_PROGRAM_CHANGE | (ev->data.control.channel & 0x0F);
                    chunk.data[1] = ev->data.control.value & 0x7F;
                    chunk.length  = 2;
                    break;

                case SND_SEQ_EVENT_CHANPRESS:
                    chunk.data[0] = MEL_MIDI_STATUS_CHANNEL_PRESSURE | (ev->data.control.channel & 0x0F);
                    chunk.data[1] = ev->data.control.value & 0x7F;
                    chunk.length  = 2;
                    break;

                case SND_SEQ_EVENT_PITCHBEND:
                {
                    chunk.data[0] = MEL_MIDI_STATUS_PITCH_BEND | (ev->data.control.channel & 0x0F);
                    int32_t bend = ev->data.control.value;
                    chunk.data[1] = (uint8_t)(bend & 0x7F);
                    chunk.data[2] = (uint8_t)((bend >> 7) & 0x7F);
                    chunk.length  = 3;
                    break;
                }

                case SND_SEQ_EVENT_START:
                    chunk.data[0] = 0xFA;
                    chunk.length  = 1;
                    break;

                case SND_SEQ_EVENT_CONTINUE:
                    chunk.data[0] = 0xFB;
                    chunk.length  = 1;
                    break;

                case SND_SEQ_EVENT_STOP:
                    chunk.data[0] = 0xFC;
                    chunk.length  = 1;
                    break;

                case SND_SEQ_EVENT_CLOCK:
                    chunk.data[0] = 0xF8;
                    chunk.length  = 1;
                    break;

                default:
                    continue;
            }

            mel_midi_port_push_chunk(port, &chunk);
        }

        nanosleep(&ts, NULL);
    }

    return NULL;
}

// ── Enumerate ──────────────────────────────────────────────────────────────

int32_t mel_midi_port_platform_enumerate(Mel_Midi_Port_Info* out_infos, int32_t max_count)
{
    snd_seq_t* seq = NULL;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
        return 0;

    snd_seq_client_info_t* cinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_client_info_set_client(cinfo, -1);

    int32_t n = 0;
    while (snd_seq_query_next_client(seq, cinfo) >= 0 && n < max_count)
    {
        int client = snd_seq_client_info_get_client(cinfo);

        snd_seq_port_info_t* pinfo;
        snd_seq_port_info_alloca(&pinfo);
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);

        while (snd_seq_query_next_port(seq, pinfo) >= 0 && n < max_count)
        {
            unsigned int caps = snd_seq_port_info_get_capability(pinfo);
            if (!(caps & SND_SEQ_PORT_CAP_READ) || !(caps & SND_SEQ_PORT_CAP_SUBS_READ))
                continue;

            if (out_infos)
            {
                out_infos[n].id       = n;
                out_infos[n].name     = snd_seq_port_info_get_name(pinfo);
                out_infos[n].is_input = true;
            }
            n++;
        }
    }

    snd_seq_close(seq);
    return n;
}

// ── Open ───────────────────────────────────────────────────────────────────

Mel_Midi_Port* mel_midi_port_platform_open_input(int32_t id, Mel_Midi_Port* port)
{
    Mel_Midi_Port_Linux* pl = calloc(1, sizeof(*pl));
    if (!pl) return NULL;

    if (snd_seq_open(&pl->seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0)
    {
        free(pl);
        return NULL;
    }

    snd_seq_set_client_name(pl->seq, "Melody MIDI");

    pl->port_id = snd_seq_create_simple_port(
        pl->seq, "Melody Input",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC);

    if (pl->port_id < 0)
    {
        snd_seq_close(pl->seq);
        free(pl);
        return NULL;
    }

    // Find and connect to the nth readable port
    {
        snd_seq_client_info_t* cinfo;
        snd_seq_client_info_alloca(&cinfo);
        snd_seq_client_info_set_client(cinfo, -1);

        int32_t target = 0;
        bool found = false;

        while (snd_seq_query_next_client(pl->seq, cinfo) >= 0)
        {
            int client = snd_seq_client_info_get_client(cinfo);

            snd_seq_port_info_t* pinfo;
            snd_seq_port_info_alloca(&pinfo);
            snd_seq_port_info_set_client(pinfo, client);
            snd_seq_port_info_set_port(pinfo, -1);

            while (snd_seq_query_next_port(pl->seq, pinfo) >= 0)
            {
                unsigned int caps = snd_seq_port_info_get_capability(pinfo);
                if (!(caps & SND_SEQ_PORT_CAP_READ) || !(caps & SND_SEQ_PORT_CAP_SUBS_READ))
                    continue;

                if (target == id)
                {
                    pl->src_client = client;
                    pl->src_port   = snd_seq_port_info_get_port(pinfo);
                    port->name     = strdup(snd_seq_port_info_get_name(pinfo));
                    found = true;
                    break;
                }
                target++;
            }
            if (found) break;
        }

        if (!found)
        {
            snd_seq_delete_simple_port(pl->seq, pl->port_id);
            snd_seq_close(pl->seq);
            free(pl);
            return NULL;
        }
    }

    if (snd_seq_connect_from(pl->seq, pl->port_id, pl->src_client, pl->src_port) < 0)
    {
        snd_seq_delete_simple_port(pl->seq, pl->port_id);
        snd_seq_close(pl->seq);
        free(pl);
        return NULL;
    }

    // Start reader thread
    pl->running = true;
    if (pthread_create(&pl->thread, NULL, mel__midi_reader_thread, port) != 0)
    {
        snd_seq_disconnect_from(pl->seq, pl->port_id, pl->src_client, pl->src_port);
        snd_seq_delete_simple_port(pl->seq, pl->port_id);
        snd_seq_close(pl->seq);
        free(pl);
        return NULL;
    }

    port->platform_handle = pl;
    return port;
}

// ── Close ──────────────────────────────────────────────────────────────────

void mel_midi_port_platform_close(Mel_Midi_Port* port)
{
    if (!port) return;

    Mel_Midi_Port_Linux* pl = (Mel_Midi_Port_Linux*)port->platform_handle;
    if (!pl) return;

    pl->closed  = true;
    pl->running = false;

    pthread_join(pl->thread, NULL);

    snd_seq_disconnect_from(pl->seq, pl->port_id, pl->src_client, pl->src_port);
    snd_seq_delete_simple_port(pl->seq, pl->port_id);
    snd_seq_close(pl->seq);
    free(pl);
    port->platform_handle = NULL;
}
