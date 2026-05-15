#pragma once

#include "gui.fwd.h"

#include <string/string.str8.h>

enum {
    MEL_GUI_ATOM_NONE = 0,
};

enum {
    MEL_GUI_ID_NONE = 0,
};

enum {
    MEL_GUI_USE_DEFAULT = 0x80000000u,
};

enum {
    MEL_GUI_WS_NONE         = 0,
    MEL_GUI_WS_CHILD        = 1u << 0,
    MEL_GUI_WS_VISIBLE      = 1u << 1,
    MEL_GUI_WS_DISABLED     = 1u << 2,
    MEL_GUI_WS_TABSTOP      = 1u << 3,
    MEL_GUI_WS_CLIPCHILDREN = 1u << 4,
    MEL_GUI_WS_OVERLAPPED   = 1u << 5,
    MEL_GUI_WS_TITLED       = 1u << 6,
    MEL_GUI_WS_RESIZABLE    = 1u << 7,
    MEL_GUI_WS_CLOSABLE     = 1u << 8,
    MEL_GUI_WS_MINIMIZABLE  = 1u << 9,
    MEL_GUI_WS_MAXIMIZABLE  = 1u << 10,
};

enum {
    MEL_GUI_WS_WINDOW =
        MEL_GUI_WS_OVERLAPPED |
        MEL_GUI_WS_TITLED |
        MEL_GUI_WS_RESIZABLE |
        MEL_GUI_WS_CLOSABLE |
        MEL_GUI_WS_MINIMIZABLE,
};

enum {
    MEL_GUI_EX_NONE = 0,
};

enum {
    MEL_GUI_MSG_NULL = 0,
    MEL_GUI_MSG_CREATE,
    MEL_GUI_MSG_DESTROY,
    MEL_GUI_MSG_CLOSE,
    MEL_GUI_MSG_SHOW,
    MEL_GUI_MSG_HIDE,
    MEL_GUI_MSG_ENABLE,
    MEL_GUI_MSG_SET_TEXT,
    MEL_GUI_MSG_GET_TEXT,
    MEL_GUI_MSG_SIZE,
    MEL_GUI_MSG_MOVE,
    MEL_GUI_MSG_COMMAND,
    MEL_GUI_MSG_NOTIFY,
    MEL_GUI_MSG_CLICK,
    MEL_GUI_MSG_VALUE_CHANGED,
    MEL_GUI_MSG_TEXT_CHANGED,
    MEL_GUI_MSG_FOCUS_GAINED,
    MEL_GUI_MSG_FOCUS_LOST,
    MEL_GUI_MSG_KEY_DOWN,
    MEL_GUI_MSG_KEY_UP,
    MEL_GUI_MSG_POINTER_DOWN,
    MEL_GUI_MSG_POINTER_UP,
    MEL_GUI_MSG_POINTER_MOVE,
    MEL_GUI_MSG_PAINT,
    MEL_GUI_MSG_QUIT,
    MEL_GUI_MSG_USER = 0x4000,
};

struct Mel_Gui_Class_Desc {
    str8 name;
    str8 base_name;
    u32 style;
    Mel_Gui_Proc proc;
};

struct Mel_Gui_Create_Desc {
    str8 class_name;
    str8 text;
    u32 style;
    u32 ex_style;
    i32 x;
    i32 y;
    i32 w;
    i32 h;
    Mel_Gui_Handle parent;
    u32 id;
    Mel_Gui_Proc proc;
    void* user;
};

struct Mel_Gui_Message {
    Mel_Gui_Handle h;
    Mel_Gui_Msg msg;
    Mel_Gui_WParam wparam;
    Mel_Gui_LParam lparam;
};

bool mel_gui_init(void);
void mel_gui_shutdown(void);

Mel_Gui_Atom mel_gui_register_class(const Mel_Gui_Class_Desc* desc);
Mel_Gui_Handle mel_gui_create(const Mel_Gui_Create_Desc* desc);
void mel_gui_destroy(Mel_Gui_Handle h);

Mel_Gui_Result mel_gui_send_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam);
bool mel_gui_post_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam);
bool mel_gui_get_message(Mel_Gui_Message* out);
Mel_Gui_Result mel_gui_dispatch_message(const Mel_Gui_Message* message);

void mel_gui_show(Mel_Gui_Handle h, bool visible);
void mel_gui_enable(Mel_Gui_Handle h, bool enabled);
void mel_gui_set_text(Mel_Gui_Handle h, str8 text);
void mel_gui_set_rect(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height);
void* mel_gui_native_handle(Mel_Gui_Handle h);
Mel_Gui_Handle mel_gui_parent(Mel_Gui_Handle h);
u32 mel_gui_id(Mel_Gui_Handle h);
void* mel_gui_user(Mel_Gui_Handle h);
void mel_gui_set_user(Mel_Gui_Handle h, void* user);
