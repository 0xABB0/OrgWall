#pragma once

#include "graphical_app.h"

// Post-process: a scene rendered into an offscreen texture, then a fullscreen
// pass that samples it through the bindless heap and applies chromatic
// aberration + vignette + a tone curve. Render-to-texture + a second pass.
// Requires the bindless heap.
extern const Graphical_App POSTPROCESS_APP;
