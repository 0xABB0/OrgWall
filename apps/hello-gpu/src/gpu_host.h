#pragma once

#include <reactor/reactor.h>
#include <string/str8.h>
#include "graphical_app.h"

// Wires the host to the reactor (needed to attach per-window render sources) and
// creates the shared GPU device.
void gpu_host_init(Mel_Reactor* reactor);

// Opens a new top-level window whose content is the given graphical app rendered
// into a gpu-view component sitting beside native widgets.
void gpu_host_open(const Graphical_App* app);

// HUD seam (MEL-ENGINE-X). Sets the native status label that sits above the GPU
// view of the window currently being rendered. A screen calls this from its
// `render` callback (it runs on the reactor/UI thread, so the Cocoa label mutation
// is in-band) to surface live device caps, a measured FPS, or its own state. A
// call from outside a render callback is a no-op. The text is copied by the GUI
// backend, so a stack buffer is fine.
void gpu_host_set_status(str8 text);
