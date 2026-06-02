#pragma once

#include "graphical_app.h"

// GPU particle system: a compute pass integrates tens of thousands of particles in a
// device storage buffer each frame (attractor pull + drag + respawn), then one
// instanced draw reads the same buffer to splat an additive quad per particle. The
// canonical integrate-then-draw the engine should make trivial.
extern const Graphical_App PARTICLES_APP;
