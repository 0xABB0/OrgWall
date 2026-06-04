#include <gamepad/provider.h>
#include <gamepad/protocol.h>
#include <gamepad/win32/win32.h>

#include "../joystick_backend.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>

#include <string.h>
#include <stdio.h>

typedef struct
{
    u32            xinput_index;
    u64            stable_id;
    char           name[32];
    Mel_Array(i16) axes;
    Mel_Array(u8)  buttons;
    Mel_Array(u8)  hats;
} Win_Pad;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Array(Win_Pad) pads;
} Win_Backend;

static Win_Backend g_backend;

static Win_Pad* pad_for(u64 stable_id)
{
    for (usize i = 0; i < g_backend.pads.count; i++)
        if (g_backend.pads.items[i].stable_id == stable_id)
            return &g_backend.pads.items[i];
    return NULL;
}

static void pads_clear(void)
{
    for (usize i = 0; i < g_backend.pads.count; i++)
    {
        mel_array_free(&g_backend.pads.items[i].axes);
        mel_array_free(&g_backend.pads.items[i].buttons);
        mel_array_free(&g_backend.pads.items[i].hats);
    }
    mel_array_clear(&g_backend.pads);
}

static u32 win_enumerate(void* user, Mel_Joystick_Raw* out, u32 cap)
{
    (void)user;
    pads_clear();
    u32 n = 0;
    for (u32 i = 0; i < XUSER_MAX_COUNT && n < cap; i++)
    {
        XINPUT_STATE state;
        if (XInputGetState(i, &state) != ERROR_SUCCESS)
            continue;
        Win_Pad pad = { 0 };
        pad.xinput_index = i;
        pad.stable_id = 0x78696e00ull | (u64)i;
        snprintf(pad.name, sizeof pad.name, "XInput Controller %u", i + 1);
        mel_array_init(&pad.axes, g_backend.alloc);
        mel_array_init(&pad.buttons, g_backend.alloc);
        mel_array_init(&pad.hats, g_backend.alloc);
        for (u32 a = 0; a < 6; a++)
            mel_array_push(&pad.axes, (i16)0);
        for (u32 bn = 0; bn < MEL_GAMEPAD_BUTTON_COUNT; bn++)
            mel_array_push(&pad.buttons, (u8)0);
        mel_array_push(&pad.hats, (u8)MEL_JOYSTICK_HAT_CENTERED);

        Mel_Joystick_Descriptor desc;
        memset(&desc, 0, sizeof desc);
        desc.name = str8_from_cstr(pad.name);
        desc.guid = mel_guid_from_hidapi(0, 0x045E, 0x028E, 0, "XInput Controller", 0, 0);
        desc.axis_count = 6;
        desc.button_count = MEL_GAMEPAD_BUTTON_COUNT;
        desc.hat_count = 1;
        desc.player_index = (i32)i;
        desc.features.dual_motor_rumble = true;

        XINPUT_BATTERY_INFORMATION batt;
        if (XInputGetBatteryInformation(i, BATTERY_DEVTYPE_GAMEPAD, &batt) == ERROR_SUCCESS)
        {
            if (batt.BatteryType == BATTERY_TYPE_WIRED)
            {
                desc.power.wireless = false;
            }
            else if (batt.BatteryType != BATTERY_TYPE_DISCONNECTED && batt.BatteryType != BATTERY_TYPE_UNKNOWN)
            {
                desc.power.wireless = true;
                desc.power.has_battery = true;
                desc.power.battery_level = (f32)batt.BatteryLevel / (f32)BATTERY_LEVEL_FULL;
            }
        }

        out[n].stable_id = pad.stable_id;
        out[n].desc = desc;
        mel_array_push(&g_backend.pads, pad);
        n++;
    }
    return n;
}

static bool win_poll(void* user, u64 stable_id, Mel_Joystick_State* out)
{
    (void)user;
    Win_Pad* pad = pad_for(stable_id);
    if (!pad)
        return false;
    XINPUT_STATE state;
    if (XInputGetState(pad->xinput_index, &state) != ERROR_SUCCESS)
        return false;

    const XINPUT_GAMEPAD* gp = &state.Gamepad;
    pad->axes.items[0] = gp->sThumbLX;
    pad->axes.items[1] = (i16)(-((i32)gp->sThumbLY + 1));
    pad->axes.items[2] = gp->sThumbRX;
    pad->axes.items[3] = (i16)(-((i32)gp->sThumbRY + 1));
    pad->axes.items[4] = (i16)((i32)gp->bLeftTrigger * 32767 / 255);
    pad->axes.items[5] = (i16)((i32)gp->bRightTrigger * 32767 / 255);

    WORD b = gp->wButtons;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_SOUTH] = (b & XINPUT_GAMEPAD_A) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_EAST] = (b & XINPUT_GAMEPAD_B) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_WEST] = (b & XINPUT_GAMEPAD_X) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_NORTH] = (b & XINPUT_GAMEPAD_Y) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_LEFT_SHOULDER] = (b & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_BACK] = (b & XINPUT_GAMEPAD_BACK) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_START] = (b & XINPUT_GAMEPAD_START) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_LEFT_STICK] = (b & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_RIGHT_STICK] = (b & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_DPAD_UP] = (b & XINPUT_GAMEPAD_DPAD_UP) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_DPAD_DOWN] = (b & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_DPAD_LEFT] = (b & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    pad->buttons.items[MEL_GAMEPAD_BUTTON_DPAD_RIGHT] = (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;

    pad->hats.items[0] = (u8)((pad->buttons.items[MEL_GAMEPAD_BUTTON_DPAD_UP] ? MEL_JOYSTICK_HAT_UP : 0) | (pad->buttons.items[MEL_GAMEPAD_BUTTON_DPAD_DOWN] ? MEL_JOYSTICK_HAT_DOWN : 0) | (pad->buttons.items[MEL_GAMEPAD_BUTTON_DPAD_LEFT] ? MEL_JOYSTICK_HAT_LEFT : 0) | (pad->buttons.items[MEL_GAMEPAD_BUTTON_DPAD_RIGHT] ? MEL_JOYSTICK_HAT_RIGHT : 0));

    memset(out, 0, sizeof *out);
    out->axes = pad->axes.items;
    out->axis_count = 6;
    out->buttons = pad->buttons.items;
    out->button_count = MEL_GAMEPAD_BUTTON_COUNT;
    out->hats = pad->hats.items;
    out->hat_count = 1;
    return true;
}

static Mel_Joystick_Status win_rumble(void* user, u64 stable_id, Mel_Joystick_Rumble rumble)
{
    (void)user;
    Win_Pad* pad = pad_for(stable_id);
    if (!pad)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    XINPUT_VIBRATION vib;
    vib.wLeftMotorSpeed = (WORD)(rumble.low_frequency * 65535.0f);
    vib.wRightMotorSpeed = (WORD)(rumble.high_frequency * 65535.0f);
    if (XInputSetState(pad->xinput_index, &vib) != ERROR_SUCCESS)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    Mel_Joystick_Status status = MEL_JOYSTICK_OK;
    if (rumble.left_trigger > 0.0f || rumble.right_trigger > 0.0f)
        status |= MEL_JOYSTICK_WARNED | MEL_JOYSTICK_TRIGGER_RUMBLE_OFF;
    return status;
}

void mel_joystick__register_host_providers(const Mel_Alloc* alloc)
{
    g_backend.alloc = alloc ? alloc : mel_alloc_heap();
    mel_array_init(&g_backend.pads, g_backend.alloc);
    Mel_Joystick_Provider_Desc desc = {
        .name = "xinput",
        .enumerate = win_enumerate,
        .poll = win_poll,
        .rumble = win_rumble,
    };
    mel_joystick_provider_register(&desc);
}

u32 mel_joystick_win32_xinput_index(Mel_Joystick j)
{
    u32 prov;
    u64 stable_id;
    if (!mel_joystick__lookup(j, &prov, &stable_id))
        return 0xFFFFFFFFu;
    Win_Pad* pad = pad_for(stable_id);
    return pad ? pad->xinput_index : 0xFFFFFFFFu;
}

void* mel_joystick_win32_rawinput_handle(Mel_Joystick j)
{
    (void)j;
    return NULL;
}
