#pragma once

#include <reactor/reactor.h>
#include "graphical_app.h"

// Wires the host to the reactor (needed to attach per-window render sources) and
// creates the shared GPU device.
void gpu_host_init(Mel_Reactor* reactor);

// Opens a new top-level window whose content is the given graphical app rendered
// into a gpu-view component sitting beside native widgets.
void gpu_host_open(const Graphical_App* app);
