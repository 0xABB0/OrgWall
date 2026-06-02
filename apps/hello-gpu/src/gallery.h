#pragma once

#include "graphical_app.h"

// Fill / blend gallery: a grid of quads showing solid vs wireframe fill and
// opaque vs alpha vs additive blend, each cell driven by a distinct pipeline.
// Wireframe needs the device fill-mode-non-solid feature; an ungranted request
// degrades to solid with a warning (the RHI handles that, MEL-CODE-007).
// Renders straight to the swapchain; no bindless required.
extern const Graphical_App GALLERY_APP;
