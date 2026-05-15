#pragma once

#include <core/types.h>

typedef struct Mel_Gui_Handle__* Mel_Gui_Handle;
typedef struct Mel_Gui_Class_Desc Mel_Gui_Class_Desc;
typedef struct Mel_Gui_Create_Desc Mel_Gui_Create_Desc;
typedef struct Mel_Gui_Message Mel_Gui_Message;

typedef u32 Mel_Gui_Atom;
typedef u32 Mel_Gui_Msg;
typedef u64 Mel_Gui_WParam;
typedef i64 Mel_Gui_LParam;
typedef i64 Mel_Gui_Result;

typedef Mel_Gui_Result (*Mel_Gui_Proc)(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam wparam, Mel_Gui_LParam lparam);
