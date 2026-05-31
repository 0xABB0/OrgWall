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

/* Screen lifecycle. on_enter fires when an instance becomes the visible top
 * (first show or re-reveal); on_leave when it stops being top; on_destroy just
 * before its frame is torn down. The arg matches what the builder received. */
static void fire_enter(Mel_Nav_Entry e)
{
    const Mel_Screen_Def* d = mel_gui__screen_find(e.name);
    if (d && d->on_enter) d->on_enter(e.frame, e.arg ? e.arg : d->default_user);
}
static void fire_leave(Mel_Nav_Entry e)
{
    const Mel_Screen_Def* d = mel_gui__screen_find(e.name);
    if (d && d->on_leave) d->on_leave(e.frame, e.arg ? e.arg : d->default_user);
}
static void fire_destroy(Mel_Nav_Entry e)
{
    const Mel_Screen_Def* d = mel_gui__screen_find(e.name);
    if (d && d->on_destroy) d->on_destroy(e.frame, e.arg ? e.arg : d->default_user);
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

            bool          was_top = (j == nav->entries.count - 1);
            Mel_Nav_Entry closed  = nav->entries.items[j];
            mel_array_remove_ordered(&nav->entries, j);

            if (was_top) fire_leave(closed);
            fire_destroy(closed);

            if (nav->entries.count == 0) {
                mel_array_free(&nav->entries);
                mel_array_remove_ordered(&g_navs, i);
            } else if (was_top) {
                Mel_Nav_Entry top = mel_array_last(&nav->entries);
                mel_gui_set_visible(top.frame, true);
                mel_gui_set_focus(top.frame);
                fire_enter(top);
            }
            return;
        }
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
    mel_gui__present_root(frame);
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
        Mel_Nav_Entry  left  = mel_array_last(&g_navs.items[0].entries);
        Mel_Gui_Handle frame = instantiate(name, arg);
        if (mel_gui_handle_is_none(frame)) return;
        Mel_Nav_Entry entered = { name, frame, arg };
        mel_array_push(&g_navs.items[0].entries, entered);
        mel_gui__nav_replace(frame, left.frame);
        fire_leave(left);
        fire_enter(entered);
        return;
    }

    Mel_Gui_Handle frame = instantiate(name, arg);
    if (mel_gui_handle_is_none(frame)) return;

    if (g_navs.allocator == NULL) mel_array_init(&g_navs, mel_gui__alloc());

    Mel_Nav_Entry entered = { name, frame, arg };
    Mel_Navigator nav = {0};
    mel_array_init(&nav.entries, mel_gui__alloc());
    mel_array_push(&nav.entries, entered);
    mel_array_push(&g_navs, nav);

    mel_gui__nav_replace(frame, MEL_GUI_HANDLE_NONE);
    fire_enter(entered);
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

    Mel_Nav_Entry top  = mel_array_pop(&nav->entries);
    Mel_Nav_Entry prev = mel_array_last(&nav->entries);
    mel_gui__nav_back(prev.frame, top.frame);
    fire_leave(top);
    fire_destroy(top);
    mel_gui_destroy(top.frame);
    fire_enter(prev);
    return true;
}

void mel_app_push(Mel_Gui_Handle from, str8 name, void* arg)
{
    Mel_Navigator* nav = nav_of(from);
    assert(nav && "push: from does not belong to a navigator");
    if (!nav) return;

    Mel_Nav_Entry  left  = mel_array_last(&nav->entries);
    Mel_Gui_Handle frame = instantiate(name, arg);
    if (mel_gui_handle_is_none(frame)) return;

    nav = nav_of(from); /* the build callback may have grown g_navs (re-entrant present) */
    if (!nav) return;

    Mel_Nav_Entry entered = { name, frame, arg };
    mel_array_push(&nav->entries, entered);
    mel_gui__nav_replace(frame, left.frame);
    fire_leave(left);
    fire_enter(entered);
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

    Mel_Nav_Entry entered = { name, frame, arg };
    mel_array_last(&nav->entries) = entered;
    mel_gui__nav_replace(frame, old.frame);
    fire_leave(old);
    fire_destroy(old);
    mel_gui_destroy(old.frame);
    fire_enter(entered);
}

void mel_app_back(Mel_Gui_Handle from)
{
    Mel_Navigator* nav = nav_of(from);
    assert(nav && "back: from does not belong to a navigator");
    if (!nav || nav->entries.count <= 1) return;

    Mel_Nav_Entry top  = mel_array_pop(&nav->entries);
    Mel_Nav_Entry prev = mel_array_last(&nav->entries);
    mel_gui__nav_back(prev.frame, top.frame);
    fire_leave(top);
    fire_destroy(top);
    mel_gui_destroy(top.frame);
    fire_enter(prev);
}

void mel_app_pop_to(Mel_Gui_Handle from, str8 name)
{
    Mel_Navigator* nav = nav_of(from);
    if (!nav) return;

    bool visible_left = false;
    while (nav->entries.count > 1 &&
           !str8_equals(mel_array_last(&nav->entries).name, name)) {
        Mel_Nav_Entry top = mel_array_pop(&nav->entries);
        if (!visible_left) { fire_leave(top); visible_left = true; }
        fire_destroy(top);
        mel_gui_destroy(top.frame);
    }

    Mel_Nav_Entry top = mel_array_last(&nav->entries);
    mel_gui_set_visible(top.frame, true);
    mel_gui_set_focus(top.frame);
    if (visible_left) fire_enter(top);
}

void mel_app_pop_to_root(Mel_Gui_Handle from)
{
    Mel_Navigator* nav = nav_of(from);
    if (!nav) return;

    bool visible_left = false;
    while (nav->entries.count > 1) {
        Mel_Nav_Entry top = mel_array_pop(&nav->entries);
        if (!visible_left) { fire_leave(top); visible_left = true; }
        fire_destroy(top);
        mel_gui_destroy(top.frame);
    }

    Mel_Nav_Entry top = mel_array_last(&nav->entries);
    mel_gui_set_visible(top.frame, true);
    mel_gui_set_focus(top.frame);
    if (visible_left) fire_enter(top);
}
