#pragma once

#include <hardware.fleshlight/fleshlight.h>

#include <stdatomic.h>

#define MEL_FLESHLIGHT_RING_SIZE 256

struct Mel_Fleshlight_Device
{
    i32                 id;
    char*               name;
    Mel_Fleshlight_Kind kind;
    bool                supports_position;
    bool                supports_speed;
    bool                is_connected;
    void*               platform_data;
};

// ── Platform hooks (implemented in src/<platform>/fleshlight_<platform>.c) ──

i32 mel_fleshlight_platform_enumerate(Mel_Fleshlight_Info* out_infos, i32 max_count);
Mel_Fleshlight_Device* mel_fleshlight_platform_open(i32 id, Mel_Fleshlight_Device* device);
void mel_fleshlight_platform_close(Mel_Fleshlight_Device* device);
bool mel_fleshlight_platform_set_position(Mel_Fleshlight_Device* device, f32 position);
bool mel_fleshlight_platform_set_speed(Mel_Fleshlight_Device* device, f32 speed);
bool mel_fleshlight_platform_stop(Mel_Fleshlight_Device* device);
