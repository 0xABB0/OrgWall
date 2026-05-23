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
}
