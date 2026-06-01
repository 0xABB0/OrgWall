#pragma once

#include <core/types.h>

#include <allocator/allocator.fwd.h>
#include <collection.slotmap/slotmap.h>

/* One record per drawable, backend-opaque. `native` is the platform 2D context
 * (CGContextRef on quartz). The owned-pixmap fields are populated only when
 * `owns` is true; a future borrowed-window drawable leaves them zero. */
typedef struct
{
    void*            native;
    i32              w, h;
    bool             owns;
    const Mel_Alloc* alloc;
    u8*              pixels;
    i32              stride;
    bool             painting;
} Paint_Drawable;

Paint_Drawable*    mel_paint__get(Mel_SlotMap_Handle h);
Mel_SlotMap_Handle mel_paint__insert(const Paint_Drawable* rec);
void               mel_paint__remove(Mel_SlotMap_Handle h);
