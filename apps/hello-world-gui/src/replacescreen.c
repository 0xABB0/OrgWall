#include <stdio.h>
#include <string.h>

#include <core/platform.h>
#include <app/app.h>
#include <gui/gui.h>


typedef struct {
    Mel_Gui_Handle frame;
    Mel_Gui_Handle status;
    i32  pokes;
} Replace_State;

static Replace_State g_replace;

static void update_replace_status(void)
{
    char text[160];
    snprintf(text, sizeof text,
        "Replaced screen keeps its own C state: pokes=%d", g_replace.pokes);
    mel_gui_set_text(g_replace.status, str8_from_cstr(text));
}

static void replace_poke_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    g_replace.pokes += 1;
    update_replace_status();
}

static void replace_back_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    mel_app_back();
}

void build_replace(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    memset(&g_replace, 0, sizeof g_replace);
    g_replace.frame = frame;

    mel_gui_set_text(frame, S8("Hello World Replaced"));

    mel_gui_set_layout(frame, mel_column_layout(
        .spacing = 8, .margin = 16, .cross_align = MEL_ALIGN_STRETCH));

    mel_label_create(frame, .text = S8("Replaced Screen"),
        .layoutable = { .preferred_w = 380, .preferred_h = 30 });
    mel_label_create(frame, .text = S8("This screen took over the current surface."),
        .layoutable = { .preferred_h = 24 });

    mel_button_create(frame, .text = S8("Poke replaced-local handler"),
        .pointer.on_click = replace_poke_clicked,
        .user = &g_replace,
        .layoutable = { .preferred_h = 40 });

    mel_button_create(frame, .text = S8("Back"),
        .pointer.on_click = replace_back_clicked,
        .user = &g_replace,
        .layoutable = { .preferred_h = 40 });

    g_replace.status = mel_label_create(frame, .text = S8(""),
        .layoutable = { .preferred_h = 40 });

    update_replace_status();
}
