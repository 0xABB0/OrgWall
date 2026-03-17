#pragma once

#include "render.viewport.fwd.h"
#include "core.types.h"

void mel__view_registry_add(Mel_Render_View* view);
void mel__view_registry_remove(Mel_Render_View* view);
u32  mel__view_registry_count(void);
Mel_Render_View* mel__view_registry_at(u32 index);
