#include <stdio.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <gui/gui.h>

#include "tapcounter.h"

typedef struct {
    Mel_Gui_Handle count_label;
    i32            count;
} Tap_Counter;

static void tapcounter_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    Tap_Counter* tc = (Tap_Counter*)user;
    tc->count += 1;

    char buf[32];
    snprintf(buf, sizeof buf, "Taps: %d", tc->count);
    mel_gui_set_text(tc->count_label, str8_from_cstr(buf));
}

void tapcounter_create(Mel_Gui_Handle parent, str8 button_text)
{
    Tap_Counter* tc = mel_alloc_type(mel_alloc_heap(), Tap_Counter);
    if (!tc) return;
    tc->count = 0;

    mel_button_create(parent, .text = button_text,
        .pointer.on_click = tapcounter_clicked,
        .user = tc,
        .layoutable = { .preferred_h = 40 });

    tc->count_label = mel_label_create(parent, .text = S8("Taps: 0"),
        .layoutable = { .preferred_h = 24 });
}
