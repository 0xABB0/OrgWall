#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include "../melody/app.h"
#include "../melody/ui.native.window.h"
#include "../melody/ui.native.panel.h"
#include "../melody/ui.native.button.h"
#include "../melody/ui.native.label.h"
#include "../melody/ui.native.edit.h"
#include "../melody/ui.native.slider.h"
#include "../melody/ui.native.checkbox.h"
#include "../melody/ui.native.combo.h"
#include "../melody/ui.layout.box.h"

#include <stdio.h>
#include <string.h>

static Mel_NWindow    s_window;
static Mel_NPanel     s_panel;
static Mel_BoxLayout  s_layout;

static Mel_NLabel     s_title_label;

static Mel_NLabel     s_name_label;
static Mel_NEdit      s_name_edit;

static Mel_NLabel     s_agency_label;
static Mel_NCombo     s_agency_combo;

static Mel_NLabel     s_followers_label;
static Mel_NSlider    s_followers_slider;
static Mel_NLabel     s_followers_value_label;
static f64            s_followers_value;
static char           s_followers_buf[64];

static Mel_NCheckbox  s_model_checkbox;
static Mel_NCheckbox  s_donations_checkbox;

static Mel_NButton    s_submit_btn;
static Mel_NButton    s_clear_btn;

static Mel_NLabel     s_status_label;
static char           s_status_buf[512];

static const char* s_agency_items[] = {
    "Independent",
    "VShojo",
    "Hololive",
    "Nijisanji",
};

static void on_window_close(void* user) { ((Mel_App*)user)->should_quit = true; }
static void on_window_resize(f32 w, f32 h, void* user) { (void)user; (void)w; (void)h; }

static void on_followers_change(f64 value, void* user)
{
    (void)user;
    s_followers_value = value;
    snprintf(s_followers_buf, sizeof(s_followers_buf), "%.0f", value);
    mel_nlabel_set_text(&s_followers_value_label, s_followers_buf);
}

static void on_submit(void* user)
{
    (void)user;
    const char* name = mel_nedit_get_text(&s_name_edit);
    const char* agency = s_agency_items[s_agency_combo.selected];
    snprintf(s_status_buf, sizeof(s_status_buf),
             "Registered: %s from %s, %.0f followers",
             (name && name[0]) ? name : "(unnamed)", agency, s_followers_value);
    mel_nlabel_set_text(&s_status_label, s_status_buf);
}

static void on_clear(void* user)
{
    (void)user;
    mel_nedit_set_text(&s_name_edit, "");
    mel_ncombo_set_selected(&s_agency_combo, 0);
    mel_nslider_set_value(&s_followers_slider, 0.0);
    s_followers_value = 0.0;
    snprintf(s_followers_buf, sizeof(s_followers_buf), "0");
    mel_nlabel_set_text(&s_followers_value_label, s_followers_buf);
    mel_ncheckbox_set_checked(&s_model_checkbox, false);
    mel_ncheckbox_set_checked(&s_donations_checkbox, false);
    mel_nlabel_set_text(&s_status_label, "");
}

static void build_ui(Mel_App* app)
{
    mel_nwindow_init(&s_window, .title = "VTuber Registration", .width = 500, .height = 550);
    s_window.on_close = on_window_close;
    s_window.on_resize = on_window_resize;
    s_window.user_data = app;

    mel_npanel_init(&s_panel);
    mel_nctrl_add_child(&s_window.base, &s_panel.base);

    mel_box_layout_init(&s_layout,
        .orientation = MEL_ORIENTATION_VERTICAL,
        .alignment   = MEL_ALIGN_FILL,
        .margin      = 20.0f,
        .spacing     = 8.0f);
    mel_nctrl_set_layout(&s_panel.base, &s_layout.base);

    mel_nlabel_init(&s_title_label, .text = "VTuber Registration Form", .font_size = 20.0f);
    s_title_label.base.fixed_size = mel_vec2(0, 30);
    mel_nctrl_add_child(&s_panel.base, &s_title_label.base);

    mel_nlabel_init(&s_name_label, .text = "Name:");
    s_name_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_name_label.base);

    mel_nedit_init(&s_name_edit, .placeholder = "Enter your VTuber name...");
    s_name_edit.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_panel.base, &s_name_edit.base);

    mel_nlabel_init(&s_agency_label, .text = "Agency:");
    s_agency_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_agency_label.base);

    mel_ncombo_init(&s_agency_combo, .items = s_agency_items, .item_count = 4, .selected = 0);
    s_agency_combo.base.fixed_size = mel_vec2(0, 28);
    mel_nctrl_add_child(&s_panel.base, &s_agency_combo.base);

    mel_nlabel_init(&s_followers_label, .text = "Followers:");
    s_followers_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_followers_label.base);

    mel_nslider_init(&s_followers_slider, .min = 0.0, .max = 1000000.0, .value = 0.0);
    s_followers_slider.on_change = on_followers_change;
    s_followers_slider.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_panel.base, &s_followers_slider.base);

    snprintf(s_followers_buf, sizeof(s_followers_buf), "0");
    mel_nlabel_init(&s_followers_value_label, .text = s_followers_buf);
    s_followers_value_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_followers_value_label.base);

    mel_ncheckbox_init(&s_model_checkbox, .text = "Has 3D Model", .checked = false);
    s_model_checkbox.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_panel.base, &s_model_checkbox.base);

    mel_ncheckbox_init(&s_donations_checkbox, .text = "Accepts Donations", .checked = false);
    s_donations_checkbox.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_panel.base, &s_donations_checkbox.base);

    mel_nbutton_init(&s_submit_btn, .text = "Submit");
    s_submit_btn.on_click = on_submit;
    s_submit_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_panel.base, &s_submit_btn.base);

    mel_nbutton_init(&s_clear_btn, .text = "Clear");
    s_clear_btn.on_click = on_clear;
    s_clear_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_panel.base, &s_clear_btn.base);

    mel_nlabel_init(&s_status_label, .text = "");
    s_status_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_status_label.base);

    mel_nctrl_set_position(&s_panel.base, mel_vec2(0, 0));
    mel_nctrl_set_size(&s_panel.base, s_window.base.size);
    mel_nctrl_perform_layout(&s_panel.base);

    mel_nwindow_show(&s_window);
}

static void app_init(Mel_App* app) { build_ui(app); }
static void app_shutdown(Mel_App* app) { (void)app; mel_nctrl_destroy(&s_window.base); }

MEL_APP(.on_init = app_init, .on_shutdown = app_shutdown)
