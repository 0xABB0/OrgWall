#pragma once

#include <core/types.h>

#define MEL__QR_MAXV 10

static const i32 MEL__QR_EC[MEL__QR_MAXV][4][5] = {
    { { 7, 1, 19, 0, 0 }, { 10, 1, 16, 0, 0 }, { 13, 1, 13, 0, 0 }, { 17, 1, 9, 0, 0 } },       { { 10, 1, 34, 0, 0 }, { 16, 1, 28, 0, 0 }, { 22, 1, 22, 0, 0 }, { 28, 1, 16, 0, 0 } },
    { { 15, 1, 55, 0, 0 }, { 26, 1, 44, 0, 0 }, { 18, 2, 17, 0, 0 }, { 22, 2, 13, 0, 0 } },     { { 20, 1, 80, 0, 0 }, { 18, 2, 32, 0, 0 }, { 26, 2, 24, 0, 0 }, { 16, 4, 9, 0, 0 } },
    { { 26, 1, 108, 0, 0 }, { 24, 2, 43, 0, 0 }, { 18, 2, 15, 2, 16 }, { 22, 2, 11, 2, 12 } },  { { 18, 2, 68, 0, 0 }, { 16, 4, 27, 0, 0 }, { 24, 4, 19, 0, 0 }, { 28, 4, 15, 0, 0 } },
    { { 20, 2, 78, 0, 0 }, { 18, 4, 31, 0, 0 }, { 18, 2, 14, 4, 15 }, { 26, 4, 13, 1, 14 } },   { { 24, 2, 97, 0, 0 }, { 22, 2, 38, 2, 39 }, { 22, 4, 18, 2, 19 }, { 26, 4, 14, 2, 15 } },
    { { 30, 2, 116, 0, 0 }, { 22, 3, 36, 2, 37 }, { 20, 4, 16, 4, 17 }, { 24, 4, 12, 4, 13 } }, { { 18, 2, 68, 2, 69 }, { 26, 4, 43, 1, 44 }, { 24, 6, 19, 2, 20 }, { 28, 6, 15, 2, 16 } },
};

static const i32 MEL__QR_TOTAL[MEL__QR_MAXV] = {
    26, 44, 70, 100, 134, 172, 196, 242, 292, 346,
};

static const u32 MEL__QR_FORMAT_CODES[32] = {
    0x5412u, 0x5125u, 0x5E7Cu, 0x5B4Bu, 0x45F9u, 0x40CEu, 0x4F97u, 0x4AA0u, 0x77C4u, 0x72F3u, 0x7DAAu, 0x789Du, 0x662Fu, 0x6318u, 0x6C41u, 0x6976u,
    0x1689u, 0x13BEu, 0x1CE7u, 0x19D0u, 0x0762u, 0x0255u, 0x0D0Cu, 0x083Bu, 0x355Fu, 0x3068u, 0x3F31u, 0x3A06u, 0x24B4u, 0x2183u, 0x2EDAu, 0x2BEDu,
};

static const char MEL__QR_ALNUM[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

static const i32 MEL__QR_ALIGN[MEL__QR_MAXV][4] = {
    { 0, 0, 0, 0 }, { 2, 6, 18, 0 }, { 2, 6, 22, 0 }, { 2, 6, 26, 0 }, { 2, 6, 30, 0 }, { 2, 6, 34, 0 }, { 3, 6, 22, 38 }, { 3, 6, 24, 42 }, { 3, 6, 26, 46 }, { 3, 6, 28, 50 },
};

static i32 mel__qr_count_bits(i32 version, i32 mode)
{
    static const i32 width[3][3] = { { 10, 9, 8 }, { 12, 11, 16 }, { 14, 13, 16 } };
    i32              group = version <= 9 ? 0 : (version <= 26 ? 1 : 2);
    return width[group][mode];
}

static i32 mel__qr_data_cw(i32 version, i32 rank)
{
    const i32* e = MEL__QR_EC[version - 1][rank];
    return e[1] * e[2] + e[3] * e[4];
}

static u8 mel__qr_bit(u32 v, i32 i) { return (u8)((v >> i) & 1u); }

static i32 mel__qr_absmax(i32 a, i32 b)
{
    i32 aa = a < 0 ? -a : a;
    i32 bb = b < 0 ? -b : b;
    return aa > bb ? aa : bb;
}

static void mel__qr_set(u8* g, u8* fn, i32 size, i32 r, i32 c, bool dark)
{
    if (g != NULL)
    {
        g[r * size + c] = dark ? 1 : 0;
    }
    fn[r * size + c] = 1;
}

static bool mel__qr_mask(i32 m, i32 r, i32 c)
{
    switch (m)
    {
    case 0:
        return (r + c) % 2 == 0;
    case 1:
        return r % 2 == 0;
    case 2:
        return c % 3 == 0;
    case 3:
        return (r + c) % 3 == 0;
    case 4:
        return (r / 2 + c / 3) % 2 == 0;
    case 5:
        return (r * c) % 2 + (r * c) % 3 == 0;
    case 6:
        return ((r * c) % 2 + (r * c) % 3) % 2 == 0;
    default:
        return ((r + c) % 2 + (r * c) % 3) % 2 == 0;
    }
}

static void mel__qr_draw_finder(u8* g, u8* fn, i32 size, i32 cr, i32 cc)
{
    for (i32 dr = -4; dr <= 4; ++dr)
    {
        for (i32 dc = -4; dc <= 4; ++dc)
        {
            i32 r = cr + dr, c = cc + dc;
            if (r < 0 || r >= size || c < 0 || c >= size)
            {
                continue;
            }
            i32 dist = mel__qr_absmax(dr, dc);
            mel__qr_set(g, fn, size, r, c, dist != 2 && dist != 4);
        }
    }
}

static void mel__qr_draw_function(u8* g, u8* fn, i32 size, i32 version)
{
    for (i32 i = 0; i < size; ++i)
    {
        mel__qr_set(g, fn, size, 6, i, (i % 2) == 0);
        mel__qr_set(g, fn, size, i, 6, (i % 2) == 0);
    }
    mel__qr_draw_finder(g, fn, size, 3, 3);
    mel__qr_draw_finder(g, fn, size, 3, size - 4);
    mel__qr_draw_finder(g, fn, size, size - 4, 3);

    const i32* al = MEL__QR_ALIGN[version - 1];
    i32        cnt = al[0];
    for (i32 a = 1; a <= cnt; ++a)
    {
        for (i32 b = 1; b <= cnt; ++b)
        {
            bool corner = (a == 1 && b == 1) || (a == 1 && b == cnt) || (a == cnt && b == 1);
            if (corner)
            {
                continue;
            }
            i32 pr = al[a], pc = al[b];
            for (i32 dr = -2; dr <= 2; ++dr)
            {
                for (i32 dc = -2; dc <= 2; ++dc)
                {
                    mel__qr_set(g, fn, size, pr + dr, pc + dc, mel__qr_absmax(dr, dc) != 1);
                }
            }
        }
    }

    for (i32 c = 0; c < 9; ++c)
    {
        if (c != 6)
        {
            mel__qr_set(g, fn, size, 8, c, false);
        }
    }
    for (i32 r = 0; r < 9; ++r)
    {
        if (r != 6)
        {
            mel__qr_set(g, fn, size, r, 8, false);
        }
    }
    for (i32 c = size - 8; c < size; ++c)
    {
        mel__qr_set(g, fn, size, 8, c, false);
    }
    for (i32 r = size - 8; r < size; ++r)
    {
        mel__qr_set(g, fn, size, r, 8, false);
    }
    mel__qr_set(g, fn, size, size - 8, 8, true);

    if (version >= 7)
    {
        for (i32 i = 0; i < 6; ++i)
        {
            for (i32 j = 0; j < 3; ++j)
            {
                mel__qr_set(g, fn, size, i, size - 11 + j, false);
                mel__qr_set(g, fn, size, size - 11 + j, i, false);
            }
        }
    }
}
