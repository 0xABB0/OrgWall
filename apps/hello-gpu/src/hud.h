#pragma once

#include <gpu.h>

// Reusable caps + FPS HUD. A screen embeds one, calls hud_init once (it snapshots
// the device caps into a fixed prefix) and hud_frame(dt, suffix) per frame; the
// helper smooths the frame time into an FPS reading and pushes
// "<adapter> · bindless <tier> · <fps> fps · <suffix>" to the host status label
// through gpu_host_set_status. The whole live HUD the round-1 screens lacked, in
// two call sites (MEL-ENGINE-IX, MEL-ENGINE-X: the engine is the wingman; the
// number lives in the chrome, not the scene).
typedef struct
{
    char prefix[160]; // adapter name + bindless tier, snapshotted at init
    f64  ewma_dt;     // smoothed seconds/frame; 0 until the first frame seeds it
    f64  accum;       // wall-clock since the last label push
    u32  frames;      // frames since the last label push
} Hud;

// Snapshots the device's adapter name and bindless tier into the fixed prefix.
void hud_init(Hud* hud, Mel_Gpu_Device* dev);

// Advances the FPS estimate by `dt` seconds and, a few times per second, pushes
// the formatted status (prefix + smoothed fps + the screen's own `suffix`, which
// may be NULL) to the host label. Cheap to call every frame.
void hud_frame(Hud* hud, f64 dt, const char* suffix);
