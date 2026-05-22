#pragma once

#include <core/types.h>
#include <core/platform.h>
#include <collection.slotmap/slotmap.h>
#include <allocator/allocator.h>
#include <string/str8.h>
#include <reactor/reactor.h>

#include <gui/gui.h>

typedef struct {
    Mel_Gui_Lifecycle_Cb lifecycle;
    Mel_Gui_Focus_Cb     focus;
    Mel_Gui_Pointer_Cb   pointer;
    Mel_Gui_Keyboard_Cb  keyboard;
} Mel_Gui_Callbacks;

typedef struct Mel_Gui_Widget {
    Mel_Gui_Handle     self;
    Mel_Gui_Handle     parent;
    void*              native;
    void*              user;
    void*              impl;
    u32                id;
    i32                x, y, width, height;
    bool               disabled;
    bool               hidden;
    Mel_Gui_Callbacks* cb;
} Mel_Gui_Widget;

typedef struct { Mel_CheckBox_On  on_; bool initial_checked; }            Mel_Gui_CheckBox_Impl;
typedef struct { Mel_TextField_On on_; }                                 Mel_Gui_TextField_Impl;
typedef struct { Mel_Slider_On    on_; i32 min_value, max_value, value; } Mel_Gui_Slider_Impl;
typedef struct { Mel_Canvas_On    on_; }                                 Mel_Gui_Canvas_Impl;

const Mel_Alloc* mel_gui__alloc(void);
Mel_Reactor*     mel_gui__reactor(void);

Mel_Gui_Widget* mel_gui__get(Mel_Gui_Handle h);

Mel_Gui_Handle  mel_gui__create(Mel_Gui_Handle parent,
                                i32 x, i32 y, i32 w, i32 h, u32 id, void* user,
                                bool disabled, bool hidden, usize impl_size,
                                const Mel_Gui_Lifecycle_Cb* lc,
                                const Mel_Gui_Focus_Cb* fc,
                                const Mel_Gui_Pointer_Cb* pc,
                                const Mel_Gui_Keyboard_Cb* kc);

void mel_gui__destroy_tree(Mel_Gui_Handle root);

Mel_Gui_Widget* mel_gui__widgets(u32* count_out);

bool mel_gui__is_toplevel(const Mel_Gui_Widget* w);

void mel_gui__set_focused(Mel_Gui_Handle h);

i32  mel_gui__frames_inc(void);
i32  mel_gui__frames_dec(void);

Mel_Gui_Callbacks* mel_gui__ensure_cb(Mel_Gui_Widget* w);

void mel_gui__fire_focus_in    (Mel_Gui_Handle h);
void mel_gui__fire_focus_out   (Mel_Gui_Handle h);
void mel_gui__fire_click       (Mel_Gui_Handle h);
void mel_gui__fire_key_down    (Mel_Gui_Handle h, Mel_Key key);
void mel_gui__fire_key_up      (Mel_Gui_Handle h, Mel_Key key);
void mel_gui__fire_char        (Mel_Gui_Handle h, u32 codepoint);
void mel_gui__fire_pointer_down(Mel_Gui_Handle h, i32 x, i32 y);
void mel_gui__fire_pointer_up  (Mel_Gui_Handle h, i32 x, i32 y);
void mel_gui__fire_pointer_move(Mel_Gui_Handle h, i32 x, i32 y);
void mel_gui__fire_resize      (Mel_Gui_Handle h, i32 w, i32 height);

bool mel_gui__backend_init(void);

void mel_gui__backend_frame_create    (Mel_Gui_Widget* w, str8 title);
void mel_gui__backend_label_create    (Mel_Gui_Widget* w, str8 text);
void mel_gui__backend_button_create   (Mel_Gui_Widget* w, str8 text);
void mel_gui__backend_checkbox_create (Mel_Gui_Widget* w, str8 text);
void mel_gui__backend_textfield_create(Mel_Gui_Widget* w, str8 text);
void mel_gui__backend_slider_create   (Mel_Gui_Widget* w, str8 text);
void mel_gui__backend_canvas_create   (Mel_Gui_Widget* w, str8 text);

void mel_gui__backend_destroy    (Mel_Gui_Widget* w);
void mel_gui__backend_set_text   (Mel_Gui_Widget* w, str8 text);
size mel_gui__backend_get_text   (Mel_Gui_Widget* w, char* buf, size cap);
void mel_gui__backend_set_bounds (Mel_Gui_Widget* w, i32 x, i32 y, i32 width, i32 height);
void mel_gui__backend_set_visible(Mel_Gui_Widget* w, bool visible);
void mel_gui__backend_set_enabled(Mel_Gui_Widget* w, bool enabled);
void mel_gui__backend_set_focus  (Mel_Gui_Widget* w);
void mel_gui__backend_invalidate (Mel_Gui_Widget* w);

i32  mel_gui__backend_slider_value    (Mel_Gui_Widget* w);
void mel_gui__backend_slider_set_value(Mel_Gui_Widget* w, i32 value);
bool mel_gui__backend_checkbox_checked(Mel_Gui_Widget* w);
