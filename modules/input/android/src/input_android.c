#include <input/provider.h>
#include <input/android/android.h>

#include <platform/android/jni.h>
#include <log/log.h>

#include "../../src/input_internal.h"

#define MEL_ANDROID_KEYBOARD_ID 0x616E646B6264ULL
#define MEL_ANDROID_TOUCH_ID    0x616E64746368ULL
#define MEL_ANDROID_MOUSE_ID    0x616E646D7365ULL
#define MEL_ANDROID_PEN_ID      0x616E64706E00ULL

enum
{
    ANDROID_ACTION_DOWN = 0,
    ANDROID_ACTION_UP = 1,
    ANDROID_ACTION_MOVE = 2,
    ANDROID_ACTION_CANCEL = 3,
    ANDROID_ACTION_POINTER_DOWN = 5,
    ANDROID_ACTION_POINTER_UP = 6,
    ANDROID_ACTION_HOVER_MOVE = 7,
};

enum
{
    ANDROID_SOURCE_TOUCHSCREEN = 0x00001002,
    ANDROID_SOURCE_MOUSE = 0x00002002,
    ANDROID_SOURCE_STYLUS = 0x00004002,
};

enum
{
    ANDROID_META_SHIFT_ON = 0x1,
    ANDROID_META_ALT_ON = 0x2,
    ANDROID_META_CTRL_ON = 0x1000,
    ANDROID_META_CAPS_LOCK_ON = 0x100000,
    ANDROID_META_NUM_LOCK_ON = 0x200000,
};

static struct
{
    bool text_active;
    bool osk_visible;
} g_and;

static Mel_Scancode android_scancode_from_keycode(i32 kc)
{
    if (kc >= 29 && kc <= 54)
        return (Mel_Scancode)(MEL_SCANCODE_A + (kc - 29));
    if (kc >= 7 && kc <= 16)
        return (Mel_Scancode)(MEL_SCANCODE_1 + ((kc - 7 + 9) % 10));
    switch (kc)
    {
    case 66:
        return MEL_SCANCODE_RETURN;
    case 67:
        return MEL_SCANCODE_BACKSPACE;
    case 61:
        return MEL_SCANCODE_TAB;
    case 62:
        return MEL_SCANCODE_SPACE;
    case 111:
        return MEL_SCANCODE_ESCAPE;
    case 59:
        return MEL_SCANCODE_LSHIFT;
    case 60:
        return MEL_SCANCODE_RSHIFT;
    case 113:
        return MEL_SCANCODE_LCTRL;
    case 114:
        return MEL_SCANCODE_RCTRL;
    case 57:
        return MEL_SCANCODE_LALT;
    case 58:
        return MEL_SCANCODE_RALT;
    case 21:
        return MEL_SCANCODE_LEFT;
    case 22:
        return MEL_SCANCODE_RIGHT;
    case 19:
        return MEL_SCANCODE_UP;
    case 20:
        return MEL_SCANCODE_DOWN;
    default:
        return MEL_SCANCODE_UNKNOWN;
    }
}

static u32 android_modifiers(i32 meta)
{
    u32 m = 0;
    if (meta & ANDROID_META_SHIFT_ON)
        m |= MEL_INPUT_MOD_LSHIFT;
    if (meta & ANDROID_META_CTRL_ON)
        m |= MEL_INPUT_MOD_LCTRL;
    if (meta & ANDROID_META_ALT_ON)
        m |= MEL_INPUT_MOD_LALT;
    if (meta & ANDROID_META_CAPS_LOCK_ON)
        m |= MEL_INPUT_MOD_CAPS;
    if (meta & ANDROID_META_NUM_LOCK_ON)
        m |= MEL_INPUT_MOD_NUM;
    return m;
}

static u32 android_enumerate(void* user, Mel_Input_Raw* out, u32 cap)
{
    (void)user;
    if (cap < 2)
        return 0;
    u32 n = 0;
    out[n++] = (Mel_Input_Raw){ .stable_id = MEL_ANDROID_TOUCH_ID, .desc = { .name = S8("Touchscreen"), .caps = MEL_INPUT_CAP_TOUCH | MEL_INPUT_CAP_PRESSURE, .touch_point_max = 10, .touch_direct = true, .pressure_max = 1.0f } };
    out[n++] = (Mel_Input_Raw){ .stable_id = MEL_ANDROID_KEYBOARD_ID, .desc = { .name = S8("Keyboard"), .caps = MEL_INPUT_CAP_KEYBOARD | MEL_INPUT_CAP_TEXT | MEL_INPUT_CAP_IME, .key_count = MEL_SCANCODE_COUNT } };
    if (cap >= 4)
    {
        out[n++] = (Mel_Input_Raw){ .stable_id = MEL_ANDROID_PEN_ID,
                                    .desc = { .name = S8("Stylus"), .caps = MEL_INPUT_CAP_PEN | MEL_INPUT_CAP_PRESSURE | MEL_INPUT_CAP_TILT | MEL_INPUT_CAP_ERASER, .pen_button_count = 2, .pressure_max = 1.0f } };
        out[n++] = (Mel_Input_Raw){ .stable_id = MEL_ANDROID_MOUSE_ID, .desc = { .name = S8("Mouse"), .caps = MEL_INPUT_CAP_MOUSE | MEL_INPUT_CAP_RELATIVE, .button_count = 3 } };
    }
    return n;
}

static jobject android_context(JNIEnv* env)
{
    jclass at = (*env)->FindClass(env, "android/app/ActivityThread");
    if (!at)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jmethodID cur = (*env)->GetStaticMethodID(env, at, "currentApplication", "()Landroid/app/Application;");
    if (!cur)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jobject app = (*env)->CallStaticObjectMethod(env, at, cur);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return app;
}

static jobject android_imm(JNIEnv* env, jobject ctx)
{
    jclass    ctx_cls = (*env)->GetObjectClass(env, ctx);
    jmethodID get = (*env)->GetMethodID(env, ctx_cls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!get)
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jstring name = (*env)->NewStringUTF(env, "input_method");
    jobject imm = (*env)->CallObjectMethod(env, ctx, get, name);
    (*env)->DeleteLocalRef(env, name);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    return imm;
}

static Mel_Input_Status android_text_start(void* user, const Mel_Input_Text_Opt* opt)
{
    (void)user;
    (void)opt;
    g_and.text_active = true;
    return MEL_INPUT_OK;
}

static void android_text_stop(void* user)
{
    (void)user;
    g_and.text_active = false;
}

static Mel_Input_Status android_osk_show_impl(void* user)
{
    (void)user;
    JNIEnv* env = mel_platform_android_env();
    if (!env)
        return MEL_INPUT_ERROR | MEL_INPUT_NO_PROVIDER;
    jobject ctx = android_context(env);
    if (!ctx)
        return MEL_INPUT_ERROR | MEL_INPUT_NO_PROVIDER;
    jobject imm = android_imm(env, ctx);
    if (!imm)
        return MEL_INPUT_ERROR | MEL_INPUT_NO_PROVIDER;
    jclass    imm_cls = (*env)->GetObjectClass(env, imm);
    jmethodID toggle = (*env)->GetMethodID(env, imm_cls, "toggleSoftInput", "(II)V");
    if (toggle)
        (*env)->CallVoidMethod(env, imm, toggle, 2, 0);
    (*env)->ExceptionClear(env);
    g_and.osk_visible = true;
    return MEL_INPUT_OK;
}

static Mel_Input_Status android_osk_hide_impl(void* user)
{
    (void)user;
    g_and.osk_visible = false;
    return MEL_INPUT_OK;
}

static Mel_Input_Provider_Desc g_desc;

void mel_input__register_host_providers(void)
{
    g_desc = (Mel_Input_Provider_Desc){
        .name = "android-jni",
        .enumerate = android_enumerate,
        .text_start = android_text_start,
        .text_stop = android_text_stop,
        .osk_show = android_osk_show_impl,
        .osk_hide = android_osk_hide_impl,
    };
    mel_input_provider_register(&g_desc);
}

void mel_input_android_handle_key(i32 action, i32 keycode, i32 meta_state, i32 unicode, bool repeat)
{
    Mel_Input_Sink* sink = mel_input__sink();
    if (sink == NULL)
        return;
    Mel_Input_Key_Event ke = {
        .scancode = android_scancode_from_keycode(keycode),
        .keycode = (Mel_Keycode)unicode,
        .modifiers = android_modifiers(meta_state),
        .down = action == ANDROID_ACTION_DOWN,
        .repeat = repeat,
    };
    mel_input_sink_key(sink, MEL_ANDROID_KEYBOARD_ID, &ke);
}

void mel_input_android_handle_motion(i32 source, i32 action, i32 pointer_id, f32 x, f32 y, f32 pressure)
{
    Mel_Input_Sink* sink = mel_input__sink();
    if (sink == NULL)
        return;
    i32 masked = action & 0xFF;
    if ((source & ANDROID_SOURCE_STYLUS) == ANDROID_SOURCE_STYLUS)
    {
        u32                 phase = masked == ANDROID_ACTION_DOWN ? MEL_INPUT_PEN_DOWN : (masked == ANDROID_ACTION_UP ? MEL_INPUT_PEN_UP : (masked == ANDROID_ACTION_HOVER_MOVE ? MEL_INPUT_PEN_MOVE : MEL_INPUT_PEN_MOVE));
        Mel_Input_Pen_Event pe = { .phase = phase, .x = x, .y = y, .pressure = pressure, .in_proximity = true };
        mel_input_sink_pen(sink, MEL_ANDROID_PEN_ID, &pe);
        return;
    }
    if ((source & ANDROID_SOURCE_MOUSE) == ANDROID_SOURCE_MOUSE)
    {
        Mel_Input_Mouse_Event me = { .x = x, .y = y };
        mel_input_sink_mouse(sink, MEL_ANDROID_MOUSE_ID, &me);
        return;
    }
    u32 phase;
    switch (masked)
    {
    case ANDROID_ACTION_DOWN:
    case ANDROID_ACTION_POINTER_DOWN:
        phase = MEL_INPUT_TOUCH_DOWN;
        break;
    case ANDROID_ACTION_UP:
    case ANDROID_ACTION_POINTER_UP:
        phase = MEL_INPUT_TOUCH_UP;
        break;
    case ANDROID_ACTION_CANCEL:
        phase = MEL_INPUT_TOUCH_CANCEL;
        break;
    default:
        phase = MEL_INPUT_TOUCH_MOVE;
        break;
    }
    Mel_Input_Touch_Event te = { .finger_id = (u64)pointer_id, .phase = phase, .x = x, .y = y, .pressure = pressure, .direct = true };
    mel_input_sink_touch(sink, MEL_ANDROID_TOUCH_ID, &te);
}
