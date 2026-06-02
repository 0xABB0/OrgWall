#pragma once

#include "graphical_app.h"

// GPU-driven dispatch screen. A cull compute pass appends the surviving agents and
// a device-scope count; a one-thread pass turns that count into the {gx,1,1}
// workgroup triple; cmd_dispatch_indirect runs the shade pass at exactly that size,
// splatting survivors into a bindless storage image — the next dispatch's size is
// computed on the GPU from data the GPU just produced, with no CPU round-trip.
extern const Graphical_App DISPATCH_INDIRECT_APP;
