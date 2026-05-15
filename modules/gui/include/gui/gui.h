#pragma once

#include "gui.fwd.h"

#include <string/string.str8.h>

#define MEL_GUI_ID_NONE          ((u32)0)
#define MEL_GUI_DEFAULT_POSITION ((i32)0x80000000)

enum {
    MEL_GUI_WS_NONE         = 0,
    MEL_GUI_WS_CHILD        = 1u << 0,
    MEL_GUI_WS_VISIBLE      = 1u << 1,
    MEL_GUI_WS_DISABLED     = 1u << 2,
    MEL_GUI_WS_TABSTOP      = 1u << 3,
    MEL_GUI_WS_CLIPCHILDREN = 1u << 4,
};

enum {
    MEL_GUI_SWP_NOMOVE  = 1u << 0,
    MEL_GUI_SWP_NOSIZE  = 1u << 1,
    MEL_GUI_SWP_SHOW    = 1u << 2,
    MEL_GUI_SWP_HIDE    = 1u << 3,
    MEL_GUI_SWP_ENABLE  = 1u << 4,
    MEL_GUI_SWP_DISABLE = 1u << 5,
};

struct Mel_Gui_Class_Desc {
    str8         name;
    Mel_Atom     base_class;
    u32          style;
    Mel_Gui_Proc proc;
};

struct Mel_Gui_Create_Desc {
    Mel_Atom       class_atom;
    str8           text;
    u32            style;
    i32            x;
    i32            y;
    i32            w;
    i32            h;
    Mel_Gui_Handle parent;
    u32            id;
    Mel_Gui_Proc   proc;
    void*          user;
};

bool mel_gui_init(void);
void mel_gui_shutdown(void);
bool mel_gui_is_initialized(void);

bool mel_gui_is_ui_thread(void);
void mel_gui_assert_ui_thread(void);

Mel_Atom_Table* mel_gui_atom_table(void);

Mel_Atom mel_gui_register_class(const Mel_Gui_Class_Desc* desc);
Mel_Atom mel_gui_class_atom(str8 name);
Mel_Atom mel_gui_class_base(Mel_Atom class_atom);
str8     mel_gui_class_name(Mel_Atom class_atom);

Mel_Gui_Handle mel_gui_create(const Mel_Gui_Create_Desc* desc);
bool           mel_gui_destroy(Mel_Gui_Handle h);
void           mel_gui_destroy_all_roots(void);
bool           mel_gui_alive(Mel_Gui_Handle h);

Mel_Gui_Result mel_gui_send_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l);
Mel_Gui_Result mel_gui_call_super(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l);
Mel_Gui_Result mel_gui_def_proc(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l);

bool           mel_gui_post_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l);

void           mel_gui_dispatch_app_message(Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l);

bool mel_gui_attach_proc(Mel_Gui_Handle h, Mel_Gui_Proc proc);
bool mel_gui_detach_proc(Mel_Gui_Handle h, Mel_Gui_Proc proc);

bool mel_gui_set_window_pos(Mel_Gui_Handle h, i32 x, i32 y, i32 w, i32 hgt, u32 flags);
bool mel_gui_set_text(Mel_Gui_Handle h, str8 text);

Mel_Gui_Handle mel_gui_parent(Mel_Gui_Handle h);
u32            mel_gui_id(Mel_Gui_Handle h);
u32            mel_gui_style(Mel_Gui_Handle h);
void*          mel_gui_user(Mel_Gui_Handle h);
void           mel_gui_set_user(Mel_Gui_Handle h, void* user);
void*          mel_gui_native(Mel_Gui_Handle h);
Mel_Atom       mel_gui_class_of(Mel_Gui_Handle h);
