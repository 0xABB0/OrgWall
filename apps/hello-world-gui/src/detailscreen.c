#include <stdio.h>
#include <string.h>

#include <core/platform.h>
#include <app/app.h>
#include <gui/gui.h>

#if MEL_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif MEL_PLATFORM_OSX
#include <CoreGraphics/CoreGraphics.h>
#endif


typedef struct {
    Mel_Gui_Handle frame;
    Mel_Gui_Handle status;
    i32  taps;
    bool focused;
} Details_State;

static Details_State g_details;

static void update_details_status(void)
{
    char text[192];
    snprintf(text, sizeof text,
        "Details screen keeps its own C state: taps=%d, focus=%s",
        g_details.taps,
        g_details.focused ? "yes" : "no");
    mel_gui_set_text(g_details.status, str8_from_cstr(text));
}

static void details_focus_in(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    g_details.focused = true;
    update_details_status();
}

static void details_focus_out(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    if (mel_gui_handle_is_none(mel_gui_focused())) {
        g_details.focused = false;
        update_details_status();
    }
}

static void details_button_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    g_details.taps += 1;
    update_details_status();
}

void build_details(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    memset(&g_details, 0, sizeof g_details);
    g_details.frame = frame;

    mel_gui_set_text(frame, S8("Hello World Details"));

    mel_label_create(frame, .text = S8("Details Screen"),
        .x = 24, .y = 20, .w = 380, .h = 30);
    mel_label_create(frame, .text = S8("Different surface, same melody build code."),
        .x = 24, .y = 56, .w = 380, .h = 24);

    mel_button_create(frame, .text = S8("Tap details-local handler"),
        .x = 24, .y = 96, .w = 280, .h = 40,
        .pointer.on_click   = details_button_clicked,
        .focus.on_focus_in  = details_focus_in,
        .focus.on_focus_out = details_focus_out,
        .user = &g_details);

    g_details.status = mel_label_create(frame, .text = S8(""),
        .x = 24, .y = 148, .w = 380, .h = 56);

    update_details_status();
}
