#pragma once

#include "render.viewport.fwd.h"
#include "core.types.h"

Mel_SlotMap_Handle mel__view_registry_insert(const Mel_Render_View* view);
void              mel__view_registry_remove(Mel_SlotMap_Handle handle);
Mel_Render_View*  mel__view_registry_get(Mel_SlotMap_Handle handle);
bool              mel__view_registry_alive(Mel_SlotMap_Handle handle);
u32               mel__view_registry_count(void);
Mel_Render_View*  mel__view_registry_data(void);
Mel_Render_View*  mel__view_registry_at(u32 packed_index);
Mel_Render_View_Handle mel__view_registry_handle_at(u32 packed_index);
