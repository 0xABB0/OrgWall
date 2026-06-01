#pragma once

#include <core/types.h>
#include <reactor/reactor.h>
#include <gpu/swapchain.h>

typedef struct Mel_Gpu_Render_Source Mel_Gpu_Render_Source;

typedef void (*Mel_Gpu_Render_Fn)(Mel_Gpu_Swapchain* sc, f64 dt_seconds, void* user);

Mel_Gpu_Render_Source* mel_gpu_render_source_new(Mel_Reactor* reactor, Mel_Gpu_Swapchain* sc, u32 hz, Mel_Gpu_Render_Fn fn, void* user);
void                   mel_gpu_render_source_destroy(Mel_Gpu_Render_Source* src);
