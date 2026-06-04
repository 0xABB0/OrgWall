#include <input/provider.h>
#include <input/wasm/wasm.h>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <string.h>
#include <stdio.h>

#include "../input_internal.h"

#define MEL_WASM_KEYBOARD_ID 0x7761736B6264ULL
#define MEL_WASM_MOUSE_ID    0x7761736D7365ULL
#define MEL_WASM_TOUCH_ID    0x77617374636800ULL
#define MEL_WASM_PEN_ID      0x77617370656E00ULL

static struct
{
    char target[64];
    bool relative;
    bool registered;
    f32  mouse_x, mouse_y;
    u32  buttons;
} g_wasm;

static const char* wasm_target(void) { return g_wasm.target[0] ? g_wasm.target : "#canvas"; }

static Mel_Scancode wasm_scancode_from_code(const char* code)
{
    if (strlen(code) == 4 && strncmp(code, "Key", 3) == 0)
    {
        char c = code[3];
        if (c >= 'A' && c <= 'Z')
            return (Mel_Scancode)(MEL_SCANCODE_A + (c - 'A'));
    }
    if (strlen(code) == 6 && strncmp(code, "Digit", 5) == 0)
    {
        char c = code[5];
        if (c == '0')
            return MEL_SCANCODE_0;
        if (c >= '1' && c <= '9')
            return (Mel_Scancode)(MEL_SCANCODE_1 + (c - '1'));
    }
    if (strcmp(code, "Enter") == 0)
        return MEL_SCANCODE_RETURN;
    if (strcmp(code, "Escape") == 0)
        return MEL_SCANCODE_ESCAPE;
    if (strcmp(code, "Backspace") == 0)
        return MEL_SCANCODE_BACKSPACE;
    if (strcmp(code, "Tab") == 0)
        return MEL_SCANCODE_TAB;
    if (strcmp(code, "Space") == 0)
        return MEL_SCANCODE_SPACE;
    if (strcmp(code, "ShiftLeft") == 0)
        return MEL_SCANCODE_LSHIFT;
    if (strcmp(code, "ShiftRight") == 0)
        return MEL_SCANCODE_RSHIFT;
    if (strcmp(code, "ControlLeft") == 0)
        return MEL_SCANCODE_LCTRL;
    if (strcmp(code, "ControlRight") == 0)
        return MEL_SCANCODE_RCTRL;
    if (strcmp(code, "AltLeft") == 0)
        return MEL_SCANCODE_LALT;
    if (strcmp(code, "AltRight") == 0)
        return MEL_SCANCODE_RALT;
    if (strcmp(code, "MetaLeft") == 0)
        return MEL_SCANCODE_LGUI;
    if (strcmp(code, "MetaRight") == 0)
        return MEL_SCANCODE_RGUI;
    if (strcmp(code, "ArrowLeft") == 0)
        return MEL_SCANCODE_LEFT;
    if (strcmp(code, "ArrowRight") == 0)
        return MEL_SCANCODE_RIGHT;
    if (strcmp(code, "ArrowUp") == 0)
        return MEL_SCANCODE_UP;
    if (strcmp(code, "ArrowDown") == 0)
        return MEL_SCANCODE_DOWN;
    if (strcmp(code, "CapsLock") == 0)
        return MEL_SCANCODE_CAPSLOCK;
    return MEL_SCANCODE_UNKNOWN;
}

static u32 wasm_modifiers(const EmscriptenKeyboardEvent* e)
{
    u32 m = 0;
    if (e->shiftKey)
        m |= MEL_INPUT_MOD_LSHIFT;
    if (e->ctrlKey)
        m |= MEL_INPUT_MOD_LCTRL;
    if (e->altKey)
        m |= MEL_INPUT_MOD_LALT;
    if (e->metaKey)
        m |= MEL_INPUT_MOD_LGUI;
    return m;
}

static EM_BOOL wasm_on_key(int type, const EmscriptenKeyboardEvent* e, void* user)
{
    (void)user;
    Mel_Input_Sink* sink = mel_input__sink();
    if (!sink)
        return EM_FALSE;
    Mel_Input_Key_Event ke = {
        .scancode = wasm_scancode_from_code(e->code),
        .keycode = e->key[0] && !e->key[1] ? (Mel_Keycode)(u8)e->key[0] : 0,
        .modifiers = wasm_modifiers(e),
        .down = type == EMSCRIPTEN_EVENT_KEYDOWN,
        .repeat = e->repeat,
    };
    mel_input_sink_key(sink, MEL_WASM_KEYBOARD_ID, &ke);
    return EM_TRUE;
}

static u32 wasm_button_mask(unsigned short button)
{
    switch (button)
    {
    case 0:
        return MEL_INPUT_MOUSE_BUTTON_LEFT;
    case 1:
        return MEL_INPUT_MOUSE_BUTTON_MIDDLE;
    case 2:
        return MEL_INPUT_MOUSE_BUTTON_RIGHT;
    case 3:
        return MEL_INPUT_MOUSE_BUTTON_X1;
    case 4:
        return MEL_INPUT_MOUSE_BUTTON_X2;
    default:
        return 0;
    }
}

static EM_BOOL wasm_on_mouse(int type, const EmscriptenMouseEvent* e, void* user)
{
    (void)user;
    Mel_Input_Sink* sink = mel_input__sink();
    if (!sink)
        return EM_FALSE;
    g_wasm.mouse_x = (f32)e->targetX;
    g_wasm.mouse_y = (f32)e->targetY;
    Mel_Input_Mouse_Event me = { .x = g_wasm.mouse_x, .y = g_wasm.mouse_y, .global_x = (f32)e->screenX, .global_y = (f32)e->screenY, .dx = (f32)e->movementX, .dy = (f32)e->movementY };
    if (type == EMSCRIPTEN_EVENT_MOUSEDOWN || type == EMSCRIPTEN_EVENT_MOUSEUP)
    {
        u32  mask = wasm_button_mask(e->button);
        bool down = type == EMSCRIPTEN_EVENT_MOUSEDOWN;
        if (down)
            g_wasm.buttons |= mask;
        else
            g_wasm.buttons &= ~mask;
        me.button_changed = mask;
        me.button_down = down;
    }
    me.buttons = g_wasm.buttons;
    mel_input_sink_mouse(sink, MEL_WASM_MOUSE_ID, &me);
    return EM_TRUE;
}

static EM_BOOL wasm_on_wheel(int type, const EmscriptenWheelEvent* e, void* user)
{
    (void)type;
    (void)user;
    Mel_Input_Sink* sink = mel_input__sink();
    if (!sink)
        return EM_FALSE;
    Mel_Input_Mouse_Event me = { .x = g_wasm.mouse_x, .y = g_wasm.mouse_y, .wheel_x = (f32)-e->deltaX, .wheel_y = (f32)-e->deltaY, .buttons = g_wasm.buttons };
    mel_input_sink_mouse(sink, MEL_WASM_MOUSE_ID, &me);
    return EM_TRUE;
}

static EM_BOOL wasm_on_touch(int type, const EmscriptenTouchEvent* e, void* user)
{
    (void)user;
    Mel_Input_Sink* sink = mel_input__sink();
    if (!sink)
        return EM_FALSE;
    u32 phase;
    switch (type)
    {
    case EMSCRIPTEN_EVENT_TOUCHSTART:
        phase = MEL_INPUT_TOUCH_DOWN;
        break;
    case EMSCRIPTEN_EVENT_TOUCHEND:
        phase = MEL_INPUT_TOUCH_UP;
        break;
    case EMSCRIPTEN_EVENT_TOUCHCANCEL:
        phase = MEL_INPUT_TOUCH_CANCEL;
        break;
    default:
        phase = MEL_INPUT_TOUCH_MOVE;
        break;
    }
    for (int i = 0; i < e->numTouches; i++)
    {
        const EmscriptenTouchPoint* t = &e->touches[i];
        if (!t->isChanged)
            continue;
        Mel_Input_Touch_Event te = { .finger_id = (u64)t->identifier, .phase = phase, .x = (f32)t->targetX, .y = (f32)t->targetY, .pressure = 1.0f, .direct = true };
        mel_input_sink_touch(sink, MEL_WASM_TOUCH_ID, &te);
    }
    return EM_TRUE;
}

static void wasm_register(void)
{
    if (g_wasm.registered)
        return;
    const char* tgt = wasm_target();
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, wasm_on_key);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, wasm_on_key);
    emscripten_set_mousemove_callback(tgt, NULL, EM_TRUE, wasm_on_mouse);
    emscripten_set_mousedown_callback(tgt, NULL, EM_TRUE, wasm_on_mouse);
    emscripten_set_mouseup_callback(tgt, NULL, EM_TRUE, wasm_on_mouse);
    emscripten_set_wheel_callback(tgt, NULL, EM_TRUE, wasm_on_wheel);
    emscripten_set_touchstart_callback(tgt, NULL, EM_TRUE, wasm_on_touch);
    emscripten_set_touchmove_callback(tgt, NULL, EM_TRUE, wasm_on_touch);
    emscripten_set_touchend_callback(tgt, NULL, EM_TRUE, wasm_on_touch);
    emscripten_set_touchcancel_callback(tgt, NULL, EM_TRUE, wasm_on_touch);
    g_wasm.registered = true;
}

static u32 wasm_enumerate(void* user, Mel_Input_Raw* out, u32 cap)
{
    (void)user;
    if (cap < 2)
        return 0;
    wasm_register();
    u32 n = 0;
    out[n++] = (Mel_Input_Raw){ .stable_id = MEL_WASM_KEYBOARD_ID, .desc = { .name = S8("Keyboard"), .caps = MEL_INPUT_CAP_KEYBOARD | MEL_INPUT_CAP_TEXT | MEL_INPUT_CAP_IME, .key_count = MEL_SCANCODE_COUNT } };
    out[n++] = (Mel_Input_Raw){ .stable_id = MEL_WASM_MOUSE_ID, .desc = { .name = S8("Pointer"), .caps = MEL_INPUT_CAP_MOUSE | MEL_INPUT_CAP_RELATIVE | MEL_INPUT_CAP_CAPTURE, .button_count = 5 } };
    int has_touch = emscripten_run_script_int("('ontouchstart' in window) ? 1 : 0");
    if (has_touch && cap >= 3)
        out[n++] = (Mel_Input_Raw){ .stable_id = MEL_WASM_TOUCH_ID, .desc = { .name = S8("Touchscreen"), .caps = MEL_INPUT_CAP_TOUCH | MEL_INPUT_CAP_PRESSURE, .touch_point_max = 10, .touch_direct = true, .pressure_max = 1.0f } };
    return n;
}

static Mel_Mouse_State wasm_mouse_state(void* user, u64 sid)
{
    (void)user;
    (void)sid;
    return (Mel_Mouse_State){ .x = g_wasm.mouse_x, .y = g_wasm.mouse_y, .buttons = g_wasm.buttons, .relative = g_wasm.relative };
}

static Mel_Input_Status wasm_set_relative(void* user, u64 sid, bool enable)
{
    (void)user;
    (void)sid;
    g_wasm.relative = enable;
    if (enable)
        emscripten_request_pointerlock(wasm_target(), EM_TRUE);
    else
        emscripten_exit_pointerlock();
    return MEL_INPUT_OK;
}

static Mel_Input_Status wasm_capture(void* user, bool enable)
{
    (void)user;
    if (enable)
        emscripten_request_pointerlock(wasm_target(), EM_TRUE);
    else
        emscripten_exit_pointerlock();
    return MEL_INPUT_OK;
}

static Mel_Input_Status wasm_cursor_show(void* user, bool visible)
{
    (void)user;
    char script[160];
    snprintf(script, sizeof script, "(function(){var el=document.querySelector('%s');if(el)el.style.cursor='%s';})()", wasm_target(), visible ? "" : "none");
    emscripten_run_script(script);
    return MEL_INPUT_OK;
}

static Mel_Input_Status wasm_text_start(void* user, const Mel_Input_Text_Opt* opt)
{
    (void)user;
    (void)opt;
    return MEL_INPUT_OK;
}

static void wasm_text_stop(void* user) { (void)user; }

static Mel_Input_Provider_Desc g_desc;

void mel_input__register_host_providers(void)
{
    g_desc = (Mel_Input_Provider_Desc){
        .name = "wasm-dom",
        .enumerate = wasm_enumerate,
        .mouse_state = wasm_mouse_state,
        .mouse_set_relative = wasm_set_relative,
        .mouse_capture = wasm_capture,
        .cursor_show = wasm_cursor_show,
        .text_start = wasm_text_start,
        .text_stop = wasm_text_stop,
    };
    mel_input_provider_register(&g_desc);
}

void mel_input_wasm_set_target(const char* css_selector)
{
    if (css_selector == NULL)
    {
        g_wasm.target[0] = 0;
        return;
    }
    strncpy(g_wasm.target, css_selector, sizeof g_wasm.target - 1);
    g_wasm.target[sizeof g_wasm.target - 1] = 0;
}
