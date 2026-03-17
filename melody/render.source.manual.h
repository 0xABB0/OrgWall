#pragma once

#include "render.source.type.h"
#include "render.manager.h"
#include "allocator.fwd.h"

extern const Mel_Render_Source_Type mel_source_manual_type;

Mel_Render_Source* mel_source_manual_create(const Mel_Alloc* alloc);

Mel_Render_Handle mel_source_manual_add(Mel_Render_Source* source,
                                         Mel_Mat4 transform,
                                         Mel_Render_Bounds bounds,
                                         Mel_Render_Info info);

void mel_source_manual_remove(Mel_Render_Source* source, Mel_Render_Handle h);

void mel_source_manual_set_transform(Mel_Render_Source* source,
                                      Mel_Render_Handle h, Mel_Mat4 transform);

void mel_source_manual_set_bounds(Mel_Render_Source* source,
                                   Mel_Render_Handle h, Mel_Render_Bounds bounds);

void mel_source_manual_set_info(Mel_Render_Source* source,
                                 Mel_Render_Handle h, Mel_Render_Info info);
