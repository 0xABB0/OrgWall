#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include "../melody/app.h"
#include "../melody/ui.native.window.h"
#include "../melody/ui.native.panel.h"
#include "../melody/ui.native.button.h"
#include "../melody/ui.native.label.h"
#include "../melody/ui.native.menu.h"
#include "../melody/ui.native.menuitem.h"
#include "../melody/ui.native.popup.h"
#include "../melody/ui.layout.box.h"

#include <stdio.h>

static Mel_NWindow    s_window;
static Mel_NPanel     s_panel;
static Mel_BoxLayout  s_layout;

static Mel_NLabel     s_title_label;
static Mel_NLabel     s_action_label;
static char           s_action_buf[128];

static Mel_NButton    s_context_btn;
static Mel_NMenu      s_menu;
static Mel_NMenuItem  s_item_copy;
static Mel_NMenuItem  s_item_paste;
static Mel_NMenuItem  s_item_delete;
static Mel_NMenuItem  s_item_select_all;

static Mel_NButton    s_popup_btn;
static Mel_NPopup     s_popup;
static Mel_BoxLayout  s_popup_layout;
static Mel_NLabel     s_popup_label;
static Mel_NButton    s_popup_close_btn;

static Mel_NLabel     s_status_label;

static void update_action(const char* action)
{
    snprintf(s_action_buf, sizeof(s_action_buf), "Last action: %s", action);
    mel_nlabel_set_text(&s_action_label, s_action_buf);
}

static void on_copy(void* user)    { (void)user; update_action("Copy"); }
static void on_paste(void* user)   { (void)user; update_action("Paste"); }
static void on_delete(void* user)  { (void)user; update_action("Delete"); }
static void on_select_all(void* user) { (void)user; update_action("Select All"); }

static void on_context_menu(void* user)
{
    (void)user;
    mel_nmenu_popup(&s_menu, mel_vec2(0, 0), &s_context_btn.base);
}

static void on_show_popup(void* user)
{
    (void)user;
    mel_npopup_show_relative_to(&s_popup, &s_popup_btn.base);
    mel_nlabel_set_text(&s_status_label, "Popup is open");
}

static void on_close_popup(void* user)
{
    (void)user;
    mel_npopup_close(&s_popup);
    mel_nlabel_set_text(&s_status_label, "Popup closed");
}

static void on_window_close(void* user) { ((Mel_App*)user)->should_quit = true; }
static void on_window_resize(f32 w, f32 h, void* user) { (void)user; (void)w; (void)h; }

static void build_ui(Mel_App* app)
{
    mel_nwindow_init(&s_window, .title = "Melody Menu Demo", .width = 500, .height = 400);
    s_window.on_close = on_window_close;
    s_window.on_resize = on_window_resize;
    s_window.user_data = app;

    mel_npanel_init(&s_panel);
    mel_nctrl_add_child(&s_window.base, &s_panel.base);

    mel_box_layout_init(&s_layout,
        .orientation = MEL_ORIENTATION_VERTICAL,
        .alignment   = MEL_ALIGN_FILL,
        .margin      = 20.0f,
        .spacing     = 12.0f);
    mel_nctrl_set_layout(&s_panel.base, &s_layout.base);

    mel_nlabel_init(&s_title_label, .text = "Menu & Popup Demo", .font_size = 20.0f);
    s_title_label.base.fixed_size = mel_vec2(0, 30);
    mel_nctrl_add_child(&s_panel.base, &s_title_label.base);

    mel_nlabel_init(&s_action_label, .text = "Last action: (none)");
    s_action_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_action_label.base);

    mel_nmenu_init(&s_menu, .title = "Context");

    mel_nmenuitem_init(&s_item_copy, .title = "Copy", .key_equivalent = "c");
    s_item_copy.on_action = on_copy;
    mel_nctrl_add_child(&s_menu.base, &s_item_copy.base);

    mel_nmenuitem_init(&s_item_paste, .title = "Paste", .key_equivalent = "v");
    s_item_paste.on_action = on_paste;
    mel_nctrl_add_child(&s_menu.base, &s_item_paste.base);

    mel_nmenuitem_init(&s_item_delete, .title = "Delete");
    s_item_delete.on_action = on_delete;
    mel_nctrl_add_child(&s_menu.base, &s_item_delete.base);

    mel_nmenuitem_init(&s_item_select_all, .title = "Select All", .key_equivalent = "a");
    s_item_select_all.on_action = on_select_all;
    mel_nctrl_add_child(&s_menu.base, &s_item_select_all.base);

    mel_nbutton_init(&s_context_btn, .text = "Show Context Menu");
    s_context_btn.on_click = on_context_menu;
    s_context_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_panel.base, &s_context_btn.base);

    mel_npopup_init(&s_popup, .width = 200.0f, .height = 100.0f, .side = MEL_NPOPUP_SIDE_BOTTOM);

    mel_box_layout_init(&s_popup_layout,
        .orientation = MEL_ORIENTATION_VERTICAL,
        .alignment   = MEL_ALIGN_FILL,
        .margin      = 10.0f,
        .spacing     = 8.0f);
    mel_nctrl_set_layout(&s_popup.base, &s_popup_layout.base);

    mel_nlabel_init(&s_popup_label, .text = "I'm a popup!");
    s_popup_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_popup.base, &s_popup_label.base);

    mel_nbutton_init(&s_popup_close_btn, .text = "Close Me");
    s_popup_close_btn.on_click = on_close_popup;
    s_popup_close_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_popup.base, &s_popup_close_btn.base);

    mel_nctrl_perform_layout(&s_popup.base);

    mel_nbutton_init(&s_popup_btn, .text = "Show Popup");
    s_popup_btn.on_click = on_show_popup;
    s_popup_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_panel.base, &s_popup_btn.base);

    mel_nlabel_init(&s_status_label, .text = "Ready");
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
