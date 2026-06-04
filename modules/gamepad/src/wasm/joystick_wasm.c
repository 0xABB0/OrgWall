#include <gamepad/provider.h>
#include <gamepad/protocol.h>
#include <gamepad/wasm/wasm.h>

#include "../joystick_backend.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <emscripten/html5.h>
#include <emscripten/emscripten.h>

#include <string.h>
#include <stdio.h>

typedef struct
{
    i32            gamepad_index;
    u64            stable_id;
    char           name[96];
    Mel_Array(i16) axes;
    Mel_Array(u8)  buttons;
} Wasm_Pad;

typedef struct
{
    const Mel_Alloc* alloc;
    Mel_Array(Wasm_Pad) pads;
} Wasm_Backend;

static Wasm_Backend g_backend;

static Wasm_Pad* pad_for(u64 stable_id)
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
    }
    mel_array_clear(&g_backend.pads);
}

EM_JS(int, mel_gamepad_js_vibrate, (int index, double weak, double strong, double duration_ms), {
    var pads = navigator.getGamepads ? navigator.getGamepads() : [];
    var gp = pads[index];
    if (!gp || !gp.vibrationActuator)
        return 0;
    gp.vibrationActuator.playEffect("dual-rumble", { duration : duration_ms, strongMagnitude : strong, weakMagnitude : weak });
    return 1;
});

static u32 wasm_enumerate(void* user, Mel_Joystick_Raw* out, u32 cap)
{
    (void)user;
    pads_clear();
    emscripten_sample_gamepad_data();
    int n_pads = emscripten_get_num_gamepads();
    if (n_pads < 0)
        return 0;

    u32 n = 0;
    for (int i = 0; i < n_pads && n < cap; i++)
    {
        EmscriptenGamepadEvent gp;
        if (emscripten_get_gamepad_status(i, &gp) != EMSCRIPTEN_RESULT_SUCCESS || !gp.connected)
            continue;

        Wasm_Pad pad = { 0 };
        pad.gamepad_index = i;
        pad.stable_id = 0x7761736d00ull | (u64)i;
        strncpy(pad.name, gp.id, sizeof pad.name - 1);
        mel_array_init(&pad.axes, g_backend.alloc);
        mel_array_init(&pad.buttons, g_backend.alloc);

        Mel_Joystick_Descriptor desc;
        memset(&desc, 0, sizeof desc);
        desc.name = str8_from_cstr(pad.name);
        desc.guid = mel_guid_from_hidapi(0, 0, 0, 0, gp.mapping[0] ? gp.mapping : pad.name, 0, 0);
        desc.axis_count = (u32)(gp.numAxes < 8 ? gp.numAxes : 8);
        desc.button_count = MEL_GAMEPAD_BUTTON_COUNT;
        desc.player_index = i;
        desc.features.dual_motor_rumble = true;

        for (u32 a = 0; a < 8; a++)
            mel_array_push(&pad.axes, (i16)0);
        for (u32 bn = 0; bn < MEL_GAMEPAD_BUTTON_COUNT; bn++)
            mel_array_push(&pad.buttons, (u8)0);

        out[n].stable_id = pad.stable_id;
        out[n].desc = desc;
        mel_array_push(&g_backend.pads, pad);
        n++;
    }
    return n;
}

static bool wasm_poll(void* user, u64 stable_id, Mel_Joystick_State* out)
{
    (void)user;
    Wasm_Pad* pad = pad_for(stable_id);
    if (!pad)
        return false;
    emscripten_sample_gamepad_data();
    EmscriptenGamepadEvent gp;
    if (emscripten_get_gamepad_status(pad->gamepad_index, &gp) != EMSCRIPTEN_RESULT_SUCCESS || !gp.connected)
        return false;

    int na = gp.numAxes < 8 ? gp.numAxes : 8;
    for (int i = 0; i < na; i++)
        pad->axes.items[i] = (i16)(gp.axis[i] * 32767.0);

    int nb = gp.numButtons;
    static const int standard_to_mel[] = {
        MEL_GAMEPAD_BUTTON_SOUTH, MEL_GAMEPAD_BUTTON_EAST, MEL_GAMEPAD_BUTTON_WEST, MEL_GAMEPAD_BUTTON_NORTH,
        MEL_GAMEPAD_BUTTON_LEFT_SHOULDER, MEL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0, 0,
        MEL_GAMEPAD_BUTTON_BACK, MEL_GAMEPAD_BUTTON_START, MEL_GAMEPAD_BUTTON_LEFT_STICK, MEL_GAMEPAD_BUTTON_RIGHT_STICK,
        MEL_GAMEPAD_BUTTON_DPAD_UP, MEL_GAMEPAD_BUTTON_DPAD_DOWN, MEL_GAMEPAD_BUTTON_DPAD_LEFT, MEL_GAMEPAD_BUTTON_DPAD_RIGHT,
        MEL_GAMEPAD_BUTTON_GUIDE,
    };
    for (int i = 0; i < nb && i < (int)(sizeof standard_to_mel / sizeof standard_to_mel[0]); i++)
    {
        int mel = standard_to_mel[i];
        if (mel != 0)
            pad->buttons.items[mel] = gp.digitalButton[i] ? 1 : 0;
    }
    pad->axes.items[4] = nb > 6 ? (i16)(gp.analogButton[6] * 32767.0) : 0;
    pad->axes.items[5] = nb > 7 ? (i16)(gp.analogButton[7] * 32767.0) : 0;

    memset(out, 0, sizeof *out);
    out->axes = pad->axes.items;
    out->axis_count = 6;
    out->buttons = pad->buttons.items;
    out->button_count = MEL_GAMEPAD_BUTTON_COUNT;
    return true;
}

static Mel_Joystick_Status wasm_rumble(void* user, u64 stable_id, Mel_Joystick_Rumble rumble)
{
    (void)user;
    Wasm_Pad* pad = pad_for(stable_id);
    if (!pad)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
    double ms = rumble.duration_s > 0.0f ? rumble.duration_s * 1000.0 : 200.0;
    if (!mel_gamepad_js_vibrate(pad->gamepad_index, rumble.high_frequency, rumble.low_frequency, ms))
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_UNSUPPORTED;
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
        .name = "gamepad-api",
        .enumerate = wasm_enumerate,
        .poll = wasm_poll,
        .rumble = wasm_rumble,
    };
    mel_joystick_provider_register(&desc);
}

i32 mel_joystick_wasm_gamepad_index(Mel_Joystick j)
{
    u32 prov;
    u64 stable_id;
    if (!mel_joystick__lookup(j, &prov, &stable_id))
        return -1;
    Wasm_Pad* pad = pad_for(stable_id);
    return pad ? pad->gamepad_index : -1;
}
