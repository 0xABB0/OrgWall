#include "gui_internal.h"

#include <collection.array/array.h>

typedef struct
{
    str8           name;
    Mel_Gui_Handle screen;
    void*          arg;
} Mel_Nav_Entry;

typedef struct
{
    Mel_Gui_Handle window;
    Mel_Array(Mel_Nav_Entry) entries;
} Mel_Navigator;

static Mel_Array(Mel_Navigator) g_navs;

void mel_gui__navs_reset(void)
{
    for (usize i = 0; i < g_navs.count; i++)
        mel_array_free(&g_navs.items[i].entries);
    mel_array_free(&g_navs);
}

static Mel_Navigator* nav_by_window(Mel_Gui_Handle window)
{
    for (usize i = 0; i < g_navs.count; i++)
    {
        if (mel_gui_handle_eq(g_navs.items[i].window, window))
            return &g_navs.items[i];
    }
    return NULL;
}

static Mel_Navigator* nav_of(Mel_Gui_Handle from)
{
    Mel_Gui_Handle top = mel_gui__toplevel(from);
    if (mel_gui_handle_is_none(top))
        return NULL;
    return nav_by_window(top);
}

static void fire_enter(Mel_Nav_Entry e)
{
    const Mel_Screen_Def* d = mel_gui__screen_find(e.name);
    if (d && d->on_enter)
        d->on_enter(e.screen, e.arg ? e.arg : d->default_user);
}
static void fire_leave(Mel_Nav_Entry e)
{
    const Mel_Screen_Def* d = mel_gui__screen_find(e.name);
    if (d && d->on_leave)
        d->on_leave(e.screen, e.arg ? e.arg : d->default_user);
}
static void fire_destroy(Mel_Nav_Entry e)
{
    const Mel_Screen_Def* d = mel_gui__screen_find(e.name);
    if (d && d->on_destroy)
        d->on_destroy(e.screen, e.arg ? e.arg : d->default_user);
}

static Mel_Gui_Handle build_screen(Mel_Gui_Handle window, str8 name, void* arg)
{
    const Mel_Screen_Def* def = mel_gui__screen_find(name);
    assert(def && "navigate: screen name not registered");
    if (!def)
        return MEL_GUI_HANDLE_NONE;

    Mel_Gui_Handle screen = mel_gui__screen_new(window);

    void* user = arg ? arg : def->default_user;
    if (def->build)
        def->build(screen, user);
    return screen;
}

static void fit_screen(Mel_Gui_Handle window, Mel_Gui_Handle screen)
{
    Mel_Gui_Node* wn = mel_gui__node(window);
    if (!wn || wn->width <= 0 || wn->height <= 0)
        return;
    mel_gui_set_bounds(screen, 0, 0, wn->width, wn->height);
    mel_gui_relayout(screen);
}

static void reveal_title(Mel_Gui_Handle window, Mel_Gui_Handle screen)
{
    Mel_Gui_Node* sn = mel_gui__node(screen);
    if (sn && sn->screen_title.len > 0)
        mel_gui_set_text(window, sn->screen_title);
}

static void size_window_to_screen(Mel_Gui_Handle window, Mel_Gui_Handle screen)
{
    i32 cw = 0, ch = 0;
    mel_gui__content_size(screen, &cw, &ch);
    if (cw < 320)
        cw = 320;
    if (ch < 240)
        ch = 240;

    Mel_Gui_Node* wn = mel_gui__node(window);
    i32           x = wn ? wn->x : 0;
    i32           y = wn ? wn->y : 0;
    mel_gui_set_bounds(window, x, y, cw, ch);
    mel_gui_set_bounds(screen, 0, 0, cw, ch);
    mel_gui_relayout(screen);
}

static void destroy_screen(Mel_Gui_Handle screen) { mel_gui__destroy_tree(screen); }

void mel_gui__frame_closed(Mel_Gui_Handle window)
{
    Mel_Navigator* nav = nav_by_window(window);
    if (!nav)
        return;
    usize idx = (usize)(nav - g_navs.items);

    if (nav->entries.count > 0)
        fire_leave(mel_array_last(&nav->entries));
    for (usize j = nav->entries.count; j-- > 0;)
        fire_destroy(nav->entries.items[j]);

    mel_array_free(&nav->entries);
    mel_array_remove_ordered(&g_navs, idx);
}

void mel_app_present(str8 name, void* arg)
{
    /* Single-surface backends (phone, web) have no coequal Roots: present after
     * the first degrades to a push on the one Root — under Model B literally a
     * content swap (MEL-ENGINE-VII). That Root is always g_navs[0]. */
    if (g_navs.count > 0 && !mel_gui_supports_multi_root())
    {
        Mel_Gui_Handle window = g_navs.items[0].window;
        Mel_Nav_Entry  left = mel_array_last(&g_navs.items[0].entries);
        Mel_Gui_Handle screen = build_screen(window, name, arg);
        if (mel_gui_handle_is_none(screen))
            return;

        fit_screen(window, screen);
        Mel_Nav_Entry entered = { name, screen, arg };
        mel_array_push(&g_navs.items[0].entries, entered);
        mel_gui__nav_replace(screen, left.screen);
        reveal_title(window, screen);
        fire_leave(left);
        fire_enter(entered);
        return;
    }

    Mel_Gui_Handle window = mel_frame_create(.title = name);
    Mel_Gui_Handle screen = build_screen(window, name, arg);
    if (mel_gui_handle_is_none(screen))
        return;

    size_window_to_screen(window, screen);

    if (g_navs.allocator == NULL)
        mel_array_init(&g_navs, mel_gui__alloc());

    Mel_Navigator nav = { 0 };
    nav.window = window;
    mel_array_init(&nav.entries, mel_gui__alloc());
    Mel_Nav_Entry entered = { name, screen, arg };
    mel_array_push(&nav.entries, entered);
    mel_array_push(&g_navs, nav);

    reveal_title(window, screen);
    mel_gui_set_visible(window, true);
    mel_gui__nav_replace(screen, MEL_GUI_HANDLE_NONE);
    mel_gui_set_focus(window);
    fire_enter(entered);
}

void mel_app_push(Mel_Gui_Handle from, str8 name, void* arg)
{
    Mel_Navigator* nav = nav_of(from);
    assert(nav && "push: from does not belong to a navigator");
    if (!nav)
        return;

    Mel_Gui_Handle window = nav->window;
    Mel_Nav_Entry  left = mel_array_last(&nav->entries);
    Mel_Gui_Handle screen = build_screen(window, name, arg);
    if (mel_gui_handle_is_none(screen))
        return;

    nav = nav_of(from); /* the build callback may have grown g_navs (re-entrant present) */
    if (!nav)
        return;

    fit_screen(window, screen);
    Mel_Nav_Entry entered = { name, screen, arg };
    mel_array_push(&nav->entries, entered);
    mel_gui__nav_replace(screen, left.screen);
    reveal_title(window, screen);
    fire_leave(left);
    fire_enter(entered);
}

void mel_app_replace(Mel_Gui_Handle from, str8 name, void* arg)
{
    Mel_Navigator* nav = nav_of(from);
    assert(nav && "replace: from does not belong to a navigator");
    if (!nav || nav->entries.count == 0)
        return;

    Mel_Gui_Handle window = nav->window;
    Mel_Nav_Entry  old = mel_array_last(&nav->entries);
    Mel_Gui_Handle screen = build_screen(window, name, arg);
    if (mel_gui_handle_is_none(screen))
        return;

    nav = nav_of(from); /* the build callback may have grown g_navs (re-entrant present) */
    if (!nav || nav->entries.count == 0)
        return;

    fit_screen(window, screen);
    Mel_Nav_Entry entered = { name, screen, arg };
    mel_array_last(&nav->entries) = entered;
    mel_gui__nav_replace(screen, old.screen);
    reveal_title(window, screen);
    fire_leave(old);
    fire_destroy(old);
    destroy_screen(old.screen);
    fire_enter(entered);
}

void mel_app_back(Mel_Gui_Handle from)
{
    Mel_Navigator* nav = nav_of(from);
    assert(nav && "back: from does not belong to a navigator");
    if (!nav || nav->entries.count <= 1)
        return;

    Mel_Gui_Handle window = nav->window;
    Mel_Nav_Entry  top = mel_array_pop(&nav->entries);
    Mel_Nav_Entry  prev = mel_array_last(&nav->entries);
    fit_screen(window, prev.screen);
    mel_gui__nav_back(prev.screen, top.screen);
    reveal_title(window, prev.screen);
    fire_leave(top);
    fire_destroy(top);
    destroy_screen(top.screen);
    fire_enter(prev);
}

void mel_app_pop_to(Mel_Gui_Handle from, str8 name)
{
    Mel_Navigator* nav = nav_of(from);
    if (!nav)
        return;

    Mel_Gui_Handle window = nav->window;
    bool           visible_left = false;
    while (nav->entries.count > 1 && !str8_equals(mel_array_last(&nav->entries).name, name))
    {
        Mel_Nav_Entry top = mel_array_pop(&nav->entries);
        if (!visible_left)
        {
            fire_leave(top);
            visible_left = true;
        }
        fire_destroy(top);
        destroy_screen(top.screen);
    }

    Mel_Nav_Entry top = mel_array_last(&nav->entries);
    fit_screen(window, top.screen);
    mel_gui_set_visible(top.screen, true);
    mel_gui_set_focus(top.screen);
    reveal_title(window, top.screen);
    if (visible_left)
        fire_enter(top);
}

void mel_app_pop_to_root(Mel_Gui_Handle from)
{
    Mel_Navigator* nav = nav_of(from);
    if (!nav)
        return;

    Mel_Gui_Handle window = nav->window;
    bool           visible_left = false;
    while (nav->entries.count > 1)
    {
        Mel_Nav_Entry top = mel_array_pop(&nav->entries);
        if (!visible_left)
        {
            fire_leave(top);
            visible_left = true;
        }
        fire_destroy(top);
        destroy_screen(top.screen);
    }

    Mel_Nav_Entry top = mel_array_last(&nav->entries);
    fit_screen(window, top.screen);
    mel_gui_set_visible(top.screen, true);
    mel_gui_set_focus(top.screen);
    reveal_title(window, top.screen);
    if (visible_left)
        fire_enter(top);
}

bool mel_gui__nav_os_back(void)
{
    if (g_navs.count == 0)
        return false;
    Mel_Navigator* nav = &g_navs.items[g_navs.count - 1];
    if (nav->entries.count <= 1)
        return false;

    Mel_Gui_Handle window = nav->window;
    Mel_Nav_Entry  top = mel_array_pop(&nav->entries);
    Mel_Nav_Entry  prev = mel_array_last(&nav->entries);
    fit_screen(window, prev.screen);
    mel_gui__nav_back(prev.screen, top.screen);
    reveal_title(window, prev.screen);
    fire_leave(top);
    fire_destroy(top);
    destroy_screen(top.screen);
    fire_enter(prev);
    return true;
}

/* A backend that owns its own navigation stack (iOS UINavigationController)
 * reports user-driven pops — swipe-back, the system back button — here. The OS
 * has already removed the surface, so unlike mel_gui__nav_os_back we must NOT
 * pop the backend again; we only drop the entry and reveal the one beneath. A
 * programmatic back has already removed the entry, so this then no-ops. */
void mel_gui__screen_popped(Mel_Gui_Handle screen)
{
    for (usize i = 0; i < g_navs.count; i++)
    {
        Mel_Navigator* nav = &g_navs.items[i];
        if (nav->entries.count <= 1)
            continue;
        if (!mel_gui_handle_eq(mel_array_last(&nav->entries).screen, screen))
            continue;

        Mel_Gui_Handle window = nav->window;
        Mel_Nav_Entry  top = mel_array_pop(&nav->entries);
        Mel_Nav_Entry  prev = mel_array_last(&nav->entries);
        fit_screen(window, prev.screen);
        reveal_title(window, prev.screen);
        fire_leave(top);
        fire_destroy(top);
        destroy_screen(top.screen);
        fire_enter(prev);
        return;
    }
}

void mel_gui__nav_window_resized(Mel_Gui_Handle window, i32 w, i32 h)
{
    Mel_Navigator* nav = nav_by_window(window);
    if (!nav || nav->entries.count == 0)
        return;
    Mel_Gui_Handle screen = mel_array_last(&nav->entries).screen;
    mel_gui_set_bounds(screen, 0, 0, w, h);
    mel_gui_relayout(screen);
}
