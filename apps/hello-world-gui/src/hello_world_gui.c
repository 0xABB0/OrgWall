#include <stdio.h>
#include <string.h>

#include <gui.control/button.h>
#include <gui.control/checkbox.h>
#include <gui.control/edit.h>
#include <gui.control/label.h>
#include <gui.control/slider.h>
#include <gui.control/window.h>
#include <gui.platform/gui.platform.h>

#define DEMO_CLASS_COUNTER_BUTTON      S8("demo.counter_button")
#define DEMO_CLASS_OPEN_DETAILS_BUTTON S8("demo.open_details_button")

typedef struct Main_State {
    Mel_Gui_Handle window;
    Mel_Gui_Handle status;
    Mel_Gui_Handle edit;
    Mel_Gui_Handle button;
    Mel_Gui_Handle checkbox;
    Mel_Gui_Handle slider_label;
    i32 clicks;
    i32 slider;
    bool checked;
    char edit_text[128];
} Main_State;

typedef struct Details_State {
    Mel_Gui_Handle window;
    Mel_Gui_Handle status;
    i32 taps;
} Details_State;

static Mel_Atom    g_counter_atom;
static Mel_Atom    g_open_details_atom;
static bool        g_classes_registered;
static Main_State  g_main;
static Details_State g_details;

static void update_main_status(void)
{
    char text[256];
    snprintf(
        text,
        sizeof(text),
        "C saw: clicks=%d, checked=%s, slider=%d, edit=\"%s\"",
        g_main.clicks,
        g_main.checked ? "yes" : "no",
        g_main.slider,
        g_main.edit_text);
    mel_gui_set_text(g_main.status, str8_from_cstr(text));
}

static void update_details_status(void)
{
    char text[128];
    snprintf(text, sizeof(text), "Details Activity has its own C state: taps=%d", g_details.taps);
    mel_gui_set_text(g_details.status, str8_from_cstr(text));
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

    g_main.window = mel_gui_create_window(S8("Hello World Main"), 0, 0, main_window_proc, &g_main);
    mel_gui_create_label(g_main.window, S8("Main Activity"), 1, 24, 20, 360, 48);
    mel_gui_create_label(g_main.window, S8("This screen is selected and built from C."), 2, 24, 76, 360, 46);
    create_demo_child(g_main.window, g_open_details_atom, S8("Open C-defined Details Activity"), 3, 24, 126, 310, 52);
    g_main.edit     = mel_gui_create_edit(g_main.window, S8("native ui"), 10, 24, 198, 320, 54, edit_proc, NULL);
    g_main.button   = create_demo_child(g_main.window, g_counter_atom, S8("Tap custom class"), 11, 24, 272, 210, 52);
    g_main.checkbox = mel_gui_create_checkbox(g_main.window, S8("Own checkbox proc"), 12, 24, 344, 340, 52, checkbox_proc, NULL);
    mel_gui_create_label(g_main.window, S8("Slider value"), 13, 24, 416, 120, 42);
    mel_gui_create_slider(g_main.window, 14, 138, 416, 170, 42, slider_proc, NULL);
    g_main.slider_label = mel_gui_create_label(g_main.window, S8("65"), 15, 318, 416, 56, 42);
    g_main.status   = mel_gui_create_label(g_main.window, S8(""), 16, 24, 488, 360, 80);

    update_main_status();
}

static void build_details_activity(void)
{
    memset(&g_details, 0, sizeof(g_details));

    g_details.window = mel_gui_create_window(S8("Hello World Details"), 0, 0, details_window_proc, &g_details);
    mel_gui_create_label(g_details.window, S8("Details Activity"), 1, 24, 20, 360, 48);
    mel_gui_create_label(g_details.window, S8("Different surface, same melody build code."), 2, 24, 76, 360, 46);
    mel_gui_create_button(g_details.window, S8("Tap details-local proc"), 21, 24, 144, 250, 52, details_button_proc, NULL);
    g_details.status = mel_gui_create_label(g_details.window, S8(""), 22, 24, 218, 360, 80);

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
