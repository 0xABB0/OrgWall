#pragma once

#include <core/types.h>

#include <allocator/allocator.fwd.h>
#include <collection/slotmap.h>
#include <image/image.h>

typedef struct
{
    void*     native;
    i32       w, h;
    bool      owns;
    Mel_Image img;
    bool      painting;
} Paint_Drawable;

Paint_Drawable*    mel_paint__get(Mel_SlotMap_Handle h);
Mel_SlotMap_Handle mel_paint__insert(const Paint_Drawable* rec);
void               mel_paint__remove(Mel_SlotMap_Handle h);
