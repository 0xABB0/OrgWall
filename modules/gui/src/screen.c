#include "gui_internal.h"

#define MEL_GUI_MAX_SCREENS 32

typedef struct {
    str8             name;
    Mel_Screen_Build build;
    void*            user;
    Mel_Gui_Handle   frame;
    bool             created;
} Mel_Gui_Screen;

static Mel_Gui_Screen g_screens[MEL_GUI_MAX_SCREENS];
static u32            g_screen_count;

void mel_gui__screens_reset(void)
{
    for (u32 i = 0; i < g_screen_count; i++) g_screens[i] = (Mel_Gui_Screen){0};
    g_screen_count = 0;
}

void mel_app_register_screen(str8 name, Mel_Screen_Build build, void* user)
{
    if (g_screen_count >= MEL_GUI_MAX_SCREENS || !build) return;
    Mel_Gui_Screen* s = &g_screens[g_screen_count++];
    s->name    = name;
    s->build   = build;
    s->user    = user;
    s->frame   = MEL_GUI_HANDLE_NONE;
    s->created = false;
}

static Mel_Gui_Screen* find_screen(str8 name)
{
    for (u32 i = 0; i < g_screen_count; i++) {
        if (str8_equals(g_screens[i].name, name)) return &g_screens[i];
    }
    return NULL;
}

static void autosize_frame(Mel_Gui_Handle frame)
{
    Mel_Gui_Widget* fw = mel_gui__get(frame);
    if (!fw) return;

    i32 cw, ch;

    if (fw->layout) {
        mel_gui__layout_measure(frame, 0, 0, &cw, &ch);
    } else {
        u32 count = 0;
        Mel_Gui_Widget* data = mel_gui__widgets(&count);

        i32 max_x = 0;
        i32 max_y = 0;
        for (u32 i = 0; i < count; i++) {
            Mel_Gui_Widget* w = &data[i];
            if (!mel_gui_handle_eq(w->parent, frame)) continue;
            i32 rx = w->x + w->width;
            i32 ry = w->y + w->height;
            if (rx > max_x) max_x = rx;
            if (ry > max_y) max_y = ry;
        }

        i32 margin = 24;
        cw = max_x + margin;
        ch = max_y + margin;
    }

    if (cw < 320) cw = 320;
    if (ch < 240) ch = 240;

    mel_gui_set_bounds(frame, fw->x, fw->y, cw, ch);

    if (fw->layout) {
        Mel_Gui_Widget* refreshed = mel_gui__get(frame);
        if (refreshed) {
            refreshed->width  = cw;
            refreshed->height = ch;
        }
        mel_gui__layout_arrange(frame);
    }
}

void mel_app_present(str8 name)
{
    Mel_Gui_Screen* s = find_screen(name);
    if (!s) return;

    if (!s->created) {
        s->frame   = mel_frame_create(.title = name);
        s->created = true;
        if (s->build) s->build(s->frame, s->user);
        autosize_frame(s->frame);
    }

    mel_gui_set_visible(s->frame, true);
    mel_gui_set_focus(s->frame);
}
