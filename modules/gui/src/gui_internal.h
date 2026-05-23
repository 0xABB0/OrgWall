#pragma once

#include <core/types.h>
#include <core/platform.h>
#include <collection.slotmap/slotmap.h>
#include <allocator/allocator.h>
#include <string/str8.h>
#include <reactor/reactor.h>

#include <gui/gui.h>

typedef struct Mel_Gui_Node {
    Mel_Gui_Handle self;
    Mel_Gui_Handle parent;
    void*          native;
    void*          user;
    u32            id;
    i32            x, y, width, height;
    bool           hidden;
    Mel_Layoutable layoutable;
    Mel_Layout*    layout;
} Mel_Gui_Node;

const Mel_Alloc* mel_gui__alloc(void);
Mel_Reactor*     mel_gui__reactor(void);

Mel_Gui_Node*  mel_gui__node(Mel_Gui_Handle h);
Mel_Gui_Node*  mel_gui__nodes(u32* count_out);

Mel_Gui_Handle mel_gui__node_new(Mel_Gui_Handle parent,
                                 i32 x, i32 y, i32 w, i32 h, u32 id, void* user,
                                 bool hidden,
                                 const Mel_Layoutable* layoutable, Mel_Layout* layout);
void           mel_gui__node_release(Mel_Gui_Handle h);

bool           mel_gui__is_toplevel(const Mel_Gui_Node* n);

void           mel_gui__destroy_tree(Mel_Gui_Handle root);

void           mel_gui__screens_reset(void);

void           mel_gui__set_focused(Mel_Gui_Handle h);

i32            mel_gui__frames_inc(void);
i32            mel_gui__frames_dec(void);

/* Layout drives the backend through the public mel_gui_set_bounds only. */
void           mel_gui__layout_free   (Mel_Layout* layout);
void           mel_gui__layout_measure(Mel_Gui_Handle h, i32 avail_w, i32 avail_h,
                                       i32* out_w, i32* out_h);
void           mel_gui__layout_arrange(Mel_Gui_Handle h);
void           mel_gui__push_bounds   (Mel_Gui_Handle h);

/* Native size change: update geometry + re-arrange. The user on_resize handler
 * is fired by the backend from the callbacks it stores on the native object. */
void           mel_gui__resized(Mel_Gui_Handle h, i32 w, i32 height);

/* The two hooks every backend implements. Everything else a backend exposes is
 * the public API itself (mel_<widget>_create_opt, mel_gui_set_*, ...), defined
 * directly in src/<backend>/. There is no generic create or op delegation. */
bool           mel_gui__backend_init(void);
void           mel_gui__backend_destroy(Mel_Gui_Node* n);
