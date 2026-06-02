#pragma once

#include "graphical_app.h"

// Two-pass mini render-graph: a depth-only prepass establishes the nearest-Z of a
// heavily overlapping scene, then a lit pass renders the same geometry with an
// EQUAL depth test and depth-write off, so only the fragments that own the nearest
// surface shade — overdraw is killed by the prepass. One depth attachment shared
// across both passes; the lit colour is blit-presented.
extern const Graphical_App PREPASS_APP;
