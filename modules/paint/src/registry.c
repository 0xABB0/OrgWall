#include "paint_internal.h"

#include <core/compiler.h>

#include <allocator/heap.h>
#include <debug/assert.h>

#include <paint/handle.h>
#include <paint/pixmap.h>

/* Module-global drawable table, eagerly built at load (no lazy-init race). The
 * table's bookkeeping uses the heap allocator — a constructor predates any
 * caller-supplied allocator; each pixmap's pixel buffer takes the caller's
 * allocator instead (MEL-CODE-003). */
Mel_SlotMap mel_paint__drawables;

MEL_CONSTRUCTOR static void mel_paint__registry_init(void) { mel_slotmap_init(&mel_paint__drawables, mel_alloc_heap(), .item_size = sizeof(Paint_Drawable), .initial_capacity = 16); }

Paint_Drawable* mel_paint__get(Mel_SlotMap_Handle h)
{
    mel_assert(mel_slotmap_alive(&mel_paint__drawables, h));
    return (Paint_Drawable*)mel_slotmap_get(&mel_paint__drawables, h);
}

Mel_SlotMap_Handle mel_paint__insert(const Paint_Drawable* rec) { return mel_slotmap_insert(&mel_paint__drawables, rec); }

void mel_paint__remove(Mel_SlotMap_Handle h) { mel_slotmap_remove(&mel_paint__drawables, h); }

bool mel_drawable_alive(Mel_Drawable d) { return mel_slotmap_alive(&mel_paint__drawables, d); }

Mel_Drawable mel_drawable_borrow(void* native, i32 w, i32 h)
{
    mel_assert(native && w > 0 && h > 0);
    Paint_Drawable rec = { .native = native, .w = w, .h = h, .owns = false, .alloc = NULL, .pixels = NULL, .stride = 0, .painting = false };
    return mel_paint__insert(&rec);
}

void mel_drawable_release(Mel_Drawable d)
{
    Paint_Drawable* rec = mel_paint__get(d);
    mel_assert(!rec->owns);
    mel_assert(!rec->painting);
    mel_paint__remove(d);
}

Mel_Drawable mel_pixmap_drawable(Mel_Pixmap pm) { return pm; }

Mel_Pixmap_Pixels mel_pixmap_pixels(Mel_Pixmap pm)
{
    Paint_Drawable* d = mel_paint__get(pm);
    mel_assert(d->owns);
    return (Mel_Pixmap_Pixels){ .pixels = (mel_color8*)d->pixels, .stride = d->stride, .w = d->w, .h = d->h };
}
