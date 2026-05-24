#include <stdio.h>

#include <gui/gui.h>

#include "detailscreen.h"
#include "tapcounter.h"


void build_details(Mel_Gui_Handle frame, void* user)
{
    (void)user;

    mel_gui_set_text(frame, S8("Hello World Details"));

    mel_gui_set_layout(frame, mel_column_layout(
        .spacing = 8, .margin = 16, .cross_align = MEL_ALIGN_STRETCH));

    mel_label_create(frame, .text = S8("Details Screen"),
        .layoutable = { .preferred_w = 380, .preferred_h = 30 });
    mel_label_create(frame, .text = S8("Different surface, same melody build code."),
        .layoutable = { .preferred_h = 24 });

    tapcounter_create(frame, S8("Tap details-local handler"));

    Mel_Gui_Handle panel = mel_panel_create(frame,
        .layout = mel_column_layout(.spacing = 4, .margin = 8, .cross_align = MEL_ALIGN_STRETCH),
        .layoutable = { .preferred_h = 72 });
    mel_label_create(panel, .text = S8("A Panel groups children,"),
        .layoutable = { .preferred_h = 22 });
    mel_label_create(panel, .text = S8("carrying its own column layout."),
        .layoutable = { .preferred_h = 22 });

    Mel_Gui_Handle box = mel_groupbox_create(frame, .title = S8("A GroupBox"),
        .layout = mel_column_layout(.spacing = 4, .margin = 8, .cross_align = MEL_ALIGN_STRETCH),
        .layoutable = { .preferred_h = 100 });
    mel_label_create(box, .text = S8("Children parent to the content host."),
        .layoutable = { .preferred_h = 22 });
    mel_button_create(box, .text = S8("A button inside the box"),
        .layoutable = { .preferred_h = 34 });

    Mel_Gui_Handle scroll = mel_scrollview_create(frame,
        .content_h = 360,
        .layoutable = { .preferred_h = 120, .weight = 1 });
    for (i32 i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof buf, "Scrollable row %d", i + 1);
        mel_label_create(scroll, .text = str8_from_cstr(buf),
            .x = 8, .y = 8 + i * 34, .w = 320, .h = 28);
    }
}
