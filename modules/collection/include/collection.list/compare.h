#pragma once

#include <core/types.h>

static inline i32 mel_compare_int(const void* a, const void* b)
{
    intptr_t ia = (intptr_t)a;
    intptr_t ib = (intptr_t)b;
    return (ia > ib) - (ia < ib);
}
