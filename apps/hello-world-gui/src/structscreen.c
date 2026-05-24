#include <stdio.h>

#include <app/app.h>
#include <gui/gui.h>

#include "structscreen.h"

static Mel_Gui_Handle g_struct_status;

static void dialog_result(Mel_Gui_Handle h, i32 result, void* user)
{
    (void)h;
    (void)user;
    if (mel_gui_handle_is_none(g_struct_status)) return;
    char buf[64];
    snprintf(buf, sizeof buf, "dialog closed with result %d", result);
    mel_gui_set_text(g_struct_status, str8_from_cstr(buf));
}

static Mel_Gui_Handle g_dialog;

static void dialog_ok(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    mel_dialog_close(g_dialog, 1);
}

static void dialog_cancel(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    mel_dialog_close(g_dialog, 0);
}

static void open_dialog_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    g_dialog = mel_dialog_create(
        .title = S8("Confirm"),
        .w = 320, .h = 160,
        .on_.on_result = dialog_result,
        .layout = mel_column_layout(.spacing = 10, .margin = 16, .cross_align = MEL_ALIGN_STRETCH));

    mel_label_create(g_dialog, .text = S8("A modeless dialog. Pick an outcome."),
        .layoutable = { .preferred_h = 40, .weight = 1 });
    mel_button_create(g_dialog, .text = S8("OK"),
        .pointer.on_click = dialog_ok,
        .layoutable = { .preferred_h = 34 });
    mel_button_create(g_dialog, .text = S8("Cancel"),
        .pointer.on_click = dialog_cancel,
        .layoutable = { .preferred_h = 34 });

    mel_gui_relayout(g_dialog);
}

static void back_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    mel_app_present(S8("main"));
}

void build_struct(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    g_struct_status = MEL_GUI_HANDLE_NONE;

    mel_gui_set_text(frame, S8("Structural Widgets"));
    mel_gui_set_layout(frame, mel_column_layout(
        .spacing = 8, .margin = 16, .cross_align = MEL_ALIGN_STRETCH));

    mel_label_create(frame, .text = S8("TabView, Splitter, Dialog"),
        .layoutable = { .preferred_w = 380, .preferred_h = 28 });

    Mel_Gui_Handle tabs = mel_tabview_create(frame,
        .layoutable = { .preferred_h = 160, .weight = 1 });

    Mel_Gui_Handle t1 = mel_tab_create(tabs, .title = S8("First"),
        .layout = mel_column_layout(.spacing = 6, .margin = 12, .cross_align = MEL_ALIGN_STRETCH));
    mel_label_create(t1, .text = S8("Each tab is its own node."),
        .layoutable = { .preferred_h = 24 });
    mel_button_create(t1, .text = S8("Children parent to the tab."),
        .layoutable = { .preferred_h = 34 });

    Mel_Gui_Handle t2 = mel_tab_create(tabs, .title = S8("Second"),
        .layout = mel_column_layout(.spacing = 6, .margin = 12, .cross_align = MEL_ALIGN_STRETCH));
    mel_label_create(t2, .text = S8("Second page content."),
        .layoutable = { .preferred_h = 24 });

    Mel_Gui_Handle split = mel_splitter_create(frame,
        .orientation = MEL_SPLIT_HORIZONTAL,
        .layoutable = { .preferred_h = 140, .weight = 1 });

    Mel_Gui_Handle left = mel_splitpane_create(split, .min_size = 80,
        .layout = mel_column_layout(.spacing = 6, .margin = 10, .cross_align = MEL_ALIGN_STRETCH));
    mel_label_create(left, .text = S8("Left pane"),
        .layoutable = { .preferred_h = 24 });

    Mel_Gui_Handle right = mel_splitpane_create(split, .min_size = 80,
        .layout = mel_column_layout(.spacing = 6, .margin = 10, .cross_align = MEL_ALIGN_STRETCH));
    mel_label_create(right, .text = S8("Right pane, drag the divider"),
        .layoutable = { .preferred_h = 24 });

    mel_button_create(frame, .text = S8("Open Dialog"),
        .pointer.on_click = open_dialog_clicked,
        .layoutable = { .preferred_h = 38 });

    g_struct_status = mel_label_create(frame, .text = S8("dialog: (not opened)"),
        .layoutable = { .preferred_h = 24 });

    mel_button_create(frame, .text = S8("Back to Main"),
        .pointer.on_click = back_clicked,
        .layoutable = { .preferred_h = 38 });
}
