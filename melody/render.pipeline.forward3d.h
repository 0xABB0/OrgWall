#pragma once

#include "event.channel.fwd.h"
#include "gpu.geometry_pool.fwd.h"
#include "render.material_base.fwd.h"

extern Mel_Event_Channel mel_pipeline_forward3d_ready;

void mel_pipeline_forward3d_set_geometry_pool(Mel_Geometry_Pool* pool);
