#pragma once

#include "graphical_app.h"

// MSAA resolve screen: a spiky rotating star rendered into a multisample color
// attachment that resolves on-tile (Mel_Gpu_Color_Attachment.resolve_view) into a
// single-sample target, shown beside a single-sample reference of the same star so
// the smoothed edges read against the aliased ones. Sample count in the HUD.
extern const Graphical_App MSAA_APP;
