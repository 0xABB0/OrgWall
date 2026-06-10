#pragma once

#include <core/types.h>
#include <core/platform.h>
#include <collection/slotmap.h>
#include <allocator/allocator.h>
#include <string/str8.h>
#include <vat/vat.h>

#include <gui/gui.h>

typedef struct Mel_Gui_Node
{
    Mel_Gui_Handle self;
    Mel_Gui_Handle parent;
    void*          native;
    void*          content;
    void*          user;
    u32            id;
    i32            x, y, width, height;
    bool           hidden;
    bool           is_screen;
    bool           is_scroll_host;
    i32            content_floor_w, content_floor_h;
    str8           screen_title;
    Mel_Layoutable layoutable;
    Mel_Layout*    layout;
} Mel_Gui_Node;

const Mel_Alloc* mel_gui__alloc(void);
Mel_Vat*         mel_gui__vat(void);

Mel_Gui_Node* mel_gui__node(Mel_Gui_Handle h);
Mel_Gui_Node* mel_gui__nodes(u32* count_out);

Mel_Gui_Handle mel_gui__node_new(Mel_Gui_Handle parent, i32 x, i32 y, i32 w, i32 h, u32 id, void* user, bool hidden, const Mel_Layoutable* layoutable, Mel_Layout* layout);
void           mel_gui__node_release(Mel_Gui_Handle h);

bool           mel_gui__is_toplevel(const Mel_Gui_Node* n);
Mel_Gui_Handle mel_gui__toplevel(Mel_Gui_Handle h);

void mel_gui__destroy_tree(Mel_Gui_Handle root);

/* Create a screen-root container (a child of the Root window) into which a
 * screen builds its widgets. A minimal pass-through view, not an input-handling
 * panel. Backend-provided. */
Mel_Gui_Handle mel_gui__screen_new(Mel_Gui_Handle window);

/* Screen registry (identity): name -> builder + register-time default user.
 * Holds no live navigation state; instances live on a Navigator (nav.c). */
typedef struct
{
    str8             name;
    Mel_Screen_Build build;
    void*            default_user;
    void (*on_enter)(Mel_Gui_Handle, void*);
    void (*on_leave)(Mel_Gui_Handle, void*);
    void (*on_destroy)(Mel_Gui_Handle, void*);
} Mel_Screen_Def;

const Mel_Screen_Def* mel_gui__screen_find(str8 name);

void mel_gui__screens_reset(void);
void mel_gui__navs_reset(void);

/* A Root window was torn down by the OS (close box). Tears down the whole
 * Navigator that owns it: fires leave on the visible top, destroy on every
 * entry, drops the Navigator. Does NOT release gui nodes — the caller's
 * destroy_tree does that while handles are still valid. */
void mel_gui__frame_closed(Mel_Gui_Handle window);

/* OS back where the toolkit does not itself remove the surface (Android hardware
 * back, web history back): pop+reveal+destroy the foreground Navigator's top.
 * Returns true if it popped, false at the root (so the OS can exit the app). */
bool mel_gui__nav_os_back(void);

/* A backend that owns its navigation stack (iOS) reports a user-driven pop
 * (swipe-back, system back button) of `screen` here: drop the entry and reveal
 * the one beneath WITHOUT popping the backend again. No-ops if `screen` is no
 * longer the top (a programmatic back already dropped it). */
void mel_gui__screen_popped(Mel_Gui_Handle screen);

/* Natural content extent of a screen (constant-free measurement). */
void mel_gui__content_size(Mel_Gui_Handle frame, i32* out_w, i32* out_h);

/* Screen-to-screen transitions within the one Root window: show/hide the screen
 * content subtree.
 *  - nav_replace: make `next` the active screen in place of `prev`.
 *  - nav_back:    return from `cur` to the earlier `prev`.
 * Handles may be MEL_GUI_HANDLE_NONE when there is no counterpart. */
void mel_gui__nav_replace(Mel_Gui_Handle next, Mel_Gui_Handle prev);
void mel_gui__nav_back(Mel_Gui_Handle prev, Mel_Gui_Handle cur);

/* The Root window resized: refit and relayout its active screen subtree. No-op
 * when the window is not navigator-managed (a directly-created frame). */
void mel_gui__nav_window_resized(Mel_Gui_Handle window, i32 w, i32 h);

void mel_gui__set_focused(Mel_Gui_Handle h);

i32 mel_gui__frames_inc(void);
i32 mel_gui__frames_dec(void);

/* Layout drives the backend through the public mel_gui_set_bounds only. */
void mel_gui__layout_free(Mel_Layout* layout);
void mel_gui__layout_measure(Mel_Gui_Handle h, i32 avail_w, i32 avail_h, i32* out_w, i32* out_h);
void mel_gui__layout_arrange(Mel_Gui_Handle h);
void mel_gui__push_bounds(Mel_Gui_Handle h);

/* A scroll host sizes its scrollable surface from its content, not a constant:
 * measure the subtree (layout-measured or the bounding box of absolute children),
 * clamp up to the viewport and the optional author floor, hand the extent to the
 * backend, then arrange any layout against that extent rather than the viewport. */
void mel_gui__scroll_fit(Mel_Gui_Handle h);

/* Resize a scroll host's scrollable surface (documentView / contentSize / inner
 * container) to (w, h) in layout units. Backend-provided; only scroll hosts call it. */
void mel_gui__backend_set_content_size(Mel_Gui_Node* n, i32 w, i32 h);

/* Native size change: update geometry + re-arrange. The user on_resize handler
 * is fired by the backend from the callbacks it stores on the native object. */
void mel_gui__resized(Mel_Gui_Handle h, i32 w, i32 height);

/* The two hooks every backend implements. Everything else a backend exposes is
 * the public API itself (mel_<widget>_create_opt, mel_gui_set_*, ...), defined
 * directly in src/<backend>/. There is no generic create or op delegation. */
bool mel_gui__backend_init(void);
void mel_gui__backend_destroy(Mel_Gui_Node* n);
