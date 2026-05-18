#pragma once

#include <core/compiler.h>
#include <core/types.h>

#include "fleshlight.fwd.h"

// ── Constants ──────────────────────────────────────────────────────────────

#define MEL_FLESHLIGHT_POSITION_MIN  0.0f
#define MEL_FLESHLIGHT_POSITION_MAX  1.0f
#define MEL_FLESHLIGHT_SPEED_MIN     0.0f
#define MEL_FLESHLIGHT_SPEED_MAX     1.0f

// ── Device info ────────────────────────────────────────────────────────────

typedef enum
{
    MEL_FLESHLIGHT_KIND_UNKNOWN,
    MEL_FLESHLIGHT_KIND_LAUNCH,
    MEL_FLESHLIGHT_KIND_MAX2,
    MEL_FLESHLIGHT_KIND_KEON,
} Mel_Fleshlight_Kind;

struct Mel_Fleshlight_Info
{
    i32                 id;
    const char*         name;
    Mel_Fleshlight_Kind kind;
    bool                supports_position;
    bool                supports_speed;
};

// ── API ────────────────────────────────────────────────────────────────────

i32                  mel_fleshlight_enumerate(Mel_Fleshlight_Info* out_infos, i32 max_count);
Mel_Fleshlight_Device* mel_fleshlight_open(i32 id);
void                 mel_fleshlight_close(Mel_Fleshlight_Device* device);
bool                 mel_fleshlight_is_connected(const Mel_Fleshlight_Device* device);

bool mel_fleshlight_set_position(Mel_Fleshlight_Device* device, f32 position);
bool mel_fleshlight_set_speed(Mel_Fleshlight_Device* device, f32 speed);
bool mel_fleshlight_stop(Mel_Fleshlight_Device* device);

i32         mel_fleshlight_id(const Mel_Fleshlight_Device* device);
const char* mel_fleshlight_name(const Mel_Fleshlight_Device* device);
