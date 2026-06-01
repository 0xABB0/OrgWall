#pragma once

#include <core/types.h>

#include <allocator/allocator.fwd.h>
#include <color/rgba8.h>

#include <paint/handle.h>

typedef struct
{
    mel_color8* pixels;
    i32         stride;
    i32         w, h;
} Mel_Pixmap_Pixels;

Mel_Pixmap        mel_pixmap_create(const Mel_Alloc* alloc, i32 w, i32 h);
Mel_Drawable      mel_pixmap_drawable(Mel_Pixmap pm);
Mel_Pixmap_Pixels mel_pixmap_pixels(Mel_Pixmap pm);
void              mel_pixmap_destroy(Mel_Pixmap pm);
