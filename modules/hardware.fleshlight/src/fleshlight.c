#include <hardware.fleshlight/fleshlight_priv.h>

#include <core/platform.h>

#include <stdlib.h>
#include <string.h>

// ── Public API ─────────────────────────────────────────────────────────────

i32 mel_fleshlight_enumerate(Mel_Fleshlight_Info* out_infos, i32 max_count)
{
    return mel_fleshlight_platform_enumerate(out_infos, max_count);
}

Mel_Fleshlight_Device* mel_fleshlight_open(i32 id)
{
    Mel_Fleshlight_Device* device = calloc(1, sizeof(*device));
    if (!device) return NULL;

    device->id = id;

    if (!mel_fleshlight_platform_open(id, device))
    {
        free(device);
        return NULL;
    }

    device->is_connected = true;
    return device;
}

void mel_fleshlight_close(Mel_Fleshlight_Device* device)
{
    if (!device) return;
    mel_fleshlight_platform_close(device);
    free(device->name);
    free(device);
}

bool mel_fleshlight_is_connected(const Mel_Fleshlight_Device* device)
{
    return device ? device->is_connected : false;
}

bool mel_fleshlight_set_position(Mel_Fleshlight_Device* device, f32 position)
{
    if (!device || !device->is_connected || !device->supports_position) return false;
    if (position < MEL_FLESHLIGHT_POSITION_MIN) position = MEL_FLESHLIGHT_POSITION_MIN;
    if (position > MEL_FLESHLIGHT_POSITION_MAX) position = MEL_FLESHLIGHT_POSITION_MAX;
    return mel_fleshlight_platform_set_position(device, position);
}

bool mel_fleshlight_set_speed(Mel_Fleshlight_Device* device, f32 speed)
{
    if (!device || !device->is_connected || !device->supports_speed) return false;
    if (speed < MEL_FLESHLIGHT_SPEED_MIN) speed = MEL_FLESHLIGHT_SPEED_MIN;
    if (speed > MEL_FLESHLIGHT_SPEED_MAX) speed = MEL_FLESHLIGHT_SPEED_MAX;
    return mel_fleshlight_platform_set_speed(device, speed);
}

bool mel_fleshlight_stop(Mel_Fleshlight_Device* device)
{
    if (!device || !device->is_connected) return false;
    return mel_fleshlight_platform_stop(device);
}

i32 mel_fleshlight_id(const Mel_Fleshlight_Device* device)
{
    return device ? device->id : -1;
}

const char* mel_fleshlight_name(const Mel_Fleshlight_Device* device)
{
    return device ? device->name : NULL;
}
