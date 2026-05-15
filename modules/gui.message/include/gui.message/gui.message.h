#pragma once

#include <core/types.h>

typedef u32 Mel_Gui_Msg;
typedef u64 Mel_Gui_WParam;
typedef i64 Mel_Gui_LParam;
typedef i64 Mel_Gui_Result;

enum {
    MEL_GUI_OK                   =  0,
    MEL_GUI_ERR_INVALID_HANDLE   = -1,
    MEL_GUI_ERR_NOT_INITIALIZED  = -2,
    MEL_GUI_ERR_NO_PROC          = -3,
    MEL_GUI_ERR_BAD_DESC         = -4,
    MEL_GUI_ERR_CLASS_UNKNOWN    = -5,
    MEL_GUI_ERR_PLATFORM         = -6,
    MEL_GUI_ERR_VETOED           = -7,
    MEL_GUI_ERR_OUT_OF_MEMORY    = -8,
    MEL_GUI_ERR_NO_PARENT        = -9,
};

enum {
    MEL_GUI_MSG_NULL = 0,

    MEL_GUI_MSG_NCCREATE,
    MEL_GUI_MSG_CREATE,
    MEL_GUI_MSG_DESTROY,
    MEL_GUI_MSG_NCDESTROY,
    MEL_GUI_MSG_CLOSE,

    MEL_GUI_MSG_SHOW,
    MEL_GUI_MSG_HIDE,
    MEL_GUI_MSG_ENABLE,

    MEL_GUI_MSG_SET_TEXT,
    MEL_GUI_MSG_GET_TEXT,

    MEL_GUI_MSG_MOVE,
    MEL_GUI_MSG_SIZE,

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

    MEL_GUI_MSG_APP_CREATE,
    MEL_GUI_MSG_APP_RESUME,
    MEL_GUI_MSG_APP_PAUSE,
    MEL_GUI_MSG_APP_DESTROY,
    MEL_GUI_MSG_APP_BACK,
    MEL_GUI_MSG_APP_CONFIG_CHANGED,

    MEL_GUI_MSG_USER = 0x4000,
};

static inline Mel_Gui_LParam mel_gui_pack_xy(i32 x, i32 y)
{
    return ((Mel_Gui_LParam)(u32)y << 32) | (Mel_Gui_LParam)(u32)x;
}

static inline i32 mel_gui_unpack_x(Mel_Gui_LParam l) { return (i32)(u32)((u64)l & 0xFFFFFFFFu); }
static inline i32 mel_gui_unpack_y(Mel_Gui_LParam l) { return (i32)(u32)((u64)l >> 32); }

static inline Mel_Gui_LParam mel_gui_pack_wh(i32 w, i32 h) { return mel_gui_pack_xy(w, h); }
static inline i32 mel_gui_unpack_w(Mel_Gui_LParam l) { return mel_gui_unpack_x(l); }
static inline i32 mel_gui_unpack_h(Mel_Gui_LParam l) { return mel_gui_unpack_y(l); }
