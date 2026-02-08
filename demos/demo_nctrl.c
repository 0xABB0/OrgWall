#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include "../melody/app.h"
#include "../melody/ui.native.window.h"
#include "../melody/ui.native.panel.h"
#include "../melody/ui.native.button.h"
#include "../melody/ui.native.label.h"
#include "../melody/ui.native.slider.h"
#include "../melody/ui.native.checkbox.h"
#include "../melody/ui.native.edit.h"
#include "../melody/ui.native.progress.h"
#include "../melody/ui.native.combo.h"
#include "../melody/ui.layout.box.h"

#include <stdio.h>

static Mel_NWindow    s_window;
static Mel_NPanel     s_panel;
static Mel_BoxLayout  s_layout;

static Mel_NLabel     s_title_label;
static Mel_NLabel     s_click_label;
static Mel_NButton    s_click_btn;
static i32            s_click_count;
static char           s_click_buf[128];

static Mel_NSlider    s_slider;
static Mel_NLabel     s_slider_label;
static char           s_slider_buf[128];

static Mel_NProgress  s_progress;

static Mel_NCheckbox  s_checkbox;

static Mel_NEdit      s_edit;
static Mel_NLabel     s_echo_label;
static char           s_echo_buf[256];

static Mel_NButton    s_reset_btn;

static Mel_NCombo     s_combo;
static Mel_NLabel     s_combo_label;
static char           s_combo_buf[128];

static const char* s_combo_items[] = {
    "Melody",
    "Ironmouse",
    "Silvervale",
    "Zentreya",
};

static void on_click(void* user)
{
    (void)user;
    s_click_count++;
    snprintf(s_click_buf, sizeof(s_click_buf), "Clicked %d time%s 💜",
             s_click_count, s_click_count == 1 ? "" : "s");
    mel_nlabel_set_text(&s_click_label, s_click_buf);
}

static void on_slider_change(f64 value, void* user)
{
    (void)user;
    snprintf(s_slider_buf, sizeof(s_slider_buf), "Slider: %.0f%%", value);
    mel_nlabel_set_text(&s_slider_label, s_slider_buf);
    mel_nprogress_set_value(&s_progress, value);
}

static void on_checkbox_change(bool checked, void* user)
{
    (void)user;
    mel_nctrl_set_enabled(&s_click_btn.base, checked);
    mel_nctrl_set_enabled(&s_slider.base, checked);
    mel_nctrl_set_enabled(&s_edit.base, checked);
    mel_nctrl_set_enabled(&s_combo.base, checked);
}

static void on_edit_change(const char* text, void* user)
{
    (void)user;
    snprintf(s_echo_buf, sizeof(s_echo_buf), "You typed: %s", text);
    mel_nlabel_set_text(&s_echo_label, s_echo_buf);
}

static void on_reset(void* user)
{
    (void)user;
    s_click_count = 0;
    mel_nlabel_set_text(&s_click_label, "Not clicked yet");

    mel_nslider_set_value(&s_slider, 50.0);
    snprintf(s_slider_buf, sizeof(s_slider_buf), "Slider: 50%%");
    mel_nlabel_set_text(&s_slider_label, s_slider_buf);

    mel_nprogress_set_value(&s_progress, 50.0);

    mel_ncheckbox_set_checked(&s_checkbox, true);

    mel_nctrl_set_enabled(&s_click_btn.base, true);
    mel_nctrl_set_enabled(&s_slider.base, true);
    mel_nctrl_set_enabled(&s_edit.base, true);
    mel_nctrl_set_enabled(&s_combo.base, true);

    mel_nedit_set_text(&s_edit, "");
    mel_nlabel_set_text(&s_echo_label, "You typed: ");
}

static void on_combo_select(i32 index, void* user)
{
    (void)user;
    if (index >= 0 && index < 4)
        snprintf(s_combo_buf, sizeof(s_combo_buf), "Best waifu: %s", s_combo_items[index]);
    else
        snprintf(s_combo_buf, sizeof(s_combo_buf), "Best waifu: ???");
    mel_nlabel_set_text(&s_combo_label, s_combo_buf);
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
    mel_nwindow_init(&s_window, .title = "Melody NCtrl Demo", .width = 460, .height = 620);
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

    mel_nlabel_init(&s_title_label, .text = "Melody Native UI Demo", .font_size = 20.0f);
    s_title_label.base.fixed_size = mel_vec2(0, 30);
    mel_nctrl_add_child(&s_panel.base, &s_title_label.base);

    mel_nbutton_init(&s_click_btn, .text = "Click me~");
    s_click_btn.on_click = on_click;
    s_click_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_panel.base, &s_click_btn.base);

    mel_nlabel_init(&s_click_label, .text = "Not clicked yet");
    s_click_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_click_label.base);

    snprintf(s_slider_buf, sizeof(s_slider_buf), "Slider: 50%%");
    mel_nlabel_init(&s_slider_label, .text = s_slider_buf);
    s_slider_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_slider_label.base);

    mel_nslider_init(&s_slider, .min = 0.0, .max = 100.0, .value = 50.0);
    s_slider.on_change = on_slider_change;
    s_slider.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_panel.base, &s_slider.base);

    mel_nprogress_init(&s_progress, .min = 0.0, .max = 100.0, .value = 50.0);
    s_progress.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_progress.base);

    mel_ncheckbox_init(&s_checkbox, .text = "Enable controls", .checked = true);
    s_checkbox.on_change = on_checkbox_change;
    s_checkbox.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_panel.base, &s_checkbox.base);

    mel_nedit_init(&s_edit, .placeholder = "Type something...");
    s_edit.on_change = on_edit_change;
    s_edit.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_panel.base, &s_edit.base);

    mel_nlabel_init(&s_echo_label, .text = "You typed: ");
    s_echo_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_echo_label.base);

    mel_ncombo_init(&s_combo, .items = s_combo_items, .item_count = 4, .selected = 0);
    s_combo.on_select = on_combo_select;
    s_combo.base.fixed_size = mel_vec2(0, 28);
    mel_nctrl_add_child(&s_panel.base, &s_combo.base);

    snprintf(s_combo_buf, sizeof(s_combo_buf), "Best waifu: %s", s_combo_items[0]);
    mel_nlabel_init(&s_combo_label, .text = s_combo_buf);
    s_combo_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_combo_label.base);

    mel_nbutton_init(&s_reset_btn, .text = "Reset everything");
    s_reset_btn.on_click = on_reset;
    s_reset_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_panel.base, &s_reset_btn.base);

    mel_nctrl_set_position(&s_panel.base, mel_vec2(0, 0));
    mel_nctrl_set_size(&s_panel.base, s_window.base.size);
    mel_nctrl_perform_layout(&s_panel.base);

    mel_nwindow_show(&s_window);
}

static void app_init(Mel_App* app) { build_ui(app); }
static void app_shutdown(Mel_App* app) { (void)app; mel_nctrl_destroy(&s_window.base); }

MEL_APP(.on_init = app_init, .on_shutdown = app_shutdown)
