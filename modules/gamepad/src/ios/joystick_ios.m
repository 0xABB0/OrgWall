#include <gamepad/provider.h>
#include <gamepad/protocol.h>
#include <gamepad/ios/ios.h>

#include "../joystick_backend.h"

#include <string/str8.h>
#include <log/log.h>

#import <GameController/GameController.h>
#import <Foundation/Foundation.h>

typedef struct
{
    void* controller_ref;
    u64   stable_id;
    char  name[128];
    i16   axes[6];
    u8    buttons[MEL_GAMEPAD_BUTTON_COUNT];
} Ios_Pad;

static Ios_Pad g_pads[16];
static u32     g_pad_count;

static GCController* controller_of(Ios_Pad* pad) { return (__bridge GCController*)pad->controller_ref; }

static u64 stable_id_for(GCController* c) { return (u64)(uintptr_t)(__bridge void*)c; }

static Ios_Pad* pad_for(u64 stable_id)
{
    for (u32 i = 0; i < g_pad_count; i++)
        if (g_pads[i].stable_id == stable_id)
            return &g_pads[i];
    return NULL;
}

static u32 ios_enumerate(void* user, Mel_Joystick_Raw* out, u32 cap)
{
    (void)user;
    g_pad_count = 0;
    u32 n = 0;
    for (GCController* c in GCController.controllers)
    {
        if (g_pad_count >= 16 || n >= cap)
            break;
        Ios_Pad* pad = &g_pads[g_pad_count];
        *pad = (Ios_Pad){ 0 };
        pad->controller_ref = (__bridge void*)c;
        pad->stable_id = stable_id_for(c);

        Mel_Joystick_Descriptor d = { 0 };
        const char* vn = c.vendorName.UTF8String;
        if (vn)
        {
            strncpy(pad->name, vn, sizeof pad->name - 1);
            d.name = str8_from_cstr(pad->name);
        }
        d.guid = mel_guid_from_vidpid(0, 0, 0);
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
                d.features.dual_motor_rumble = (c.haptics != nil);
                d.features.trigger_rumble = (c.haptics != nil);
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

        out[n].stable_id = pad->stable_id;
        out[n].desc = d;
        g_pad_count++;
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

    pad->axes[0] = norm(g.leftThumbstick.xAxis.value);
    pad->axes[1] = norm(-g.leftThumbstick.yAxis.value);
    pad->axes[2] = norm(g.rightThumbstick.xAxis.value);
    pad->axes[3] = norm(-g.rightThumbstick.yAxis.value);
    pad->axes[4] = norm(g.leftTrigger.value);
    pad->axes[5] = norm(g.rightTrigger.value);

    pad->buttons[MEL_GAMEPAD_BUTTON_SOUTH] = g.buttonA.pressed;
    pad->buttons[MEL_GAMEPAD_BUTTON_EAST] = g.buttonB.pressed;
    pad->buttons[MEL_GAMEPAD_BUTTON_WEST] = g.buttonX.pressed;
    pad->buttons[MEL_GAMEPAD_BUTTON_NORTH] = g.buttonY.pressed;
    pad->buttons[MEL_GAMEPAD_BUTTON_LEFT_SHOULDER] = g.leftShoulder.pressed;
    pad->buttons[MEL_GAMEPAD_BUTTON_RIGHT_SHOULDER] = g.rightShoulder.pressed;
    pad->buttons[MEL_GAMEPAD_BUTTON_DPAD_UP] = g.dpad.up.pressed;
    pad->buttons[MEL_GAMEPAD_BUTTON_DPAD_DOWN] = g.dpad.down.pressed;
    pad->buttons[MEL_GAMEPAD_BUTTON_DPAD_LEFT] = g.dpad.left.pressed;
    pad->buttons[MEL_GAMEPAD_BUTTON_DPAD_RIGHT] = g.dpad.right.pressed;
    if (g.buttonMenu)
        pad->buttons[MEL_GAMEPAD_BUTTON_START] = g.buttonMenu.pressed;
    if (g.buttonOptions)
        pad->buttons[MEL_GAMEPAD_BUTTON_BACK] = g.buttonOptions.pressed;

    out->axes = pad->axes;
    out->axis_count = 6;
    out->buttons = pad->buttons;
    out->button_count = MEL_GAMEPAD_BUTTON_COUNT;
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
    if (@available(iOS 14.0, *))
    {
        if (c.haptics == nil)
            return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_UNSUPPORTED;
        return MEL_JOYSTICK_OK;
    }
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

void mel_joystick__register_host_providers(void)
{
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
