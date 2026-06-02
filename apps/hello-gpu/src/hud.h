#pragma once

#include <gpu.h>

typedef struct
{
    char prefix[160];
    f64  ewma_dt;
    f64  accum;
    u32  frames;
} Hud;

void hud_init(Hud* hud, Mel_Gpu_Device* dev);

void hud_frame(Hud* hud, f64 dt, const char* suffix);
