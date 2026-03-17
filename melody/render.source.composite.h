#pragma once

#include "render.source.type.h"
#include "allocator.fwd.h"

extern const Mel_Render_Source_Type mel_source_composite_type;

Mel_Render_Source* mel_source_composite_create(const Mel_Alloc* alloc);
void mel_source_composite_add(Mel_Render_Source* composite, Mel_Render_Source* child);
