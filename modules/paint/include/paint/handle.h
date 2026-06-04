#pragma once

#include <core/types.h>

#include <collection/slotmap.fwd.h>

typedef Mel_SlotMap_Handle Mel_Drawable;
typedef Mel_SlotMap_Handle Mel_Pixmap;

bool mel_drawable_alive(Mel_Drawable d);

Mel_Drawable mel_drawable_borrow(void* native, i32 w, i32 h);
void         mel_drawable_release(Mel_Drawable d);
