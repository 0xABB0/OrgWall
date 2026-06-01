#include <time/clock.h>
#include <time/duration.h>

#include <allocator/allocator.h>
#include <string/str8.h>

#include <stdio.h>

static constexpr i64 SECS_PER_DAY = 86400;

static i64 mel__floor_div(i64 a, i64 b) { return a / b - (a % b != 0 && (a % b < 0) != (b < 0)); }

static i64 mel__days_from_civil(i64 y, i64 m, i64 d)
{
    y -= m <= 2;
    i64 era = (y >= 0 ? y : y - 399) / 400;
    i64 yoe = y - era * 400;
    i64 doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    i64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void mel__civil_from_days(i64 z, i64* y, i64* m, i64* d)
{
    z += 719468;
    i64 era = (z >= 0 ? z : z - 146096) / 146097;
    i64 doe = z - era * 146097;
    i64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    i64 yy = yoe + era * 400;
    i64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    i64 mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = yy + (*m <= 2);
}

static u8 mel__iso_weekday(i64 z)
{
    i64 w = z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6;
    return (u8)(w == 0 ? 7 : w);
}

Mel_Civil mel_civil_from_unix_ns(mel_nanosec utc, i16 tz_offset_min)
{
    i64 ns = (i64)utc + (i64)tz_offset_min * 60 * MEL_NANOS_PER_SEC;
    i64 secs = mel__floor_div(ns, MEL_NANOS_PER_SEC);
    i64 sub = ns - secs * MEL_NANOS_PER_SEC;
    i64 days = mel__floor_div(secs, SECS_PER_DAY);
    i64 sod = secs - days * SECS_PER_DAY;

    i64 y, m, d;
    mel__civil_from_days(days, &y, &m, &d);

    Mel_Civil c = {
        .year = (i32)y,
        .month = (u8)m,
        .day = (u8)d,
        .hour = (u8)(sod / 3600),
        .minute = (u8)(sod / 60 % 60),
        .second = (u8)(sod % 60),
        .nanosecond = (u32)sub,
        .weekday = mel__iso_weekday(days),
        .tz_offset_min = tz_offset_min,
    };
    return c;
}

mel_nanosec mel_civil_to_unix_ns(Mel_Civil c)
{
    i64 days = mel__days_from_civil(c.year, c.month, c.day);
    i64 secs = days * SECS_PER_DAY + (i64)c.hour * 3600 + (i64)c.minute * 60 + c.second;
    i64 ns = secs * MEL_NANOS_PER_SEC + c.nanosecond;
    ns -= (i64)c.tz_offset_min * 60 * MEL_NANOS_PER_SEC;
    return (mel_nanosec)ns;
}

usize mel_civil_format_iso8601(Mel_Civil c, char* out, usize cap)
{
    int n;
    if (c.tz_offset_min == 0)
    {
        if (c.nanosecond)
            n = snprintf(out, cap, "%04d-%02u-%02uT%02u:%02u:%02u.%09uZ", c.year, c.month, c.day, c.hour, c.minute, c.second, c.nanosecond);
        else
            n = snprintf(out, cap, "%04d-%02u-%02uT%02u:%02u:%02uZ", c.year, c.month, c.day, c.hour, c.minute, c.second);
    }
    else
    {
        int  off = c.tz_offset_min;
        char sign = off < 0 ? '-' : '+';
        if (off < 0)
            off = -off;
        if (c.nanosecond)
            n = snprintf(out, cap, "%04d-%02u-%02uT%02u:%02u:%02u.%09u%c%02d:%02d", c.year, c.month, c.day, c.hour, c.minute, c.second, c.nanosecond, sign, off / 60, off % 60);
        else
            n = snprintf(out, cap, "%04d-%02u-%02uT%02u:%02u:%02u%c%02d:%02d", c.year, c.month, c.day, c.hour, c.minute, c.second, sign, off / 60, off % 60);
    }
    return n < 0 ? 0 : (usize)n;
}

str8 mel_civil_iso8601(const Mel_Alloc* alloc, Mel_Civil c)
{
    usize needed = mel_civil_format_iso8601(c, nullptr, 0);
    u8*   buf = (u8*)mel_alloc(alloc, needed + 1);
    mel_civil_format_iso8601(c, (char*)buf, needed + 1);
    return (str8){ .data = buf, .len = (size)needed };
}

typedef struct
{
    const u8* p;
    const u8* end;
} Mel__Cursor;

static bool mel__rd_digits(Mel__Cursor* c, int n, i64* v)
{
    if (c->end - c->p < n)
        return false;
    i64 acc = 0;
    for (int i = 0; i < n; i++)
    {
        u8 ch = c->p[i];
        if (ch < '0' || ch > '9')
            return false;
        acc = acc * 10 + (ch - '0');
    }
    c->p += n;
    *v = acc;
    return true;
}

static bool mel__rd_char(Mel__Cursor* c, char ch)
{
    if (c->p < c->end && *c->p == (u8)ch)
    {
        c->p++;
        return true;
    }
    return false;
}

bool mel_civil_parse_iso8601(str8 in, Mel_Civil* out)
{
    Mel__Cursor c = { in.data, in.data + in.len };
    i64         y, mo, da, h, mi, s;

    if (!mel__rd_digits(&c, 4, &y) || !mel__rd_char(&c, '-'))
        return false;
    if (!mel__rd_digits(&c, 2, &mo) || !mel__rd_char(&c, '-'))
        return false;
    if (!mel__rd_digits(&c, 2, &da))
        return false;
    if (!mel__rd_char(&c, 'T') && !mel__rd_char(&c, ' '))
        return false;
    if (!mel__rd_digits(&c, 2, &h) || !mel__rd_char(&c, ':'))
        return false;
    if (!mel__rd_digits(&c, 2, &mi) || !mel__rd_char(&c, ':'))
        return false;
    if (!mel__rd_digits(&c, 2, &s))
        return false;

    i64 ns = 0;
    if (mel__rd_char(&c, '.'))
    {
        int ndig = 0;
        while (c.p < c.end && *c.p >= '0' && *c.p <= '9' && ndig < 9)
        {
            ns = ns * 10 + (*c.p - '0');
            c.p++;
            ndig++;
        }
        if (ndig == 0)
            return false;
        if (c.p < c.end && *c.p >= '0' && *c.p <= '9')
            return false;
        for (int i = ndig; i < 9; i++)
            ns *= 10;
    }

    i64 off = 0;
    if (!mel__rd_char(&c, 'Z'))
    {
        i64 sign;
        if (mel__rd_char(&c, '+'))
            sign = 1;
        else if (mel__rd_char(&c, '-'))
            sign = -1;
        else
            return false;
        i64 oh, om;
        if (!mel__rd_digits(&c, 2, &oh) || !mel__rd_char(&c, ':') || !mel__rd_digits(&c, 2, &om))
            return false;
        off = sign * (oh * 60 + om);
    }

    if (c.p != c.end)
        return false;
    if (mo < 1 || mo > 12 || da < 1 || da > 31 || h > 23 || mi > 59 || s > 59)
        return false;

    Mel_Civil r = {
        .year = (i32)y,
        .month = (u8)mo,
        .day = (u8)da,
        .hour = (u8)h,
        .minute = (u8)mi,
        .second = (u8)s,
        .nanosecond = (u32)ns,
        .weekday = mel__iso_weekday(mel__days_from_civil(y, mo, da)),
        .tz_offset_min = (i16)off,
    };
    *out = r;
    return true;
}

void mel_clock_anchor_now(Mel_Clock_Anchor* a)
{
    a->mono = mel_nanos_since_unspecified_epoch();
    a->wall = mel_wall_now_ns();
}

mel_nanosec mel_wall_from_mono(const Mel_Clock_Anchor* a, mel_nanosec mono) { return (mel_nanosec)((i64)a->wall + ((i64)mono - (i64)a->mono)); }
