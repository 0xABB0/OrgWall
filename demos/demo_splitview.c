#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include "../melody/app.h"
#include "../melody/ui.native.window.h"
#include "../melody/ui.native.panel.h"
#include "../melody/ui.native.splitview.h"
#include "../melody/ui.native.listbox.h"
#include "../melody/ui.native.label.h"
#include "../melody/ui.layout.box.h"

#include <stdio.h>

typedef struct {
    const char* name;
    const char* agency;
    const char* description;
    const char* debut;
    const char* fans;
} VTuber_Info;

static const VTuber_Info s_vtubers[] = {
    { "Projekt Melody", "Independent", "The OG AI waifu. Lewdest VTuber in existence.", "2020", "850K" },
    { "Ironmouse",      "VShojo",      "Tiny demon queen with a voice of an angel.",    "2017", "1.5M" },
    { "Gawr Gura",      "Hololive",    "Shark girl. A. Shaaark.",                       "2020", "4.4M" },
    { "Shoto",          "Independent", "Soft boy energy with big numbers.",              "2021", "1.2M" },
    { "Filian",         "Independent", "Maximum energy chaos gremlin.",                  "2022", "900K" },
};

static const char* s_names[] = {
    "Projekt Melody",
    "Ironmouse",
    "Gawr Gura",
    "Shoto",
    "Filian",
};

static Mel_NWindow     s_window;
static Mel_NSplitView  s_split;
static Mel_NPanel      s_left_panel;
static Mel_NPanel      s_right_panel;
static Mel_BoxLayout   s_detail_layout;
static Mel_NListbox    s_listbox;

static Mel_NLabel      s_name_label;
static Mel_NLabel      s_agency_label;
static Mel_NLabel      s_debut_label;
static Mel_NLabel      s_fans_label;
static Mel_NLabel      s_desc_label;

static char s_name_buf[128];
static char s_agency_buf[128];
static char s_debut_buf[128];
static char s_fans_buf[128];

static void update_detail(i32 index)
{
    const VTuber_Info* v = &s_vtubers[index];

    snprintf(s_name_buf, sizeof(s_name_buf), "Name: %s", v->name);
    mel_nlabel_set_text(&s_name_label, s_name_buf);

    snprintf(s_agency_buf, sizeof(s_agency_buf), "Agency: %s", v->agency);
    mel_nlabel_set_text(&s_agency_label, s_agency_buf);

    snprintf(s_debut_buf, sizeof(s_debut_buf), "Debut: %s", v->debut);
    mel_nlabel_set_text(&s_debut_label, s_debut_buf);

    snprintf(s_fans_buf, sizeof(s_fans_buf), "Fans: %s", v->fans);
    mel_nlabel_set_text(&s_fans_label, s_fans_buf);

    mel_nlabel_set_text(&s_desc_label, v->description);
}

static void on_select(i32 index, void* user)
{
    (void)user;
    if (index >= 0 && index < 5)
        update_detail(index);
}

static void on_window_close(void* user)
{
    ((Mel_App*)user)->should_quit = true;
}

static void on_window_resize(f32 w, f32 h, void* user)
{
    (void)user;
    (void)w;
    (void)h;
    mel_nctrl_set_size(&s_listbox.base, s_left_panel.base.size);
}

static void build_ui(Mel_App* app)
{
    mel_nwindow_init(&s_window, .title = "VTuber Browser", .width = 700, .height = 500);
    s_window.on_close = on_window_close;
    s_window.on_resize = on_window_resize;
    s_window.user_data = app;

    mel_nsplitview_init(&s_split, .orientation = MEL_ORIENTATION_HORIZONTAL, .divider_position = 0.3f);
    mel_nctrl_set_size(&s_split.base, s_window.base.size);
    mel_nctrl_add_child(&s_window.base, &s_split.base);

    mel_npanel_init(&s_left_panel);
    mel_nctrl_add_child(&s_split.base, &s_left_panel.base);

    mel_nlistbox_init(&s_listbox, .items = s_names, .item_count = 5);
    s_listbox.on_select = on_select;
    mel_nctrl_add_child(&s_left_panel.base, &s_listbox.base);

    mel_npanel_init(&s_right_panel);
    mel_nctrl_add_child(&s_split.base, &s_right_panel.base);

    mel_box_layout_init(&s_detail_layout,
        .orientation = MEL_ORIENTATION_VERTICAL,
        .alignment   = MEL_ALIGN_FILL,
        .margin      = 16.0f,
        .spacing     = 8.0f);
    mel_nctrl_set_layout(&s_right_panel.base, &s_detail_layout.base);

    snprintf(s_name_buf, sizeof(s_name_buf), "Name: %s", s_vtubers[0].name);
    mel_nlabel_init(&s_name_label, .text = s_name_buf, .font_size = 18.0f);
    s_name_label.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_right_panel.base, &s_name_label.base);

    snprintf(s_agency_buf, sizeof(s_agency_buf), "Agency: %s", s_vtubers[0].agency);
    mel_nlabel_init(&s_agency_label, .text = s_agency_buf);
    s_agency_label.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_right_panel.base, &s_agency_label.base);

    snprintf(s_debut_buf, sizeof(s_debut_buf), "Debut: %s", s_vtubers[0].debut);
    mel_nlabel_init(&s_debut_label, .text = s_debut_buf);
    s_debut_label.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_right_panel.base, &s_debut_label.base);

    snprintf(s_fans_buf, sizeof(s_fans_buf), "Fans: %s", s_vtubers[0].fans);
    mel_nlabel_init(&s_fans_label, .text = s_fans_buf);
    s_fans_label.base.fixed_size = mel_vec2(0, 24);
    mel_nctrl_add_child(&s_right_panel.base, &s_fans_label.base);

    mel_nlabel_init(&s_desc_label, .text = s_vtubers[0].description);
    s_desc_label.base.fixed_size = mel_vec2(0, 48);
    mel_nctrl_add_child(&s_right_panel.base, &s_desc_label.base);

    mel_nctrl_perform_layout(&s_window.base);
    mel_nctrl_set_size(&s_listbox.base, s_left_panel.base.size);

    mel_nwindow_show(&s_window);
}

static void app_init(Mel_App* app) { build_ui(app); }
static void app_shutdown(Mel_App* app) { (void)app; mel_nctrl_destroy(&s_window.base); }

MEL_APP(.on_init = app_init, .on_shutdown = app_shutdown)
