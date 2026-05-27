#pragma once

#include <gpu/types.h>
#include <reactor/reactor.h>

// A reactor source that paces rendering of one swapchain. It is the swappable
// stand-in the reactor README calls "a GPU vsync waitable source": today a timer
// at hz frames/second, later a native presentation waitable registered as a poll.
typedef struct Mel_Gpu_Render_Source Mel_Gpu_Render_Source;

typedef void (*Mel_Gpu_Render_Fn)(Mel_Gpu_Swapchain* sc, f64 dt_seconds, void* user);

Mel_Gpu_Render_Source* mel_gpu_render_source_new(Mel_Reactor* reactor, Mel_Gpu_Swapchain* sc,
                                                 u32 hz, Mel_Gpu_Render_Fn fn, void* user);
void                   mel_gpu_render_source_destroy(Mel_Gpu_Render_Source* src);
