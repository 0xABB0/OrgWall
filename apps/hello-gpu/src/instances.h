#pragma once

#include "graphical_app.h"

// Instancing: a swarm of quads drawn with one instanced draw call, each
// instance's transform and colour pulled from a bindless storage buffer indexed
// by gl_InstanceIndex. Animated on the CPU each frame. Requires the bindless heap.
extern const Graphical_App INSTANCES_APP;
