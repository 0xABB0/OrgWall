#include <time/duration.h>

#include <allocator/allocator.h>
#include <string/str8.h>

#include <stdio.h>

usize mel_dur_format(Mel_Duration d, char* out, usize cap)
{
    i64 a = mel_dur_abs(d);
    int n;

    if (a < MEL_NANOS_PER_US)
        n = snprintf(out, cap, "%lldns", (long long)d);
    else if (a < MEL_NANOS_PER_MS)
        n = snprintf(out, cap, "%.3fus", (double)d / (double)MEL_NANOS_PER_US);
    else if (a < MEL_NANOS_PER_SEC)
        n = snprintf(out, cap, "%.3fms", (double)d / (double)MEL_NANOS_PER_MS);
    else if (a < MEL_NANOS_PER_MIN)
        n = snprintf(out, cap, "%.3fs", (double)d / (double)MEL_NANOS_PER_SEC);
    else if (a < 60 * MEL_NANOS_PER_MIN)
    {
        const char* sign = d < 0 ? "-" : "";
        i64         total_ms = a / MEL_NANOS_PER_MS;
        i64         mins = total_ms / 60000;
        i64         secs = total_ms / 1000 % 60;
        i64         ms = total_ms % 1000;
        n = snprintf(out, cap, "%s%lldm%02lld.%03llds", sign, (long long)mins, (long long)secs, (long long)ms);
    }
    else
    {
        const char* sign = d < 0 ? "-" : "";
        i64         total_s = a / MEL_NANOS_PER_SEC;
        i64         hours = total_s / 3600;
        i64         mins = total_s / 60 % 60;
        i64         secs = total_s % 60;
        n = snprintf(out, cap, "%s%lldh%02lldm%02llds", sign, (long long)hours, (long long)mins, (long long)secs);
    }

    return n < 0 ? 0 : (usize)n;
}

str8 mel_dur_str(const Mel_Alloc* alloc, Mel_Duration d)
{
    usize needed = mel_dur_format(d, nullptr, 0);
    u8*   buf = (u8*)mel_alloc(alloc, needed + 1);
    mel_dur_format(d, (char*)buf, needed + 1);
    return (str8){ .data = buf, .len = (size)needed };
}
