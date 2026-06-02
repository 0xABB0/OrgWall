#pragma once

#include "graphical_app.h"

// Bindless textured quad (gpu-rhi.md §6.7 headline): a procedurally-filled
// texture sampled through the device bindless heap, the slot delivered in the
// push-constant root record. Requires the bindless heap.
extern const Graphical_App TEXQUAD_APP;
