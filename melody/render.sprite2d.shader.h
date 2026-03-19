#pragma once
#include "math.mat4.h"

typedef struct {
    Mel_Mat4 projection;
    u32 draw_offset;
    u32 _pad[3];
} Mel_Sprite2D_Push_Constants;

_Static_assert(sizeof(Mel_Sprite2D_Push_Constants) == 80, "Mel_Sprite2D_Push_Constants must be 80 bytes");
