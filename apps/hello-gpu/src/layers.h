#pragma once

#include "graphical_app.h"

// Alpha-blended layers: a gradient backdrop with several translucent quads drawn
// over it through a MEL_GPU_BLEND_ALPHA pipeline, showing src-over compositing.
// Renders straight to the swapchain; no bindless required.
extern const Graphical_App LAYERS_APP;
