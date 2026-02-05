#pragma once

#include "types.h"

#define MEL_NEVENT_NONE     0
#define MEL_NEVENT_CLICK    1
#define MEL_NEVENT_CHANGE   2
#define MEL_NEVENT_CONFIRM  3
#define MEL_NEVENT_CLOSE    4
#define MEL_NEVENT_RESIZE   5
#define MEL_NEVENT_SELECT   6
#define MEL_NEVENT_ACTION   7

struct Mel_NEvent {
    i32 type;
    void* sender;
    union {
        struct { f32 x, y, w, h; } resize;
        struct { i32 index; } select;
        struct { f64 value; } change;
        struct { bool confirmed; } confirm;
    };
};
