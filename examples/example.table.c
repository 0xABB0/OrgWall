#include "../melody/core.app.h"
#include "../melody/ui.native.window.h"
#include "../melody/ui.native.panel.h"
#include "../melody/ui.native.button.h"
#include "../melody/ui.native.label.h"
#include "../melody/ui.native.tableview.h"
#include "../melody/ui.layout.box.h"
#include "../melody/string.str8.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const char* name;
    const char* agency;
    const char* followers;
    const char* debut_year;
} VTuber_Entry;

#define MAX_ENTRIES 32

static VTuber_Entry s_entries[MAX_ENTRIES] = {
    { "Projekt Melody", "Independent", "850K",  "2020" },
    { "Ironmouse",      "VShojo",      "1.5M",  "2017" },
    { "Silvervale",     "VShojo",      "400K",  "2020" },
    { "Gawr Gura",      "Hololive",    "4.4M",  "2020" },
    { "Shoto",          "Independent", "1.2M",  "2021" },
    { "Zentreya",       "VShojo",      "350K",  "2018" },
};
static i32 s_entry_count = 6;

static const VTuber_Entry s_random_pool[] = {
    { "Filian",     "Independent", "900K", "2022" },
    { "Neuro-sama", "Independent", "700K", "2022" },
    { "Vedal987",   "Independent", "500K", "2022" },
};
#define RANDOM_POOL_SIZE 3

static Mel_NWindow     s_window;
static Mel_NPanel      s_panel;
static Mel_BoxLayout   s_layout;

static Mel_NLabel      s_title_label;
static Mel_NTableView  s_table;
static Mel_NLabel      s_detail_label;
static char            s_detail_buf[256];

static Mel_NPanel      s_button_panel;
static Mel_BoxLayout   s_button_layout;
static Mel_NButton     s_add_btn;
static Mel_NButton     s_clear_btn;

static Mel_NTableColumn s_columns[] = {
    { .title = S8("Name"),       .width = 150.0f },
    { .title = S8("Agency"),     .width = 120.0f },
    { .title = S8("Followers"),  .width = 100.0f },
    { .title = S8("Debut Year"), .width = 100.0f },
};

static void on_window_close(void* user) { (void)user; mel_quit(); }
static void on_window_resize(f32 w, f32 h, void* user) { (void)user; (void)w; (void)h; }

static str8 on_table_data(i32 row, i32 col, void* user)
{
    (void)user;
    if (row < 0 || row >= s_entry_count) return STR8_EMPTY;
    VTuber_Entry* e = &s_entries[row];
    switch (col) {
        case 0: return str8_from_cstr(e->name);
        case 1: return str8_from_cstr(e->agency);
        case 2: return str8_from_cstr(e->followers);
        case 3: return str8_from_cstr(e->debut_year);
        default: return STR8_EMPTY;
    }
}

static void on_table_select(i32 row, void* user)
{
    (void)user;
    if (row < 0 || row >= s_entry_count) {
        snprintf(s_detail_buf, sizeof(s_detail_buf), "No selection");
    } else {
        VTuber_Entry* e = &s_entries[row];
        snprintf(s_detail_buf, sizeof(s_detail_buf),
                 "Selected: %s | %s | %s followers | Debut %s",
                 e->name, e->agency, e->followers, e->debut_year);
    }
    mel_nlabel_set_text(&s_detail_label, str8_from_cstr(s_detail_buf));
}

static void on_add_random(void* user)
{
    (void)user;
    if (s_entry_count >= MAX_ENTRIES) return;
    i32 idx = rand() % RANDOM_POOL_SIZE;
    s_entries[s_entry_count] = s_random_pool[idx];
    s_entry_count++;
    mel_ntableview_set_row_count(&s_table, s_entry_count);
    mel_ntableview_reload(&s_table);
}

static void on_clear_selection(void* user)
{
    (void)user;
    mel_ntableview_set_selected(&s_table, -1);
    snprintf(s_detail_buf, sizeof(s_detail_buf), "No selection");
    mel_nlabel_set_text(&s_detail_label, str8_from_cstr(s_detail_buf));
}

static void build_ui(void)
{
    mel_nwindow_init(&s_window, .title = S8("VTuber Database"), .width = 600, .height = 500);
    s_window.on_close  = on_window_close;
    s_window.on_resize = on_window_resize;
    s_window.user_data = NULL;

    mel_npanel_init(&s_panel);
    mel_nctrl_add_child(&s_window.base, &s_panel.base);

    mel_box_layout_init(&s_layout,
        .orientation = MEL_ORIENTATION_VERTICAL,
        .alignment   = MEL_ALIGN_FILL,
        .margin      = 20.0f,
        .spacing     = 10.0f);
    mel_nctrl_set_layout(&s_panel.base, &s_layout.base);

    mel_nlabel_init(&s_title_label, .text = S8("VTuber Database"), .font_size = 20.0f);
    s_title_label.base.fixed_size = mel_vec2(0, 30);
    mel_nctrl_add_child(&s_panel.base, &s_title_label.base);

    mel_ntableview_init(&s_table,
        .columns      = s_columns,
        .column_count = 4,
        .row_count    = s_entry_count);
    s_table.data_cb   = on_table_data;
    s_table.on_select = on_table_select;
    s_table.base.fixed_size = mel_vec2(0, 300);
    mel_nctrl_add_child(&s_panel.base, &s_table.base);

    snprintf(s_detail_buf, sizeof(s_detail_buf), "No selection");
    mel_nlabel_init(&s_detail_label, .text = str8_from_cstr(s_detail_buf));
    s_detail_label.base.fixed_size = mel_vec2(0, 20);
    mel_nctrl_add_child(&s_panel.base, &s_detail_label.base);

    mel_npanel_init(&s_button_panel);
    s_button_panel.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_panel.base, &s_button_panel.base);

    mel_box_layout_init(&s_button_layout,
        .orientation = MEL_ORIENTATION_HORIZONTAL,
        .alignment   = MEL_ALIGN_FILL,
        .margin      = 0.0f,
        .spacing     = 10.0f);
    mel_nctrl_set_layout(&s_button_panel.base, &s_button_layout.base);

    mel_nbutton_init(&s_add_btn, .text = S8("Add Random"));
    s_add_btn.on_click = on_add_random;
    s_add_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_button_panel.base, &s_add_btn.base);

    mel_nbutton_init(&s_clear_btn, .text = S8("Clear Selection"));
    s_clear_btn.on_click = on_clear_selection;
    s_clear_btn.base.fixed_size = mel_vec2(0, 32);
    mel_nctrl_add_child(&s_button_panel.base, &s_clear_btn.base);

    mel_nctrl_set_position(&s_panel.base, mel_vec2(0, 0));
    mel_nctrl_set_size(&s_panel.base, s_window.base.size);
    mel_nctrl_perform_layout(&s_panel.base);

    mel_nwindow_show(&s_window);
}

void app_init(void) { build_ui(); }
void app_shutdown(void) { mel_nctrl_destroy(&s_window.base); }
