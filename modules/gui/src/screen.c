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

static Mel_Gui_Screen* g_current;
static Mel_Gui_Screen* g_history[MEL_GUI_MAX_SCREENS];
static u32             g_history_count;

void mel_gui__screens_reset(void)
{
    for (u32 i = 0; i < g_screen_count; i++) g_screens[i] = (Mel_Gui_Screen){0};
    g_screen_count  = 0;
    g_current       = NULL;
    g_history_count = 0;
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
    Mel_Gui_Node* fw = mel_gui__node(frame);
    if (!fw) return;

    i32 cw, ch;

    if (fw->layout) {
        mel_gui__layout_measure(frame, 0, 0, &cw, &ch);
    } else {
        u32 count = 0;
        Mel_Gui_Node* data = mel_gui__nodes(&count);

        i32 max_x = 0;
        i32 max_y = 0;
        for (u32 i = 0; i < count; i++) {
            Mel_Gui_Node* n = &data[i];
            if (!mel_gui_handle_eq(n->parent, frame)) continue;
            i32 rx = n->x + n->width;
            i32 ry = n->y + n->height;
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
        Mel_Gui_Node* refreshed = mel_gui__node(frame);
        if (refreshed) {
            refreshed->width  = cw;
            refreshed->height = ch;
        }
        mel_gui__layout_arrange(frame);
    }
}

static void ensure_created(Mel_Gui_Screen* s)
{
    if (s->created) return;
    s->frame   = mel_frame_create(.title = s->name);
    s->created = true;
    if (s->build) s->build(s->frame, s->user);
    autosize_frame(s->frame);
}

void mel_app_present(str8 name)
{
    Mel_Gui_Screen* s = find_screen(name);
    if (!s) return;

    ensure_created(s);
    mel_gui_set_visible(s->frame, true);
    mel_gui_set_focus(s->frame);

    /* present is the additive op: on desktop each call is its own top-level
     * window. Only the first one establishes the surface that replace/back
     * navigate; later presents must not hijack it. */
    if (!g_current) g_current = s;
}

void mel_app_replace(str8 name)
{
    Mel_Gui_Screen* s = find_screen(name);
    if (!s || s == g_current) return;

    Mel_Gui_Screen* prev = g_current;
    ensure_created(s);

    mel_gui__nav_replace(s->frame, prev ? prev->frame : MEL_GUI_HANDLE_NONE);

    if (prev && g_history_count < MEL_GUI_MAX_SCREENS) g_history[g_history_count++] = prev;
    g_current = s;
}

void mel_app_back(void)
{
    if (g_history_count == 0) return;
    Mel_Gui_Screen* prev = g_history[--g_history_count];
    Mel_Gui_Screen* cur  = g_current;

    mel_gui__nav_back(prev->frame, cur ? cur->frame : MEL_GUI_HANDLE_NONE);
    g_current = prev;
}
