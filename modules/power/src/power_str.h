#pragma once

#include <power/power.h>

#include <string.h>

static inline bool mel_power_name_copy(char* buf, usize cap, const char* src)
{
    if (!src || !buf || cap == 0)
        return false;
    usize n = strlen(src);
    if (n + 1 > cap)
        return false;
    memcpy(buf, src, n + 1);
    return true;
}
