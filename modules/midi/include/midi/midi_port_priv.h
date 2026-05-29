#pragma once

#include <midi/midi_port.h>

#include <stdint.h>
#include <stdbool.h>

struct Mel_Midi_Port
{
    int32_t id;
    char*   name;
    bool    is_open;

    void* platform_handle;

    Mel_Midi_Chunk* ring;
    int32_t ring_capacity;
    volatile int32_t ring_read;
    volatile int32_t ring_write;

    void* platform_lock;
    void* platform_signal;
};
