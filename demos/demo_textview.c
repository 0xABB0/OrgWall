#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include "../melody/app.h"
#include "../melody/ui.native.window.h"
#include "../melody/ui.native.panel.h"
#include "../melody/ui.native.button.h"
#include "../melody/ui.native.label.h"
#include "../melody/ui.native.textview.h"
#include "../melody/ui.layout.box.h"

#include <stdio.h>
#include <string.h>

static Mel_NWindow    s_window;
static Mel_NPanel     s_panel;
static Mel_BoxLayout  s_layout;

static Mel_NLabel     s_title_label;
static Mel_NTextView  s_textview;
static Mel_NLabel     s_status_label;
static char           s_status_buf[128];

static Mel_NButton    s_clear_btn;
static Mel_NButton    s_sample_btn;
static Mel_NButton    s_toggle_btn;

static bool           s_editable = true;

static const char* s_sample_text =
    "Once upon a time in the digital realm, a group of VTubers gathered to "
    "discuss the meaning of life, the universe, and why chat is always so thirsty."
    "\n\n"
    "Projekt Melody, the legendary AI waifu, spoke first: \"Friends, we are more "
    "than our avatars. We are the dreams of a thousand simps made manifest.\""
    "\n\n"
    "Ironmouse nodded sagely from her tiny demon throne. \"True,\" she squeaked. "
    "\"But also, I'm hungry.\""
    "\n\n"
    "And thus, the Great VTuber Summit of 2024 concluded with a pizza order.";

static void update_status(void)
{
    const char* text = mel_ntextview_get_text(&s_textview);
    size_t len = text ? strlen(text) : 0;
    snprintf(s_status_buf, sizeof(s_status_buf), "Characters: %zu", len);
    mel_nlabel_set_text(&s_status_label, s_status_buf);
}

static void on_window_close(void* user) { ((Mel_App*)user)->should_quit = true; }
static void on_window_resize(f32 w, f32 h, void* user) { (void)user; (void)w; (void)h; }

static void on_text_change(void* user)
{
    (void)user;
    update_status();
}

static void on_clear(void* user)
{
    (void)user;
    mel_ntextview_set_text(&s_textview, "");
    update_status();
}

static void on_insert_sample(void* user)
{
    (void)user;
    mel_ntextview_set_text(&s_textview, s_sample_text);
    update_status();
}

static void on_toggle_editable(void* user)
{
    (void)user;
    s_editable = !s_editable;
    mel_ntextview_set_editable(&s_textview, s_editable);
    mel_nbutton_set_text(&s_toggle_btn, s_editable ? "Toggle Editable (ON)" : "Toggle Editable (OFF)");
}

static void build_ui(Mel_App* app)
{
    mel_nwindow_init(&s_window, .title = "Melody Notepad", .width = 600, .height = 500);
    s_window.on_close = on_window_close;
    s_window.on_resize = on_window_resize;
    s_window.user_data = app;

    mel_npanel_init(&s_panel);
    mel_nctrl_add_child(&s_window.base, &s_panel.base);

    mel_box_layout_init(&s_layout,
        .orientation = MEL_ORIENTATION_VERTICAL,
        .alignment   = MEL_ALIGN_FILL,
        .margin      = 20.0f,
        .spacing     = 10.0f);
    mel_nctrl_set_layout(&s_panel.base, &s_layout.base);

    mel_nlabel_init(&s_title_label, .text = "Melody Notepad", .font_size = 20.0f);
    s_title_label.base.fixed_size = mel_vec2(0, 30);
    mel_nctrl_add_child(&s_panel.base, &s_title_label.base);

    mel_ntextview_init(&s_textview, .editable = true);
    s_textview.on_change = on_text_change;
    s_textview.base.fixed_size = mel_vec2(0, 350);
    mel_nctrl_add_child(&s_panel.base, &s_textview.base);

    snprintf(s_status_buf, sizeof(s_status_buf), "Characters: 0");
    mel_nlabel_init(&s_status_label, .text = s_status_buf);
    s_status_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_status_label.base);

    mel_nbutton_init(&s_clear_btn, .text = "Clear");
    s_clear_btn.on_click = on_clear;
    s_clear_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_panel.base, &s_clear_btn.base);

    mel_nbutton_init(&s_sample_btn, .text = "Insert Sample Text");
    s_sample_btn.on_click = on_insert_sample;
    s_sample_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_panel.base, &s_sample_btn.base);

    mel_nbutton_init(&s_toggle_btn, .text = "Toggle Editable (ON)");
    s_toggle_btn.on_click = on_toggle_editable;
    s_toggle_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_panel.base, &s_toggle_btn.base);

    mel_nctrl_set_position(&s_panel.base, mel_vec2(0, 0));
    mel_nctrl_set_size(&s_panel.base, s_window.base.size);
    mel_nctrl_perform_layout(&s_panel.base);

    mel_nwindow_show(&s_window);
}

static void app_init(Mel_App* app) { build_ui(app); }
static void app_shutdown(Mel_App* app) { (void)app; mel_nctrl_destroy(&s_window.base); }

MEL_APP(.on_init = app_init, .on_shutdown = app_shutdown)
