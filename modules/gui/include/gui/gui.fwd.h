#pragma once

#include <core/types.h>
#include <collection.slotmap/slotmap.fwd.h>
#include <string/table.h>
#include <string/string.str8.fwd.h>
#include <gui/gui.message.h>

typedef struct { Mel_SlotMap_Handle handle; } Mel_Gui_Handle;

#define MEL_GUI_HANDLE_NONE ((Mel_Gui_Handle){0})

static inline bool mel_gui_handle_eq(Mel_Gui_Handle a, Mel_Gui_Handle b)
{
    return a.handle.index == b.handle.index && a.handle.generation == b.handle.generation;
}

static inline bool mel_gui_handle_is_none(Mel_Gui_Handle h)
{
    return h.handle.index == 0 && h.handle.generation == 0;
}

typedef struct Mel_Gui_Class_Desc  Mel_Gui_Class_Desc;
typedef struct Mel_Gui_Create_Desc Mel_Gui_Create_Desc;

typedef Mel_Gui_Result (*Mel_Gui_Proc)(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l);
