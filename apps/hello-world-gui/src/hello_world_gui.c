#include <stdio.h>
#include <string.h>

#include <gui.control/button.h>
#include <gui.control/canvas.h>
#include <gui.control/checkbox.h>
#include <gui.control/edit.h>
#include <gui.control/label.h>
#include <gui.control/slider.h>
#include <gui.control/window.h>
#include <gui.platform/gui.platform.h>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

#if defined(ANDROID) || defined(__ANDROID__)
#include <jni.h>
#include <gui.platform.android/gui.platform.android.h>
#endif

#define DEMO_CLASS_COUNTER_BUTTON      S8("demo.counter_button")
#define DEMO_CLASS_OPEN_DETAILS_BUTTON S8("demo.open_details_button")

typedef struct Main_State {
    Mel_Gui_Handle window;
    Mel_Gui_Handle status;
    Mel_Gui_Handle key_label;
    Mel_Gui_Handle canvas;
    Mel_Gui_Handle edit;
    Mel_Gui_Handle button;
    Mel_Gui_Handle checkbox;
    Mel_Gui_Handle slider_label;
    i32 clicks;
    i32 slider;
    bool checked;
    char edit_text[128];
    char focused_name[32];
    i32 pointer_x, pointer_y;
    bool pointer_down;
    i32 canvas_w, canvas_h;
    i32 last_key;
} Main_State;

typedef struct Details_State {
    Mel_Gui_Handle window;
    Mel_Gui_Handle status;
    i32 taps;
    char focused_name[32];
} Details_State;

static Mel_Atom    g_counter_atom;
static Mel_Atom    g_open_details_atom;
static bool        g_classes_registered;
static Main_State  g_main;
static Details_State g_details;

static void update_main_status(void)
{
    char text[320];
    snprintf(
        text,
        sizeof(text),
        "C saw: clicks=%d, checked=%s, slider=%d, edit=\"%s\", focus=%s",
        g_main.clicks,
        g_main.checked ? "yes" : "no",
        g_main.slider,
        g_main.edit_text,
        g_main.focused_name[0] ? g_main.focused_name : "(none)");
    mel_gui_set_text(g_main.status, str8_from_cstr(text));
}

static void update_details_status(void)
{
    char text[192];
    snprintf(text, sizeof(text),
        "Details Activity has its own C state: taps=%d, focus=%s",
        g_details.taps,
        g_details.focused_name[0] ? g_details.focused_name : "(none)");
    mel_gui_set_text(g_details.status, str8_from_cstr(text));
}

static const char* main_name_for_id(u32 id)
{
    switch (id) {
        case 3:  return "open-details";
        case 10: return "edit";
        case 11: return "counter-button";
        case 12: return "checkbox";
        case 14: return "slider";
        default: return "?";
    }
}

static const char* details_name_for_id(u32 id)
{
    return id == 21 ? "details-button" : "?";
}

static Mel_Gui_Result main_focus_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_FOCUS_GAINED) {
        snprintf(g_main.focused_name, sizeof(g_main.focused_name), "%s", main_name_for_id(mel_gui_id(h)));
        update_main_status();
    } else if (msg == MEL_GUI_MSG_FOCUS_LOST) {
        if (mel_gui_handle_is_none(mel_gui_focus())) {
            g_main.focused_name[0] = 0;
            update_main_status();
        }
    }
    return mel_gui_call_super(h, msg, w, l);
}

#ifdef __APPLE__
static void canvas_draw_macos(CGContextRef ctx, i32 w, i32 h, const Main_State* s)
{
    CGContextSetRGBFillColor(ctx, 0.15, 0.20, 0.27, 1.0);
    CGContextFillRect(ctx, CGRectMake(0, 0, w, h));
    if (s->pointer_down) {
        CGContextSetRGBFillColor(ctx, 1.0, 0.75, 0.4, 1.0);
        CGContextFillEllipseInRect(ctx, CGRectMake(s->pointer_x - 10, s->pointer_y - 10, 20, 20));
    }
}
#endif

#if defined(ANDROID) || defined(__ANDROID__)
static jclass    mel__h_canvas_class;
static jclass    mel__h_paint_class;
static jmethodID mel__h_paint_ctor;
static jmethodID mel__h_paint_set_color;
static jmethodID mel__h_canvas_draw_rect;
static jmethodID mel__h_canvas_draw_circle;

static void canvas_draw_android(JNIEnv* env, jobject canvas, i32 w, i32 h, const Main_State* s)
{
    if (mel__h_canvas_class == NULL) {
        jclass cc = (*env)->FindClass(env, "android/graphics/Canvas");
        mel__h_canvas_class = (*env)->NewGlobalRef(env, cc);
        (*env)->DeleteLocalRef(env, cc);
        jclass pc = (*env)->FindClass(env, "android/graphics/Paint");
        mel__h_paint_class = (*env)->NewGlobalRef(env, pc);
        (*env)->DeleteLocalRef(env, pc);
        mel__h_paint_ctor         = (*env)->GetMethodID(env, mel__h_paint_class,  "<init>",     "()V");
        mel__h_paint_set_color    = (*env)->GetMethodID(env, mel__h_paint_class,  "setColor",   "(I)V");
        mel__h_canvas_draw_rect   = (*env)->GetMethodID(env, mel__h_canvas_class, "drawRect",   "(FFFFLandroid/graphics/Paint;)V");
        mel__h_canvas_draw_circle = (*env)->GetMethodID(env, mel__h_canvas_class, "drawCircle", "(FFFLandroid/graphics/Paint;)V");
    }
    jobject paint = (*env)->NewObject(env, mel__h_paint_class, mel__h_paint_ctor);
    (*env)->CallVoidMethod(env, paint, mel__h_paint_set_color, (jint)0xFF263341);
    (*env)->CallVoidMethod(env, canvas, mel__h_canvas_draw_rect, 0.0f, 0.0f, (jfloat)w, (jfloat)h, paint);
    if (s->pointer_down) {
        (*env)->CallVoidMethod(env, paint, mel__h_paint_set_color, (jint)0xFFFFBE60);
        (*env)->CallVoidMethod(env, canvas, mel__h_canvas_draw_circle,
            (jfloat)s->pointer_x, (jfloat)s->pointer_y, 24.0f, paint);
    }
    (*env)->DeleteLocalRef(env, paint);
}
#endif

static void update_main_key_label(void)
{
    char edit_via_gettext[64];
    edit_via_gettext[0] = 0;
    if (!mel_gui_handle_is_none(g_main.edit)) {
        mel_gui_get_text(g_main.edit, edit_via_gettext, sizeof(edit_via_gettext));
    }
    char text[192];
    if (g_main.last_key) {
        snprintf(text, sizeof(text), "last key: %d, edit (GET_TEXT)=\"%s\"", g_main.last_key, edit_via_gettext);
    } else {
        snprintf(text, sizeof(text), "edit (GET_TEXT)=\"%s\"", edit_via_gettext);
    }
    mel_gui_set_text(g_main.key_label, str8_from_cstr(text));
}

static Mel_Gui_Result canvas_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wp, Mel_Gui_LParam lp)
{
    switch (msg) {
        case MEL_GUI_MSG_PAINT: {
#ifdef __APPLE__
            CGContextRef ctx = (CGContextRef)(uintptr_t)lp;
            canvas_draw_macos(ctx, g_main.canvas_w, g_main.canvas_h, &g_main);
#elif defined(ANDROID) || defined(__ANDROID__)
            JNIEnv* env = mel_gui_android_env();
            jobject canvas = (jobject)(intptr_t)lp;
            canvas_draw_android(env, canvas, g_main.canvas_w, g_main.canvas_h, &g_main);
#else
            (void)lp;
#endif
            return MEL_GUI_OK;
        }
        case MEL_GUI_MSG_POINTER_DOWN:
            g_main.pointer_down = true;
            g_main.pointer_x = mel_gui_unpack_x(lp);
            g_main.pointer_y = mel_gui_unpack_y(lp);
            mel_gui_invalidate(h);
            return MEL_GUI_OK;
        case MEL_GUI_MSG_POINTER_MOVE:
            g_main.pointer_x = mel_gui_unpack_x(lp);
            g_main.pointer_y = mel_gui_unpack_y(lp);
            if (g_main.pointer_down) mel_gui_invalidate(h);
            return MEL_GUI_OK;
        case MEL_GUI_MSG_POINTER_UP:
            g_main.pointer_down = false;
            mel_gui_invalidate(h);
            return MEL_GUI_OK;
        case MEL_GUI_MSG_KEY_DOWN:
            g_main.last_key = (i32)wp;
            update_main_key_label();
            return MEL_GUI_OK;
    }
    return mel_gui_call_super(h, msg, wp, lp);
}

static Mel_Gui_Result details_focus_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_FOCUS_GAINED) {
        snprintf(g_details.focused_name, sizeof(g_details.focused_name), "%s", details_name_for_id(mel_gui_id(h)));
        update_details_status();
    } else if (msg == MEL_GUI_MSG_FOCUS_LOST) {
        if (mel_gui_handle_is_none(mel_gui_focus())) {
            g_details.focused_name[0] = 0;
            update_details_status();
        }
    }
    return mel_gui_call_super(h, msg, w, l);
}

static Mel_Gui_Result main_window_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_COMMAND) {
        update_main_status();
        return MEL_GUI_OK;
    }
    return mel_gui_call_super(h, msg, w, l);
}

static Mel_Gui_Result details_window_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_COMMAND) {
        update_details_status();
        return MEL_GUI_OK;
    }
    return mel_gui_call_super(h, msg, w, l);
}

static Mel_Gui_Result counter_button_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_CLICK) {
        g_main.clicks += 1;
        mel_gui_set_text(h, S8("Handled by class proc"));
        update_main_status();
    }
    return mel_gui_call_super(h, msg, w, l);
}

static Mel_Gui_Result open_details_button_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_CLICK) {
        mel_gui_app_start_activity(S8("details"));
        return MEL_GUI_OK;
    }
    return mel_gui_call_super(h, msg, w, l);
}

static Mel_Gui_Result details_button_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_CLICK) {
        g_details.taps += 1;
        update_details_status();
        return MEL_GUI_OK;
    }
    return mel_gui_call_super(h, msg, w, l);
}

static Mel_Gui_Result checkbox_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_VALUE_CHANGED) {
        g_main.checked = w != 0;
        update_main_status();
    }
    return mel_gui_call_super(h, msg, w, l);
}

static Mel_Gui_Result slider_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_VALUE_CHANGED) {
        g_main.slider = (i32)w;
        char text[32];
        snprintf(text, sizeof(text), "%d", g_main.slider);
        mel_gui_set_text(g_main.slider_label, str8_from_cstr(text));
        update_main_status();
    }
    return mel_gui_call_super(h, msg, w, l);
}

static Mel_Gui_Result edit_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l)
{
    if (msg == MEL_GUI_MSG_TEXT_CHANGED) {
        str8 text = (str8){ .data = (u8*)(intptr_t)l, .len = (size)(usize)w };
        size n = str8_to_buf(text, g_main.edit_text, (size)sizeof(g_main.edit_text) - 1);
        g_main.edit_text[n] = 0;
        update_main_status();
        update_main_key_label();
    }
    return mel_gui_call_super(h, msg, w, l);
}

static void register_demo_classes(void)
{
    if (g_classes_registered) return;

    g_counter_atom = mel_gui_register_class(&(Mel_Gui_Class_Desc){
        .name       = DEMO_CLASS_COUNTER_BUTTON,
        .base_class = mel_gui_button_atom(),
        .proc       = counter_button_proc,
    });
    g_open_details_atom = mel_gui_register_class(&(Mel_Gui_Class_Desc){
        .name       = DEMO_CLASS_OPEN_DETAILS_BUTTON,
        .base_class = mel_gui_button_atom(),
        .proc       = open_details_button_proc,
    });

    g_classes_registered = true;
}

static Mel_Gui_Handle create_demo_child(Mel_Gui_Handle parent, Mel_Atom class_atom, str8 text, u32 id, i32 x, i32 y, i32 w, i32 h)
{
    return mel_gui_create(&(Mel_Gui_Create_Desc){
        .class_atom = class_atom,
        .text   = text,
        .style  = MEL_GUI_WS_CHILD | MEL_GUI_WS_VISIBLE | MEL_GUI_WS_TABSTOP,
        .x = x, .y = y, .w = w, .h = h,
        .parent = parent,
        .id     = id,
    });
}

static void build_main_activity(void)
{
    memset(&g_main, 0, sizeof(g_main));
    strcpy(g_main.edit_text, "native ui");
    g_main.slider = 65;

    g_main.canvas_w = 340;
    g_main.canvas_h = 140;

    g_main.window = mel_gui_create_window(S8("Hello World Main"), 0, 0, main_window_proc, &g_main);
    mel_gui_create_label(g_main.window, S8("Main Activity"), 1, 24, 20, 360, 48);
    mel_gui_create_label(g_main.window, S8("This screen is selected and built from C."), 2, 24, 76, 360, 46);
    Mel_Gui_Handle open_details = create_demo_child(g_main.window, g_open_details_atom, S8("Open C-defined Details Activity"), 3, 24, 126, 310, 52);
    g_main.edit     = mel_gui_create_edit(g_main.window, S8("native ui"), 10, 24, 198, 320, 54, edit_proc, NULL);
    g_main.button   = create_demo_child(g_main.window, g_counter_atom, S8("Tap custom class"), 11, 24, 272, 210, 52);
    g_main.checkbox = mel_gui_create_checkbox(g_main.window, S8("Own checkbox proc"), 12, 24, 344, 340, 52, checkbox_proc, NULL);
    mel_gui_create_label(g_main.window, S8("Slider value"), 13, 24, 416, 120, 42);
    Mel_Gui_Handle slider = mel_gui_create_slider(g_main.window, 14, 138, 416, 170, 42, slider_proc, NULL);
    g_main.slider_label = mel_gui_create_label(g_main.window, S8("65"), 15, 318, 416, 56, 42);
    g_main.status   = mel_gui_create_label(g_main.window, S8(""), 16, 24, 488, 360, 80);
    g_main.key_label = mel_gui_create_label(g_main.window, S8("last key: (none)"), 17, 24, 576, 360, 30);
    g_main.canvas   = mel_gui_create_canvas(g_main.window, 18, 24, 614, g_main.canvas_w, g_main.canvas_h, canvas_proc, &g_main);

    mel_gui_attach_proc(open_details,    main_focus_proc);
    mel_gui_attach_proc(g_main.edit,     main_focus_proc);
    mel_gui_attach_proc(g_main.button,   main_focus_proc);
    mel_gui_attach_proc(g_main.checkbox, main_focus_proc);
    mel_gui_attach_proc(slider,          main_focus_proc);

    update_main_status();
    update_main_key_label();
}

static void build_details_activity(void)
{
    memset(&g_details, 0, sizeof(g_details));

    g_details.window = mel_gui_create_window(S8("Hello World Details"), 0, 0, details_window_proc, &g_details);
    mel_gui_create_label(g_details.window, S8("Details Activity"), 1, 24, 20, 360, 48);
    mel_gui_create_label(g_details.window, S8("Different surface, same melody build code."), 2, 24, 76, 360, 46);
    Mel_Gui_Handle details_button = mel_gui_create_button(g_details.window, S8("Tap details-local proc"), 21, 24, 144, 250, 52, details_button_proc, NULL);
    g_details.status = mel_gui_create_label(g_details.window, S8(""), 22, 24, 218, 360, 80);

    mel_gui_attach_proc(details_button, details_focus_proc);

    update_details_status();
}

void mel_gui_app_build_activity(str8 activity_name)
{
    register_demo_classes();

    if (str8_equals(activity_name, S8("details"))) {
        build_details_activity();
        return;
    }
    build_main_activity();
}
