#pragma once

#include "gpu.cmd.fwd.h"
#include "gpu.device.fwd.h"
#include "gpu.types.h"
#include "math.geo.rect.h"

typedef struct Mel_Render_Target Mel_Render_Target;

void mel__blit_to_target(Mel_Gpu_Cmd* cmd, Mel_Gpu_Device* dev,
                          Mel_Render_Target* src, Mel_Render_Target* dst,
                          u32 dst_width, u32 dst_height,
                          u32 scale_mode);

Mel_Rect mel__compute_scaled_viewport(u32 src_w, u32 src_h,
                                       u32 dst_w, u32 dst_h,
                                       u32 scale_mode);
