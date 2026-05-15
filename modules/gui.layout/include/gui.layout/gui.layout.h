#pragma once

#include <gui/gui.fwd.h>

typedef enum Mel_Gui_Unit_Kind {
    MEL_GUI_UNIT_DEVICE_PIXEL,
    MEL_GUI_UNIT_PLATFORM_POINT,
    MEL_GUI_UNIT_DENSITY_PIXEL,
    MEL_GUI_UNIT_CSS_PIXEL,
    MEL_GUI_UNIT_METER,
} Mel_Gui_Unit_Kind;

typedef struct Mel_Gui_Unit {
    f32 value;
    Mel_Gui_Unit_Kind kind;
} Mel_Gui_Unit;

typedef struct Mel_Gui_Point {
    i32 x;
    i32 y;
} Mel_Gui_Point;

typedef struct Mel_Gui_Size {
    i32 w;
    i32 h;
} Mel_Gui_Size;

typedef struct Mel_Gui_Rect {
    i32 x;
    i32 y;
    i32 w;
    i32 h;
} Mel_Gui_Rect;

static inline Mel_Gui_Unit mel_gui_unit(f32 value, Mel_Gui_Unit_Kind kind)
{
    return (Mel_Gui_Unit){ .value = value, .kind = kind };
}

static inline Mel_Gui_Rect mel_gui_rect(i32 x, i32 y, i32 w, i32 h)
{
    return (Mel_Gui_Rect){ .x = x, .y = y, .w = w, .h = h };
}
