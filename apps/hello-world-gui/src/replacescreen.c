#include <gui/gui.h>

#include "replacescreen.h"
#include "tapcounter.h"


static void reopen_main_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    mel_app_present(S8("main"), NULL);
}

void build_replace(Mel_Gui_Handle frame, void* user)
{
    (void)user;

    mel_gui_set_text(frame, S8("Hello World Replaced"));

    mel_gui_set_layout(frame, mel_column_layout(
        .spacing = 8, .margin = 16, .cross_align = MEL_ALIGN_STRETCH));

    mel_label_create(frame, .text = S8("Replaced Screen"),
        .layoutable = { .preferred_w = 380, .preferred_h = 30 });
    mel_label_create(frame, .text = S8("This screen took over the current surface."),
        .layoutable = { .preferred_h = 24 });

    tapcounter_create(frame, S8("Poke replaced-local handler"));

    mel_button_create(frame, .text = S8("Re-open Main Window"),
        .pointer.on_click = reopen_main_clicked,
        .layoutable = { .preferred_h = 40 });
}
