#pragma once

#include "graphical_app.h"

// Compute plasma: a compute kernel evaluates an animated plasma into a bindless
// storage buffer each frame; a buffer barrier then hands it to a graphics pass
// that reads it per-instance and draws a grid of coloured cells. Compute +
// barrier + draw share one frame command list. Requires the bindless heap.
extern const Graphical_App COMPUTE_PLASMA_APP;
