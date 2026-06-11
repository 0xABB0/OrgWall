#include <gamepad/provider.h>
#include <gamepad/protocol.h>
#include <gamepad/ios/ios.h>

#include "../../src/joystick_backend.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#import <GameController/GameController.h>
#import <Foundation/Foundation.h>

typedef struct
{
    void*          controller_ref;
    u64            stable_id;
    char           name[128];
    Mel_Array(i16) axes;
    Mel_Array(u8)  buttons;
} Ios_Pad;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Array(Ios_Pad) pads;
} Ios_Backend;

static Ios_Backend g_backend;

static GCController* controller_of(Ios_Pad* pad) { return (__bridge GCController*)pad->controller_ref; }

static u64 stable_id_for(GCController* c) { return 0x600d600d00000000ull | (u64)(uintptr_t)(__bridge void*)c; }

static Ios_Pad* pad_for(u64 stable_id)
{
    for (usize i = 0; i < g_backend.pads.count; i++)
        if (g_backend.pads.items[i].stable_id == stable_id)
            return &g_backend.pads.items[i];
    return NULL;
}

static void set_button(Ios_Pad* pad, u32 idx, bool pressed)
{
    if (idx < pad->buttons.count)
        pad->buttons.items[idx] = pressed ? 1 : 0;
}

static void pads_clear(void)
{
    for (usize i = 0; i < g_backend.pads.count; i++)
    {
        mel_array_free(&g_backend.pads.items[i].axes);
        mel_array_free(&g_backend.pads.items[i].buttons);
    }
    mel_array_clear(&g_backend.pads);
}

static u32 ios_enumerate(void* user, Mel_Joystick_Raw* out, u32 cap)
{
    (void)user;
    pads_clear();
    u32 n = 0;
    for (GCController* c in GCController.controllers)
    {
        if (n >= cap)
            break;
        Ios_Pad pad = { 0 };
        pad.controller_ref = (__bridge void*)c;
        pad.stable_id = stable_id_for(c);
        mel_array_init(&pad.axes, g_backend.alloc);
        mel_array_init(&pad.buttons, g_backend.alloc);

        Mel_Joystick_Descriptor d = { 0 };
        const char* vn = c.vendorName.UTF8String;
        if (vn)
        {
            strncpy(pad.name, vn, sizeof pad.name - 1);
            d.name = str8_from_cstr(pad.name);
        }
        d.guid = mel_guid_from_hidapi(3, 0, 0, 0, pad.name, 0, 0);
        d.player_index = (i32)c.playerIndex;
        if (c.extendedGamepad)
        {
            d.axis_count = 6;
            d.button_count = MEL_GAMEPAD_BUTTON_COUNT;
            d.features.player_led = true;
            d.features.accel = (c.motion != nil);
            if (@available(iOS 14.0, *))
            {
                d.features.gyro = (c.motion != nil && c.motion.hasRotationRate);
                d.features.dual_motor_rumble = false;
                d.features.trigger_rumble = false;
            }
        }
        if (@available(iOS 14.0, *))
        {
            if (c.battery)
            {
                d.power.has_battery = true;
                d.power.battery_level = c.battery.batteryLevel;
                d.power.charging = (c.battery.batteryState == GCDeviceBatteryStateCharging);
                d.power.wireless = true;
            }
        }

        for (u32 a = 0; a < d.axis_count; a++)
            mel_array_push(&pad.axes, (i16)0);
        for (u32 bn = 0; bn < d.button_count; bn++)
            mel_array_push(&pad.buttons, (u8)0);

        out[n].stable_id = pad.stable_id;
        out[n].desc = d;
        mel_array_push(&g_backend.pads, pad);
        n++;
    }
    return n;
}

static i16 norm(float v) { return (i16)(v * 32767.0f); }

static bool ios_poll(void* user, u64 stable_id, Mel_Joystick_State* out)
{
    (void)user;
    Ios_Pad* pad = pad_for(stable_id);
    if (!pad)
        return false;
    GCController* c = controller_of(pad);
    if (!c)
        return false;
    GCExtendedGamepad* g = c.extendedGamepad;
    *out = (Mel_Joystick_State){ 0 };
    if (!g)
        return true;

    if (pad->axes.count >= 6)
    {
        pad->axes.items[0] = norm(g.leftThumbstick.xAxis.value);
        pad->axes.items[1] = norm(-g.leftThumbstick.yAxis.value);
        pad->axes.items[2] = norm(g.rightThumbstick.xAxis.value);
        pad->axes.items[3] = norm(-g.rightThumbstick.yAxis.value);
        pad->axes.items[4] = norm(g.leftTrigger.value);
        pad->axes.items[5] = norm(g.rightTrigger.value);
    }

    set_button(pad, MEL_GAMEPAD_BUTTON_SOUTH, g.buttonA.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_EAST, g.buttonB.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_WEST, g.buttonX.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_NORTH, g.buttonY.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_LEFT_SHOULDER, g.leftShoulder.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_RIGHT_SHOULDER, g.rightShoulder.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_DPAD_UP, g.dpad.up.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_DPAD_DOWN, g.dpad.down.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_DPAD_LEFT, g.dpad.left.pressed);
    set_button(pad, MEL_GAMEPAD_BUTTON_DPAD_RIGHT, g.dpad.right.pressed);
    if (g.buttonMenu)
        set_button(pad, MEL_GAMEPAD_BUTTON_START, g.buttonMenu.pressed);
    if (g.buttonOptions)
        set_button(pad, MEL_GAMEPAD_BUTTON_BACK, g.buttonOptions.pressed);

    out->axes = pad->axes.items;
    out->axis_count = (u32)pad->axes.count;
    out->buttons = pad->buttons.items;
    out->button_count = (u32)pad->buttons.count;
    return true;
}

static Mel_Joystick_Status ios_rumble(void* user, u64 stable_id, Mel_Joystick_Rumble rumble)
{
    (void)user;
    (void)rumble;
    Ios_Pad* pad = pad_for(stable_id);
    if (!pad)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    GCController* c = controller_of(pad);
    if (!c)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_UNSUPPORTED;
}

static Mel_Joystick_Status ios_set_player_index(void* user, u64 stable_id, i32 player_index)
{
    (void)user;
    Ios_Pad* pad = pad_for(stable_id);
    if (!pad)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    GCController* c = controller_of(pad);
    if (!c)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    c.playerIndex = (GCControllerPlayerIndex)player_index;
    return MEL_JOYSTICK_OK;
}

static void* ios_native(void* user, u64 stable_id)
{
    (void)user;
    Ios_Pad* pad = pad_for(stable_id);
    return pad ? pad->controller_ref : NULL;
}

void mel_joystick__register_host_providers(const Mel_Alloc* alloc)
{
    g_backend.alloc = alloc ? alloc : mel_alloc_heap();
    mel_array_init(&g_backend.pads, g_backend.alloc);
    Mel_Joystick_Provider_Desc desc = {
        .name = "gamecontroller",
        .enumerate = ios_enumerate,
        .poll = ios_poll,
        .rumble = ios_rumble,
        .set_player_index = ios_set_player_index,
        .native = ios_native,
    };
    mel_joystick_provider_register(&desc);
}

GCController* mel_joystick_ios_controller(Mel_Joystick j)
{
    u32 prov;
    u64 stable_id;
    if (!mel_joystick__lookup(j, &prov, &stable_id))
        return nil;
    Ios_Pad* pad = pad_for(stable_id);
    return pad ? controller_of(pad) : nil;
}
