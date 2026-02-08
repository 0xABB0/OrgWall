#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include "../melody/app.h"
#include "../melody/ui.native.window.h"
#include "../melody/ui.native.panel.h"
#include "../melody/ui.native.button.h"
#include "../melody/ui.native.label.h"
#include "../melody/ui.native.slider.h"
#include "../melody/ui.native.checkbox.h"
#include "../melody/ui.layout.box.h"
#include "../melody/ui.layout.grid.h"
#include "../melody/ui.layout.group.h"

#include <stdio.h>

static Mel_NWindow     s_window;

static Mel_NPanel      s_main_panel;
static Mel_BoxLayout   s_main_layout;

static Mel_NPanel      s_vbox_panel;
static Mel_BoxLayout   s_vbox_layout;
static Mel_NLabel      s_vbox_title;
static Mel_NButton     s_vbox_btn_a;
static Mel_NButton     s_vbox_btn_b;
static Mel_NButton     s_vbox_btn_c;

static Mel_NPanel      s_hbox_panel;
static Mel_BoxLayout   s_hbox_layout;
static Mel_NLabel      s_hbox_title;
static Mel_NLabel      s_hbox_lbl_left;
static Mel_NLabel      s_hbox_lbl_center;
static Mel_NLabel      s_hbox_lbl_right;

static Mel_NPanel      s_grid_panel;
static Mel_GridLayout  s_grid_layout;
static Mel_NLabel      s_grid_title;
static Mel_NLabel      s_grid_lbl_1;
static Mel_NLabel      s_grid_lbl_2;
static Mel_NLabel      s_grid_lbl_3;
static Mel_NLabel      s_grid_lbl_4;

static Mel_NPanel      s_group_panel;
static Mel_GroupLayout s_group_layout;
static Mel_NLabel      s_group_title;
static Mel_NCheckbox   s_group_checkbox;
static Mel_NSlider     s_group_slider;
static Mel_NLabel      s_group_label;

static void on_window_close(void* user) { ((Mel_App*)user)->should_quit = true; }
static void on_window_resize(f32 w, f32 h, void* user) { (void)user; (void)w; (void)h; }

static void build_ui(Mel_App* app)
{
    mel_nwindow_init(&s_window, .title = "Melody Layout Demo", .width = 800, .height = 600);
    s_window.on_close = on_window_close;
    s_window.on_resize = on_window_resize;
    s_window.user_data = app;

    mel_npanel_init(&s_main_panel);
    mel_nctrl_add_child(&s_window.base, &s_main_panel.base);

    mel_box_layout_init(&s_main_layout,
        .orientation = MEL_ORIENTATION_VERTICAL,
        .alignment   = MEL_ALIGN_FILL,
        .margin      = 20.0f,
        .spacing     = 10.0f);
    mel_nctrl_set_layout(&s_main_panel.base, &s_main_layout.base);

    mel_npanel_init(&s_vbox_panel);
    s_vbox_panel.base.fixed_size = mel_vec2(0, 140);
    mel_nctrl_add_child(&s_main_panel.base, &s_vbox_panel.base);

    mel_box_layout_init(&s_vbox_layout,
        .orientation = MEL_ORIENTATION_VERTICAL,
        .alignment   = MEL_ALIGN_FILL,
        .margin      = 10.0f,
        .spacing     = 8.0f);
    mel_nctrl_set_layout(&s_vbox_panel.base, &s_vbox_layout.base);

    mel_nlabel_init(&s_vbox_title, .text = "Box Layout (Vertical)", .font_size = 16.0f);
    s_vbox_title.base.fixed_size = mel_vec2(0, 28);
    mel_nctrl_add_child(&s_vbox_panel.base, &s_vbox_title.base);

    mel_nbutton_init(&s_vbox_btn_a, .text = "Button A");
    s_vbox_btn_a.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_vbox_panel.base, &s_vbox_btn_a.base);

    mel_nbutton_init(&s_vbox_btn_b, .text = "Button B");
    s_vbox_btn_b.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_vbox_panel.base, &s_vbox_btn_b.base);

    mel_nbutton_init(&s_vbox_btn_c, .text = "Button C");
    s_vbox_btn_c.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_vbox_panel.base, &s_vbox_btn_c.base);

    mel_npanel_init(&s_hbox_panel);
    s_hbox_panel.base.fixed_size = mel_vec2(0, 120);
    mel_nctrl_add_child(&s_main_panel.base, &s_hbox_panel.base);

    mel_box_layout_init(&s_hbox_layout,
        .orientation = MEL_ORIENTATION_HORIZONTAL,
        .alignment   = MEL_ALIGN_FILL,
        .margin      = 10.0f,
        .spacing     = 10.0f);
    mel_nctrl_set_layout(&s_hbox_panel.base, &s_hbox_layout.base);

    mel_nlabel_init(&s_hbox_title, .text = "Box Layout (Horizontal)", .font_size = 16.0f);
    s_hbox_title.base.fixed_size = mel_vec2(0, 28);
    mel_nctrl_add_child(&s_hbox_panel.base, &s_hbox_title.base);

    mel_nlabel_init(&s_hbox_lbl_left, .text = "Left", .font_size = 14.0f);
    s_hbox_lbl_left.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_hbox_panel.base, &s_hbox_lbl_left.base);

    mel_nlabel_init(&s_hbox_lbl_center, .text = "Center", .font_size = 14.0f);
    s_hbox_lbl_center.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_hbox_panel.base, &s_hbox_lbl_center.base);

    mel_nlabel_init(&s_hbox_lbl_right, .text = "Right", .font_size = 14.0f);
    s_hbox_lbl_right.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_hbox_panel.base, &s_hbox_lbl_right.base);

    mel_npanel_init(&s_grid_panel);
    s_grid_panel.base.fixed_size = mel_vec2(0, 120);
    mel_nctrl_add_child(&s_main_panel.base, &s_grid_panel.base);

    mel_grid_layout_init(&s_grid_layout,
        .orientation = MEL_ORIENTATION_HORIZONTAL,
        .resolution  = 2,
        .alignment   = MEL_ALIGN_FILL,
        .margin      = 10.0f,
        .spacing     = 10.0f);
    mel_nctrl_set_layout(&s_grid_panel.base, &s_grid_layout.base);

    mel_nlabel_init(&s_grid_title, .text = "Grid Layout", .font_size = 16.0f);
    s_grid_title.base.fixed_size = mel_vec2(0, 28);
    mel_nctrl_add_child(&s_grid_panel.base, &s_grid_title.base);

    mel_nlabel_init(&s_grid_lbl_1, .text = "Grid 1", .font_size = 14.0f);
    s_grid_lbl_1.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_grid_panel.base, &s_grid_lbl_1.base);

    mel_nlabel_init(&s_grid_lbl_2, .text = "Grid 2", .font_size = 14.0f);
    s_grid_lbl_2.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_grid_panel.base, &s_grid_lbl_2.base);

    mel_nlabel_init(&s_grid_lbl_3, .text = "Grid 3", .font_size = 14.0f);
    s_grid_lbl_3.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_grid_panel.base, &s_grid_lbl_3.base);

    mel_nlabel_init(&s_grid_lbl_4, .text = "Grid 4", .font_size = 14.0f);
    s_grid_lbl_4.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_grid_panel.base, &s_grid_lbl_4.base);

    mel_npanel_init(&s_group_panel);
    s_group_panel.base.fixed_size = mel_vec2(0, 130);
    mel_nctrl_add_child(&s_main_panel.base, &s_group_panel.base);

    mel_group_layout_init(&s_group_layout,
        .margin        = 10.0f,
        .spacing       = 8.0f,
        .group_spacing = 16.0f,
        .group_indent  = 12.0f);
    mel_nctrl_set_layout(&s_group_panel.base, &s_group_layout.base);

    mel_nlabel_init(&s_group_title, .text = "Group Layout", .font_size = 16.0f);
    s_group_title.base.fixed_size = mel_vec2(0, 28);
    mel_nctrl_add_child(&s_group_panel.base, &s_group_title.base);

    mel_ncheckbox_init(&s_group_checkbox, .text = "Check me", .checked = true);
    s_group_checkbox.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_group_panel.base, &s_group_checkbox.base);

    mel_nslider_init(&s_group_slider, .min = 0.0, .max = 100.0, .value = 50.0);
    s_group_slider.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_group_panel.base, &s_group_slider.base);

    mel_nlabel_init(&s_group_label, .text = "Grouped label", .font_size = 14.0f);
    s_group_label.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_group_panel.base, &s_group_label.base);

    mel_nctrl_set_position(&s_main_panel.base, mel_vec2(0, 0));
    mel_nctrl_set_size(&s_main_panel.base, s_window.base.size);
    mel_nctrl_perform_layout(&s_main_panel.base);

    mel_nwindow_show(&s_window);
}

static void app_init(Mel_App* app) { build_ui(app); }
static void app_shutdown(Mel_App* app) { (void)app; mel_nctrl_destroy(&s_window.base); }

MEL_APP(.on_init = app_init, .on_shutdown = app_shutdown)
