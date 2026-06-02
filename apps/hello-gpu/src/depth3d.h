#pragma once

#include "graphical_app.h"

// Depth-buffered 3D: many cubes rendered into an offscreen colour + D32 depth
// attachment, depth-tested by the hardware (vs cube.c's CPU back-face sort), then
// presented through the bindless heap. Requires the bindless heap to present.
extern const Graphical_App DEPTH3D_APP;
