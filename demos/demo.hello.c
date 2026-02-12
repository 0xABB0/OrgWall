#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include "../melody/app.h"
#include "../melody/ui.native.window.h"
#include "../melody/ui.native.panel.h"
#include "../melody/ui.native.button.h"
#include "../melody/ui.native.label.h"
#include "../melody/ui.layout.box.h"
#include "../melody/string.str8.h"

#include <stdio.h>

static Mel_NWindow   s_window;
static Mel_NPanel    s_panel;
static Mel_BoxLayout s_layout;
static Mel_NLabel    s_title_label;
static Mel_NLabel    s_subtitle_label;
static Mel_NButton   s_hello_btn;
static i32           s_greeting_index;

static str8 s_greetings[] = {
    S8("Hii~ \xF0\x9F\x92\x9C"),
    S8("Konnichiwa! \xF0\x9F\x8C\xB8"),
    S8("Bonjour~ \xE2\x9C\xA8"),
    S8("Hola! \xF0\x9F\x94\xA5"),
    S8("Ciao~ \xF0\x9F\x98\x98"),
};

static void on_hello_click(void* user)
{
    (void)user;
    mel_nlabel_set_text(&s_subtitle_label, s_greetings[s_greeting_index]);
    s_greeting_index = (s_greeting_index + 1) % 5;
    mel_nctrl_perform_layout(&s_panel.base);
}

static void on_window_close(void* user)
{
    Mel_App* app = (Mel_App*)user;
    app->should_quit = true;
}

static void on_window_resize(f32 w, f32 h, void* user)
{
    (void)user;
    (void)w;
    (void)h;
}

static void build_ui(Mel_App* app)
{
    mel_nwindow_init(&s_window, .title = S8("Hello, Melody!"), .width = 400, .height = 300);
    s_window.on_close = on_window_close;
    s_window.on_resize = on_window_resize;
    s_window.user_data = app;

    mel_npanel_init(&s_panel);
    mel_nctrl_add_child(&s_window.base, &s_panel.base);

    mel_box_layout_init(&s_layout,
        .orientation = MEL_ORIENTATION_VERTICAL,
        .alignment   = MEL_ALIGN_MIDDLE,
        .margin      = 20.0f,
        .spacing     = 10.0f);
    mel_nctrl_set_layout(&s_panel.base, &s_layout.base);

    mel_nlabel_init(&s_title_label, .text = S8("Hello, World!"), .font_size = 24.0f);
    s_title_label.base.fixed_size = mel_vec2(0, 30);
    mel_nctrl_add_child(&s_panel.base, &s_title_label.base);

    mel_nlabel_init(&s_subtitle_label, .text = S8("Welcome to the Melody UI engine"));
    s_subtitle_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_subtitle_label.base);

    mel_nbutton_init(&s_hello_btn, .text = S8("Say Hello~"));
    s_hello_btn.on_click = on_hello_click;
    s_hello_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_panel.base, &s_hello_btn.base);

    mel_nctrl_set_position(&s_panel.base, mel_vec2(0, 0));
    mel_nctrl_set_size(&s_panel.base, s_window.base.size);
    mel_nctrl_perform_layout(&s_panel.base);

    mel_nwindow_show(&s_window);
}

static void app_init(Mel_App* app) { build_ui(app); }
static void app_shutdown(Mel_App* app) { (void)app; mel_nctrl_destroy(&s_window.base); }

MEL_APP(.on_init = app_init, .on_shutdown = app_shutdown)
