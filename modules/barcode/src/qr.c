#include <barcode/qr.h>

#include <barcode/bitwriter.h>
#include <barcode/galois.h>
#include <barcode/rs.h>

#include "qr_internal.h"

#include <string.h>

mel_qr_ecc mel_qr_ecc_l(void) { return (mel_qr_ecc){ 0 }; }
mel_qr_ecc mel_qr_ecc_m(void) { return (mel_qr_ecc){ 1 }; }
mel_qr_ecc mel_qr_ecc_q(void) { return (mel_qr_ecc){ 2 }; }
mel_qr_ecc mel_qr_ecc_h(void) { return (mel_qr_ecc){ 3 }; }

static i32 mel__qr_alnum_value(char c)
{
    const char* p = strchr(MEL__QR_ALNUM, c);
    return (p == NULL || c == '\0') ? -1 : (i32)(p - MEL__QR_ALNUM);
}

static i32 mel__qr_mode(const char* s, usize n)
{
    bool numeric = true;
    bool alnum = true;
    for (usize i = 0; i < n; ++i)
    {
        if (s[i] < '0' || s[i] > '9')
        {
            numeric = false;
        }
        if (mel__qr_alnum_value(s[i]) < 0)
        {
            alnum = false;
        }
    }
    return numeric ? 0 : (alnum ? 1 : 2);
}

static usize mel__qr_data_bits(i32 mode, usize n)
{
    if (mode == 0)
    {
        usize r = n % 3;
        return (n / 3) * 10 + (r == 2 ? 7 : (r == 1 ? 4 : 0));
    }
    if (mode == 1)
    {
        return (n / 2) * 11 + ((n % 2) ? 6 : 0);
    }
    return n * 8;
}

static void mel__qr_emit_data(mel_bitwriter* w, i32 mode, const char* data, usize n)
{
    if (mode == 0)
    {
        usize i = 0;
        for (; i + 3 <= n; i += 3)
        {
            u32 v = (u32)((data[i] - '0') * 100 + (data[i + 1] - '0') * 10 + (data[i + 2] - '0'));
            mel_bitwriter_put(w, v, 10);
        }
        usize r = n - i;
        if (r == 2)
        {
            mel_bitwriter_put(w, (u32)((data[i] - '0') * 10 + (data[i + 1] - '0')), 7);
        }
        else if (r == 1)
        {
            mel_bitwriter_put(w, (u32)(data[i] - '0'), 4);
        }
    }
    else if (mode == 1)
    {
        usize i = 0;
        for (; i + 2 <= n; i += 2)
        {
            i32 hi = mel__qr_alnum_value(data[i]);
            i32 lo = mel__qr_alnum_value(data[i + 1]);
            mel_bitwriter_put(w, (u32)(45 * hi + lo), 11);
        }
        if (n % 2)
        {
            mel_bitwriter_put(w, (u32)mel__qr_alnum_value(data[n - 1]), 6);
        }
    }
    else
    {
        for (usize i = 0; i < n; ++i)
        {
            mel_bitwriter_put(w, (u8)data[i], 8);
        }
    }
}

bool mel_qr_codewords(const char* data, mel_qr_ecc ecc, i32 version_hint, const Mel_Alloc* a, u8** out_cw, usize* out_count, i32* out_version)
{
    if (data == NULL || out_cw == NULL || out_count == NULL || out_version == NULL)
    {
        return false;
    }
    i32 rank = ecc.rank;
    if (rank < 0 || rank > 3 || version_hint > MEL__QR_MAXV)
    {
        return false;
    }

    usize n = strlen(data);
    i32   mode = mel__qr_mode(data, n);

    i32 start = version_hint > 0 ? version_hint : 1;
    i32 end = version_hint > 0 ? version_hint : MEL__QR_MAXV;
    i32 v = 0;
    for (i32 t = start; t <= end; ++t)
    {
        usize need = 4 + (usize)mel__qr_count_bits(t, mode) + mel__qr_data_bits(mode, n);
        if (need <= (usize)mel__qr_data_cw(t, rank) * 8)
        {
            v = t;
            break;
        }
    }
    if (v == 0)
    {
        return false;
    }

    i32 dcw_total = mel__qr_data_cw(v, rank);

    mel_bitwriter w;
    mel_bitwriter_init(&w, a);
    static const u32 mode_indicator[3] = { 0x1, 0x2, 0x4 };
    mel_bitwriter_put(&w, mode_indicator[mode], 4);
    mel_bitwriter_put(&w, (u32)n, (u32)mel__qr_count_bits(v, mode));
    mel__qr_emit_data(&w, mode, data, n);

    usize cap_bits = (usize)dcw_total * 8;
    usize term = cap_bits - mel_bitwriter_bit_length(&w);
    if (term > 4)
    {
        term = 4;
    }
    mel_bitwriter_put(&w, 0, (u32)term);
    mel_bitwriter_pad_to_byte(&w);
    u32 pad = 0xEC;
    while (mel_bitwriter_byte_count(&w) < (usize)dcw_total)
    {
        mel_bitwriter_put(&w, pad, 8);
        pad = (pad == 0xEC) ? 0x11 : 0xEC;
    }

    const u8*  dcw = mel_bitwriter_bytes(&w);
    const i32* e = MEL__QR_EC[v - 1][rank];
    i32        ecw = e[0], g1 = e[1], g1d = e[2], g2 = e[3], g2d = e[4];
    i32        nblocks = g1 + g2;
    i32        total = MEL__QR_TOTAL[v - 1];

    mel_gf f;
    if (!mel_gf_binary_init(&f, 256, 0x11D, a))
    {
        mel_bitwriter_free(&w);
        return false;
    }

    i32  maxd = g1d > g2d ? g1d : g2d;
    u8*  ec = mel_alloc(a, (usize)nblocks * (usize)ecw);
    u16* tmpd = mel_alloc(a, sizeof(u16) * (usize)maxd);
    u16* tmpe = mel_alloc(a, sizeof(u16) * (usize)ecw);
    u8*  out = mel_alloc(a, (usize)total);
    if (ec == NULL || tmpd == NULL || tmpe == NULL || out == NULL)
    {
        mel_dealloc(a, ec);
        mel_dealloc(a, tmpd);
        mel_dealloc(a, tmpe);
        mel_dealloc(a, out);
        mel_gf_free(&f);
        mel_bitwriter_free(&w);
        return false;
    }

    for (i32 b = 0; b < nblocks; ++b)
    {
        i32 len = (b < g1) ? g1d : g2d;
        i32 off = (b < g1) ? b * g1d : g1 * g1d + (b - g1) * g2d;
        for (i32 c = 0; c < len; ++c)
        {
            tmpd[c] = dcw[off + c];
        }
        mel_rs_generate(&f, 2, 0, tmpd, (usize)len, (usize)ecw, tmpe);
        for (i32 c = 0; c < ecw; ++c)
        {
            ec[b * ecw + c] = (u8)tmpe[c];
        }
    }

    i32 idx = 0;
    for (i32 c = 0; c < maxd; ++c)
    {
        for (i32 b = 0; b < nblocks; ++b)
        {
            i32 len = (b < g1) ? g1d : g2d;
            i32 off = (b < g1) ? b * g1d : g1 * g1d + (b - g1) * g2d;
            if (c < len)
            {
                out[idx++] = dcw[off + c];
            }
        }
    }
    for (i32 c = 0; c < ecw; ++c)
    {
        for (i32 b = 0; b < nblocks; ++b)
        {
            out[idx++] = ec[b * ecw + c];
        }
    }

    mel_dealloc(a, ec);
    mel_dealloc(a, tmpd);
    mel_dealloc(a, tmpe);
    mel_gf_free(&f);
    mel_bitwriter_free(&w);

    *out_cw = out;
    *out_count = (usize)total;
    *out_version = v;
    return true;
}

static void mel__qr_place_data(u8* g, const u8* fn, i32 size, const u8* cw, usize count)
{
    usize bit = 0;
    usize total = count * 8;
    for (i32 right = size - 1; right >= 1; right -= 2)
    {
        if (right == 6)
        {
            right = 5;
        }
        for (i32 vert = 0; vert < size; ++vert)
        {
            for (i32 j = 0; j < 2; ++j)
            {
                i32  col = right - j;
                bool up = ((right + 1) & 2) == 0;
                i32  row = up ? (size - 1 - vert) : vert;
                if (!fn[row * size + col] && bit < total)
                {
                    g[row * size + col] = mel__qr_bit(cw[bit >> 3], 7 - (i32)(bit & 7));
                    bit += 1;
                }
            }
        }
    }
}

static void mel__qr_apply_mask(u8* g, const u8* fn, i32 size, i32 mask)
{
    for (i32 r = 0; r < size; ++r)
    {
        for (i32 c = 0; c < size; ++c)
        {
            if (!fn[r * size + c] && mel__qr_mask(mask, r, c))
            {
                g[r * size + c] ^= 1;
            }
        }
    }
}

static void mel__qr_write_format(u8* g, i32 size, i32 rank, i32 mask)
{
    static const i32 ind[4] = { 1, 0, 3, 2 };
    u32              data = (u32)((ind[rank] << 3) | mask);
    u32              rem = data;
    for (i32 i = 0; i < 10; ++i)
    {
        rem = (rem << 1) ^ (((rem >> 9) & 1u) * 0x537u);
    }
    u32 bits = ((data << 10) | rem) ^ 0x5412u;

    for (i32 i = 0; i <= 5; ++i)
    {
        g[i * size + 8] = mel__qr_bit(bits, i);
    }
    g[7 * size + 8] = mel__qr_bit(bits, 6);
    g[8 * size + 8] = mel__qr_bit(bits, 7);
    g[8 * size + 7] = mel__qr_bit(bits, 8);
    for (i32 i = 9; i < 15; ++i)
    {
        g[8 * size + (14 - i)] = mel__qr_bit(bits, i);
    }
    for (i32 i = 0; i < 8; ++i)
    {
        g[8 * size + (size - 1 - i)] = mel__qr_bit(bits, i);
    }
    for (i32 i = 8; i < 15; ++i)
    {
        g[(size - 15 + i) * size + 8] = mel__qr_bit(bits, i);
    }
}

static void mel__qr_write_version(u8* g, i32 size, i32 version)
{
    if (version < 7)
    {
        return;
    }
    u32 rem = (u32)version;
    for (i32 i = 0; i < 12; ++i)
    {
        rem = (rem << 1) ^ (((rem >> 11) & 1u) * 0x1F25u);
    }
    u32 bits = ((u32)version << 12) | rem;
    for (i32 i = 0; i < 18; ++i)
    {
        u8  b = mel__qr_bit(bits, i);
        i32 a = size - 11 + i % 3;
        i32 d = i / 3;
        g[d * size + a] = b;
        g[a * size + d] = b;
    }
}

static i32 mel__qr_penalty(const u8* g, i32 size)
{
    i32 pen = 0;
    for (i32 r = 0; r < size; ++r)
    {
        i32 run = 1;
        for (i32 c = 1; c < size; ++c)
        {
            if (g[r * size + c] == g[r * size + c - 1])
            {
                run += 1;
            }
            else
            {
                if (run >= 5)
                {
                    pen += 3 + (run - 5);
                }
                run = 1;
            }
        }
        if (run >= 5)
        {
            pen += 3 + (run - 5);
        }
    }
    for (i32 c = 0; c < size; ++c)
    {
        i32 run = 1;
        for (i32 r = 1; r < size; ++r)
        {
            if (g[r * size + c] == g[(r - 1) * size + c])
            {
                run += 1;
            }
            else
            {
                if (run >= 5)
                {
                    pen += 3 + (run - 5);
                }
                run = 1;
            }
        }
        if (run >= 5)
        {
            pen += 3 + (run - 5);
        }
    }
    for (i32 r = 0; r < size - 1; ++r)
    {
        for (i32 c = 0; c < size - 1; ++c)
        {
            u8 v = g[r * size + c];
            if (v == g[r * size + c + 1] && v == g[(r + 1) * size + c] && v == g[(r + 1) * size + c + 1])
            {
                pen += 3;
            }
        }
    }
    static const u8 P[11] = { 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0 };
    for (i32 r = 0; r < size; ++r)
    {
        for (i32 c = 0; c <= size - 11; ++c)
        {
            bool fwd = true, bwd = true;
            for (i32 k = 0; k < 11; ++k)
            {
                if (g[r * size + c + k] != P[k])
                {
                    fwd = false;
                }
                if (g[r * size + c + k] != P[10 - k])
                {
                    bwd = false;
                }
            }
            if (fwd || bwd)
            {
                pen += 40;
            }
        }
    }
    for (i32 c = 0; c < size; ++c)
    {
        for (i32 r = 0; r <= size - 11; ++r)
        {
            bool fwd = true, bwd = true;
            for (i32 k = 0; k < 11; ++k)
            {
                if (g[(r + k) * size + c] != P[k])
                {
                    fwd = false;
                }
                if (g[(r + k) * size + c] != P[10 - k])
                {
                    bwd = false;
                }
            }
            if (fwd || bwd)
            {
                pen += 40;
            }
        }
    }
    i32 dark = 0;
    for (i32 i = 0; i < size * size; ++i)
    {
        dark += g[i];
    }
    i32 total = size * size;
    i32 ratio = dark * 100 / total;
    i32 dev = ratio > 50 ? ratio - 50 : 50 - ratio;
    pen += (dev / 5) * 10;
    return pen;
}

bool mel_qr_encode(mel_barcode_matrix* out, const char* data, mel_qr_opt opt, const Mel_Alloc* a)
{
    u8*   cw = NULL;
    usize count = 0;
    i32   version = 0;
    if (!mel_qr_codewords(data, opt.ecc, opt.version, a, &cw, &count, &version))
    {
        return false;
    }

    i32   size = 4 * version + 17;
    usize area = (usize)size * (usize)size;
    u8*   base = mel_calloc(a, area);
    u8*   fn = mel_calloc(a, area);
    u8*   scratch = mel_alloc(a, area);
    u8*   best = mel_alloc(a, area);
    if (base == NULL || fn == NULL || scratch == NULL || best == NULL)
    {
        mel_dealloc(a, base);
        mel_dealloc(a, fn);
        mel_dealloc(a, scratch);
        mel_dealloc(a, best);
        mel_dealloc(a, cw);
        return false;
    }

    mel__qr_draw_function(base, fn, size, version);
    mel__qr_place_data(base, fn, size, cw, count);
    mel_dealloc(a, cw);

    i32  rank = opt.ecc.rank;
    bool fixed = opt.mask >= 0 && opt.mask < 8;
    i32  mstart = fixed ? opt.mask : 0;
    i32  mend = fixed ? opt.mask : 7;
    i32  best_pen = -1;
    for (i32 m = mstart; m <= mend; ++m)
    {
        memcpy(scratch, base, area);
        mel__qr_apply_mask(scratch, fn, size, m);
        mel__qr_write_format(scratch, size, rank, m);
        mel__qr_write_version(scratch, size, version);
        i32 pen = mel__qr_penalty(scratch, size);
        if (best_pen < 0 || pen < best_pen)
        {
            best_pen = pen;
            memcpy(best, scratch, area);
        }
    }

    bool ok = mel_barcode_matrix_init(out, size, size, a);
    if (ok)
    {
        out->quiet_zone = 4;
        memcpy(out->modules, best, area);
    }

    mel_dealloc(a, base);
    mel_dealloc(a, fn);
    mel_dealloc(a, scratch);
    mel_dealloc(a, best);
    return ok;
}
