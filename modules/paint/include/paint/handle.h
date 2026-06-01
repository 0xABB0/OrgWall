#pragma once

#include <core/types.h>

#include <collection.slotmap/slotmap.fwd.h>

typedef Mel_SlotMap_Handle Mel_Drawable;
typedef Mel_SlotMap_Handle Mel_Pixmap;

bool mel_drawable_alive(Mel_Drawable d);
