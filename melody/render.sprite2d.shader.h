#pragma once
#include "math.mat4.h"

typedef struct {
    Mel_Mat4 projection;
} Mel_Sprite2D_Push_Constants;

_Static_assert(sizeof(Mel_Sprite2D_Push_Constants) == 64, "Mel_Sprite2D_Push_Constants must be 64 bytes");
