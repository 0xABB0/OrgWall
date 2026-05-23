#include <stdio.h>
#include <string.h>

#include <core/platform.h>
#include <app/app.h>
#include <gui/gui.h>

#include "tapcounter.h"


typedef struct {
    Mel_Gui_Handle frame;
    Mel_Gui_Handle edit;
    Mel_Gui_Handle slider_label;
    Mel_Gui_Handle status;
    Mel_Gui_Handle key_label;
    Mel_Gui_Handle canvas;
    i32  slider;
    bool checked;
    bool focused;
    char edit_text[128];
    i32  pointer_x, pointer_y;
    bool pointer_down;
    i32  canvas_w, canvas_h;
    i32  last_key;
} Main_State;

static Main_State    g_main;

static void update_main_status(void)
{
    char text[320];
    snprintf(text, sizeof text,
        "C saw: checked=%s, slider=%d, edit=\"%s\", focus=%s",
        g_main.checked ? "yes" : "no",
        g_main.slider,
        g_main.edit_text,
        g_main.focused ? "yes" : "no");
    mel_gui_set_text(g_main.status, str8_from_cstr(text));
}

static void update_main_key_label(void)
{
    char edit_buf[64];
    edit_buf[0] = 0;
    if (!mel_gui_handle_is_none(g_main.edit)) {
        mel_gui_get_text(g_main.edit, edit_buf, sizeof edit_buf);
    }
    char text[160];
    if (g_main.last_key) {
        snprintf(text, sizeof text, "last key: %d, edit(GET_TEXT)=\"%s\"",
                 g_main.last_key, edit_buf);
    } else {
        snprintf(text, sizeof text, "edit(GET_TEXT)=\"%s\"", edit_buf);
    }
    mel_gui_set_text(g_main.key_label, str8_from_cstr(text));
}

static void main_focus_in(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    g_main.focused = true;
    update_main_status();
}

static void main_focus_out(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (mel_gui_handle_is_none(mel_gui_focused())) {
        g_main.focused = false;
        update_main_status();
    }
}

static void open_details_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    mel_app_present(S8("details"));
}

static void replace_screen_clicked(Mel_Gui_Handle h, void* user)
{
    (void)user;
    mel_app_replace(h, S8("replaced"));
}

static void main_checkbox_toggled(Mel_Gui_Handle h, bool checked, void* user)
{
    (void)h;
    (void)user;
    g_main.checked = checked;
    update_main_status();
}

static void main_slider_changed(Mel_Gui_Handle h, i32 value, void* user)
{
    (void)h;
    (void)user;
    g_main.slider = value;
    char buf[32];
    snprintf(buf, sizeof buf, "%d", value);
    mel_gui_set_text(g_main.slider_label, str8_from_cstr(buf));
    update_main_status();
}

static void main_edit_changed(Mel_Gui_Handle h, str8 text, void* user)
{
    (void)h;
    (void)user;
    size n = str8_to_buf(text, g_main.edit_text, (size)sizeof(g_main.edit_text) - 1);
    g_main.edit_text[n] = 0;
    update_main_status();
    update_main_key_label();
}

static void main_edit_key_down(Mel_Gui_Handle h, Mel_Key key, void* user)
{
    (void)h;
    (void)user;
    g_main.last_key = (i32)key;
    update_main_key_label();
}

static void canvas_paint(Mel_Gui_Handle h, Mel_Painter* p, i32 w, i32 height, void* user)
{
    (void)h;
    (void)user;
    g_main.canvas_w = w;
    g_main.canvas_h = height;

    mel_painter_clear(p, mel_rgb(38, 51, 65));
    mel_painter_stroke_rect(p, mel_rect(1, 1, (f32)w - 2, (f32)height - 2),
                            mel_rgb(90, 110, 130), 1.5f);
    mel_painter_draw_line(p, mel_vec2(0, 28), mel_vec2((f32)w, 28),
                          mel_rgb(90, 110, 130), 1.0f);
    mel_painter_draw_text(p, S8("canvas: drag to paint"), mel_vec2(10, 7),
                          mel_rgb(150, 170, 190), 13.0f);

    if (g_main.pointer_down) {
        mel_painter_fill_ellipse(p,
            mel_rect((f32)g_main.pointer_x - 18, (f32)g_main.pointer_y - 18, 36, 36),
            mel_rgb(255, 190, 96));
    }
}

static void canvas_pointer_down(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)user;
    g_main.pointer_down = true;
    g_main.pointer_x = x;
    g_main.pointer_y = y;
    mel_gui_invalidate(h);
}

static void canvas_pointer_move(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)user;
    g_main.pointer_x = x;
    g_main.pointer_y = y;
    if (g_main.pointer_down) mel_gui_invalidate(h);
}

static void canvas_pointer_up(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)x;
    (void)y;
    (void)user;
    g_main.pointer_down = false;
    mel_gui_invalidate(h);
}

static void canvas_key_down(Mel_Gui_Handle h, Mel_Key key, void* user)
{
    (void)h;
    (void)user;
    g_main.last_key = (i32)key;
    update_main_key_label();
}


void build_main(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    memset(&g_main, 0, sizeof g_main);
    g_main.frame = frame;
    g_main.slider = 65;
    snprintf(g_main.edit_text, sizeof g_main.edit_text, "%s", "native ui");

    mel_gui_set_text(frame, S8("Hello World Main"));

    mel_gui_set_layout(frame, mel_column_layout(
        .spacing = 8, .margin = 16, .cross_align = MEL_ALIGN_STRETCH));

    mel_label_create(frame, .text = S8("Main Screen"),
        .layoutable = { .preferred_w = 380, .preferred_h = 30 });
    mel_label_create(frame, .text = S8("This screen is selected and built from C."),
        .layoutable = { .preferred_h = 24 });

    mel_button_create(frame, .text = S8("Open C-defined Details Screen"),
        .pointer.on_click   = open_details_clicked,
        .focus.on_focus_in  = main_focus_in,
        .focus.on_focus_out = main_focus_out,
        .user = &g_main,
        .layoutable = { .preferred_h = 40 });

    mel_button_create(frame, .text = S8("Replace With Replaced Screen"),
        .pointer.on_click   = replace_screen_clicked,
        .focus.on_focus_in  = main_focus_in,
        .focus.on_focus_out = main_focus_out,
        .user = &g_main,
        .layoutable = { .preferred_h = 40 });

    g_main.edit = mel_textfield_create(frame, .text = S8("native ui"),
        .on_.on_text_changed   = main_edit_changed,
        .focus.on_focus_in     = main_focus_in,
        .focus.on_focus_out    = main_focus_out,
        .keyboard.on_key_down  = main_edit_key_down,
        .user = &g_main,
        .layoutable = { .preferred_h = 40 });

    tapcounter_create(frame, S8("Tap this button"));

    mel_checkbox_create(frame, .text = S8("Toggle this checkbox"),
        .on_.on_toggled     = main_checkbox_toggled,
        .focus.on_focus_in  = main_focus_in,
        .focus.on_focus_out = main_focus_out,
        .user = &g_main,
        .layoutable = { .preferred_h = 26 });

    mel_label_create(frame, .text = S8("Slider value"),
        .layoutable = { .preferred_h = 24, .cross_align = MEL_ALIGN_START, .preferred_w = 120 });
    mel_slider_create(frame,
        .min_value = 0, .max_value = 100, .value = 65,
        .on_.on_value_changed = main_slider_changed,
        .focus.on_focus_in    = main_focus_in,
        .focus.on_focus_out   = main_focus_out,
        .user = &g_main,
        .layoutable = { .preferred_h = 32 });
    g_main.slider_label = mel_label_create(frame, .text = S8("65"),
        .layoutable = { .preferred_h = 24, .cross_align = MEL_ALIGN_START, .preferred_w = 60 });

    g_main.status = mel_label_create(frame, .text = S8(""),
        .layoutable = { .preferred_h = 56 });
    g_main.key_label = mel_label_create(frame, .text = S8("last key: (none)"),
        .layoutable = { .preferred_h = 24 });

    g_main.canvas = mel_canvas_create(frame,
        .on_.on_paint            = canvas_paint,
        .pointer.on_pointer_down = canvas_pointer_down,
        .pointer.on_pointer_move = canvas_pointer_move,
        .pointer.on_pointer_up   = canvas_pointer_up,
        .keyboard.on_key_down    = canvas_key_down,
        .user = &g_main,
        .layoutable = { .preferred_h = 140, .weight = 1 });

    update_main_status();
    update_main_key_label();
}
