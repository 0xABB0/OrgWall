#include "gui_internal.h"

#include <collection.array/array.h>

typedef struct {
    str8           name;
    Mel_Gui_Handle frame;
    void*          arg;
} Mel_Nav_Entry;

typedef struct {
    Mel_Array(Mel_Nav_Entry) entries;
} Mel_Navigator;

static Mel_Array(Mel_Navigator) g_navs;

void mel_gui__navs_reset(void)
{
    for (usize i = 0; i < g_navs.count; i++) mel_array_free(&g_navs.items[i].entries);
    mel_array_free(&g_navs);
}

static Mel_Navigator* nav_of(Mel_Gui_Handle from)
{
    Mel_Gui_Handle top = mel_gui__toplevel(from);
    if (mel_gui_handle_is_none(top)) return NULL;
    for (usize i = 0; i < g_navs.count; i++) {
        Mel_Navigator* nav = &g_navs.items[i];
        for (usize j = 0; j < nav->entries.count; j++) {
            if (mel_gui_handle_eq(nav->entries.items[j].frame, top)) return nav;
        }
    }
    return NULL;
}

/* A frame's native surface was torn down (OS close box, or any backend teardown).
 * Drop its entry; if it was the visible top, reveal the predecessor; if it was a
 * Navigator's last entry, drop the Navigator. Never destroys the frame — the
 * caller already did. The programmatic verbs remove their entry *before* calling
 * mel_gui_destroy, so this is a no-op for verb-initiated closes and does real work
 * only for OS-initiated ones. */
void mel_gui__frame_closed(Mel_Gui_Handle frame)
{
    if (mel_gui_handle_is_none(frame)) return;
    for (usize i = 0; i < g_navs.count; i++) {
        Mel_Navigator* nav = &g_navs.items[i];
        for (usize j = 0; j < nav->entries.count; j++) {
            if (!mel_gui_handle_eq(nav->entries.items[j].frame, frame)) continue;

            bool was_top = (j == nav->entries.count - 1);
            mel_array_remove_ordered(&nav->entries, j);

            if (nav->entries.count == 0) {
                mel_array_free(&nav->entries);
                mel_array_remove_ordered(&g_navs, i);
            } else if (was_top) {
                Mel_Gui_Handle top = mel_array_last(&nav->entries).frame;
                mel_gui_set_visible(top, true);
                mel_gui_set_focus(top);
            }
            return;
        }
    }
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

static Mel_Gui_Handle instantiate(str8 name, void* arg)
{
    const Mel_Screen_Def* def = mel_gui__screen_find(name);
    assert(def && "navigate: screen name not registered");
    if (!def) return MEL_GUI_HANDLE_NONE;

    Mel_Gui_Handle frame = mel_frame_create(.title = name);
    void*          user  = arg ? arg : def->default_user;
    if (def->build) def->build(frame, user);
    autosize_frame(frame);
    return frame;
}

void mel_app_present(str8 name, void* arg)
{
    /* Single-surface backends (phone, web) have no coequal Roots: every present
     * after the first degrades to a push on the one Root, an honest forward
     * navigation (MEL-ENGINE-VII). That Root is always g_navs[0] — degrades never
     * append a Navigator, so the single Root never moves. The first present still
     * creates the root below, on every backend. */
    if (g_navs.count > 0 && !mel_gui_supports_multi_root()) {
        Mel_Gui_Handle prev  = mel_array_last(&g_navs.items[0].entries).frame;
        Mel_Gui_Handle frame = instantiate(name, arg);
        if (mel_gui_handle_is_none(frame)) return;
        mel_array_push(&g_navs.items[0].entries, ((Mel_Nav_Entry){ name, frame, arg }));
        mel_gui__nav_replace(frame, prev);
        return;
    }

    Mel_Gui_Handle frame = instantiate(name, arg);
    if (mel_gui_handle_is_none(frame)) return;

    if (g_navs.allocator == NULL) mel_array_init(&g_navs, mel_gui__alloc());

    Mel_Navigator nav = {0};
    mel_array_init(&nav.entries, mel_gui__alloc());
    mel_array_push(&nav.entries, ((Mel_Nav_Entry){ name, frame, arg }));
    mel_array_push(&g_navs, nav);

    mel_gui__nav_replace(frame, MEL_GUI_HANDLE_NONE);
}

/* OS-initiated back where the platform does NOT itself tear down the surface
 * (Android hardware back, web back button): behave like mel_app_back on the
 * foreground Navigator — pop, reveal predecessor, destroy the popped frame. At
 * the root it is a no-op (the OS exits the app / leaves the page). Backends whose
 * OS already removed the surface (iOS pops the VC) use mel_gui__frame_closed
 * instead, which does not destroy. */
bool mel_gui__nav_os_back(void)
{
    if (g_navs.count == 0) return false;
    Mel_Navigator* nav = &g_navs.items[g_navs.count - 1];
    if (nav->entries.count <= 1) return false;

    Mel_Nav_Entry  top  = mel_array_pop(&nav->entries);
    Mel_Gui_Handle prev = mel_array_last(&nav->entries).frame;
    mel_gui__nav_back(prev, top.frame);
    mel_gui_destroy(top.frame);
    return true;
}

void mel_app_push(Mel_Gui_Handle from, str8 name, void* arg)
{
    Mel_Navigator* nav = nav_of(from);
    assert(nav && "push: from does not belong to a navigator");
    if (!nav) return;

    Mel_Gui_Handle prev  = mel_array_last(&nav->entries).frame;
    Mel_Gui_Handle frame = instantiate(name, arg);
    if (mel_gui_handle_is_none(frame)) return;

    nav = nav_of(from); /* the build callback may have grown g_navs (re-entrant present) */
    if (!nav) return;

    mel_array_push(&nav->entries, ((Mel_Nav_Entry){ name, frame, arg }));
    mel_gui__nav_replace(frame, prev);
}

void mel_app_replace(Mel_Gui_Handle from, str8 name, void* arg)
{
    Mel_Navigator* nav = nav_of(from);
    assert(nav && "replace: from does not belong to a navigator");
    if (!nav || nav->entries.count == 0) return;

    Mel_Nav_Entry  old   = mel_array_last(&nav->entries);
    Mel_Gui_Handle frame = instantiate(name, arg);
    if (mel_gui_handle_is_none(frame)) return;

    nav = nav_of(from); /* the build callback may have grown g_navs (re-entrant present) */
    if (!nav || nav->entries.count == 0) return;

    mel_array_last(&nav->entries) = (Mel_Nav_Entry){ name, frame, arg };
    mel_gui__nav_replace(frame, old.frame);
    mel_gui_destroy(old.frame);
}

void mel_app_back(Mel_Gui_Handle from)
{
    Mel_Navigator* nav = nav_of(from);
    assert(nav && "back: from does not belong to a navigator");
    if (!nav || nav->entries.count <= 1) return;

    Mel_Nav_Entry  top  = mel_array_pop(&nav->entries);
    Mel_Gui_Handle prev = mel_array_last(&nav->entries).frame;
    mel_gui__nav_back(prev, top.frame);
    mel_gui_destroy(top.frame);
}

void mel_app_pop_to(Mel_Gui_Handle from, str8 name)
{
    Mel_Navigator* nav = nav_of(from);
    if (!nav) return;

    while (nav->entries.count > 1 &&
           !str8_equals(mel_array_last(&nav->entries).name, name)) {
        Mel_Nav_Entry top = mel_array_pop(&nav->entries);
        mel_gui_destroy(top.frame);
    }

    Mel_Gui_Handle top = mel_array_last(&nav->entries).frame;
    mel_gui_set_visible(top, true);
    mel_gui_set_focus(top);
}

void mel_app_pop_to_root(Mel_Gui_Handle from)
{
    Mel_Navigator* nav = nav_of(from);
    if (!nav) return;

    while (nav->entries.count > 1) {
        Mel_Nav_Entry top = mel_array_pop(&nav->entries);
        mel_gui_destroy(top.frame);
    }

    Mel_Gui_Handle top = mel_array_last(&nav->entries).frame;
    mel_gui_set_visible(top, true);
    mel_gui_set_focus(top);
}
