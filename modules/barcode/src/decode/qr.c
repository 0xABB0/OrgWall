#include <barcode/qr.h>

#include <barcode/bitreader.h>
#include <barcode/galois.h>
#include <barcode/rs.h>

#include "../qr_internal.h"

static const char MEL__QR_LEVEL_RANK[4] = { 1, 0, 3, 2 };

static i32 mel__qr_rank_from_level(i32 level)
{
    for (i32 r = 0; r < 4; ++r)
    {
        if (MEL__QR_LEVEL_RANK[r] == level)
        {
            return r;
        }
    }
    return -1;
}

static i32 mel__qr_popcount15(u32 v)
{
    i32 n = 0;
    for (i32 i = 0; i < 15; ++i)
    {
        n += (i32)((v >> i) & 1u);
    }
    return n;
}

static i32 mel__qr_format_correct(u32 raw, i32* out_rank, i32* out_mask)
{
    i32 best_data = -1;
    i32 best_dist = 16;
    for (u32 d = 0; d < 32; ++d)
    {
        i32 dist = mel__qr_popcount15(raw ^ MEL__QR_FORMAT_CODES[d]);
        if (dist < best_dist)
        {
            best_dist = dist;
            best_data = (i32)d;
        }
    }
    if (best_data < 0 || best_dist > 3)
    {
        return -1;
    }
    i32 rank = mel__qr_rank_from_level(best_data >> 3);
    if (rank < 0)
    {
        return -1;
    }
    *out_rank = rank;
    *out_mask = best_data & 7;
    return best_dist;
}

static u32 mel__qr_read_format_a(const u8* g, i32 size)
{
    u32 bits = 0;
    for (i32 i = 0; i <= 5; ++i)
    {
        bits |= (u32)g[i * size + 8] << i;
    }
    bits |= (u32)g[7 * size + 8] << 6;
    bits |= (u32)g[8 * size + 8] << 7;
    bits |= (u32)g[8 * size + 7] << 8;
    for (i32 i = 9; i < 15; ++i)
    {
        bits |= (u32)g[8 * size + (14 - i)] << i;
    }
    return bits;
}

static u32 mel__qr_read_format_b(const u8* g, i32 size)
{
    u32 bits = 0;
    for (i32 i = 0; i < 8; ++i)
    {
        bits |= (u32)g[8 * size + (size - 1 - i)] << i;
    }
    for (i32 i = 8; i < 15; ++i)
    {
        bits |= (u32)g[(size - 15 + i) * size + 8] << i;
    }
    return bits;
}

static void mel__qr_read_data(const u8* g, const u8* fn, i32 size, i32 mask, u8* cw, usize count)
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
                    u8 d = g[row * size + col];
                    if (mel__qr_mask(mask, row, col))
                    {
                        d ^= 1;
                    }
                    cw[bit >> 3] |= (u8)(d << (7 - (i32)(bit & 7)));
                    bit += 1;
                }
            }
        }
    }
}

static i32 mel__qr_alnum_at(i32 v)
{
    if (v < 0 || v >= (i32)(sizeof(MEL__QR_ALNUM) - 1))
    {
        return -1;
    }
    return (u8)MEL__QR_ALNUM[v];
}

static bool mel__qr_parse_segments(const u8* data, usize ndata, i32 version, char** out_text, usize* out_len, const Mel_Alloc* a)
{
    mel_bitreader r;
    mel_bitreader_init(&r, data, ndata);

    usize cap = ndata * 3 + 16;
    char* text = mel_alloc(a, cap);
    if (text == NULL)
    {
        return false;
    }
    usize produced = 0;

    bool ok = true;
    for (;;)
    {
        if (mel_bitreader_remaining(&r) < 4)
        {
            break;
        }
        u32 mode = mel_bitreader_get(&r, 4);
        if (mode == 0)
        {
            break;
        }

        i32 m;
        if (mode == 1)
        {
            m = 0;
        }
        else if (mode == 2)
        {
            m = 1;
        }
        else if (mode == 4)
        {
            m = 2;
        }
        else
        {
            ok = false;
            break;
        }

        u32   cbits = (u32)mel__qr_count_bits(version, m);
        usize n = mel_bitreader_get(&r, cbits);

        usize payload_bits;
        if (m == 0)
        {
            usize r3 = n % 3;
            payload_bits = (n / 3) * 10 + (r3 == 2 ? 7 : (r3 == 1 ? 4 : 0));
        }
        else if (m == 1)
        {
            payload_bits = (n / 2) * 11 + ((n % 2) ? 6 : 0);
        }
        else
        {
            payload_bits = n * 8;
        }
        if (payload_bits > mel_bitreader_remaining(&r))
        {
            ok = false;
            break;
        }

        if (produced + n + 16 > cap)
        {
            usize newcap = (produced + n + 16) * 2;
            char* grown = mel_realloc(a, text, newcap);
            if (grown == NULL)
            {
                ok = false;
                break;
            }
            text = grown;
            cap = newcap;
        }

        if (m == 0)
        {
            usize i = 0;
            for (; i + 3 <= n; i += 3)
            {
                u32 v = mel_bitreader_get(&r, 10);
                if (v > 999)
                {
                    ok = false;
                    break;
                }
                text[produced++] = (char)('0' + v / 100);
                text[produced++] = (char)('0' + (v / 10) % 10);
                text[produced++] = (char)('0' + v % 10);
            }
            if (!ok)
            {
                break;
            }
            usize rem = n - i;
            if (rem == 2)
            {
                u32 v = mel_bitreader_get(&r, 7);
                if (v > 99)
                {
                    ok = false;
                    break;
                }
                text[produced++] = (char)('0' + v / 10);
                text[produced++] = (char)('0' + v % 10);
            }
            else if (rem == 1)
            {
                u32 v = mel_bitreader_get(&r, 4);
                if (v > 9)
                {
                    ok = false;
                    break;
                }
                text[produced++] = (char)('0' + v);
            }
        }
        else if (m == 1)
        {
            usize i = 0;
            for (; i + 2 <= n; i += 2)
            {
                u32 v = mel_bitreader_get(&r, 11);
                i32 hi = mel__qr_alnum_at((i32)(v / 45));
                i32 lo = mel__qr_alnum_at((i32)(v % 45));
                if (hi < 0 || lo < 0)
                {
                    ok = false;
                    break;
                }
                text[produced++] = (char)hi;
                text[produced++] = (char)lo;
            }
            if (!ok)
            {
                break;
            }
            if (n % 2)
            {
                u32 v = mel_bitreader_get(&r, 6);
                i32 c = mel__qr_alnum_at((i32)v);
                if (c < 0)
                {
                    ok = false;
                    break;
                }
                text[produced++] = (char)c;
            }
        }
        else
        {
            for (usize i = 0; i < n; ++i)
            {
                u32 v = mel_bitreader_get(&r, 8);
                text[produced++] = (char)(u8)v;
            }
        }
    }

    if (!ok)
    {
        mel_dealloc(a, text);
        return false;
    }

    text[produced] = '\0';
    *out_text = text;
    *out_len = produced;
    return true;
}

bool mel_qr_decode_gf(const mel_barcode_matrix* in, mel_qr_decoded* out, mel_gf* gf, const Mel_Alloc* a)
{
    if (in == NULL || out == NULL || a == NULL || in->modules == NULL)
    {
        return false;
    }
    i32 size = in->width;
    if (size != in->height || size < 21)
    {
        return false;
    }
    if ((size - 17) % 4 != 0)
    {
        return false;
    }
    i32 version = (size - 17) / 4;
    if (version < 1 || version > MEL__QR_MAXV)
    {
        return false;
    }

    const u8* g = in->modules;

    i32 rank = -1, mask = -1;
    if (mel__qr_format_correct(mel__qr_read_format_a(g, size), &rank, &mask) < 0)
    {
        i32 rank_b = -1, mask_b = -1;
        if (mel__qr_format_correct(mel__qr_read_format_b(g, size), &rank_b, &mask_b) < 0)
        {
            return false;
        }
        rank = rank_b;
        mask = mask_b;
    }

    usize area = (usize)size * (usize)size;
    u8*   fn = mel_calloc(a, area);
    if (fn == NULL)
    {
        return false;
    }
    mel__qr_draw_function(NULL, fn, size, version);

    i32   total = MEL__QR_TOTAL[version - 1];
    u8*   raw = mel_calloc(a, (usize)total);
    if (raw == NULL)
    {
        mel_dealloc(a, fn);
        return false;
    }
    mel__qr_read_data(g, fn, size, mask, raw, (usize)total);
    mel_dealloc(a, fn);

    const i32* e = MEL__QR_EC[version - 1][rank];
    i32        ecw = e[0], g1 = e[1], g1d = e[2], g2 = e[3], g2d = e[4];
    i32        nblocks = g1 + g2;
    i32        maxd = g1d > g2d ? g1d : g2d;
    i32        dcw_total = mel__qr_data_cw(version, rank);

    u8* dcw = mel_alloc(a, (usize)dcw_total);
    u8* ec = mel_alloc(a, (usize)nblocks * (usize)ecw);
    if (dcw == NULL || ec == NULL)
    {
        mel_dealloc(a, raw);
        mel_dealloc(a, dcw);
        mel_dealloc(a, ec);
        return false;
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
                dcw[off + c] = raw[idx++];
            }
        }
    }
    for (i32 c = 0; c < ecw; ++c)
    {
        for (i32 b = 0; b < nblocks; ++b)
        {
            ec[b * ecw + c] = raw[idx++];
        }
    }
    mel_dealloc(a, raw);

    mel_gf  local_gf;
    mel_gf* f = gf;
    bool    own_gf = false;
    if (f == NULL)
    {
        if (!mel_gf_binary_init(&local_gf, 256, 0x11D, a))
        {
            mel_dealloc(a, dcw);
            mel_dealloc(a, ec);
            return false;
        }
        f = &local_gf;
        own_gf = true;
    }

    i32  ntotal = maxd + ecw;
    u16* block = mel_alloc(a, sizeof(u16) * (usize)ntotal);
    if (block == NULL)
    {
        if (own_gf)
        {
            mel_gf_free(f);
        }
        mel_dealloc(a, dcw);
        mel_dealloc(a, ec);
        return false;
    }

    bool ok = true;
    for (i32 b = 0; b < nblocks && ok; ++b)
    {
        i32 len = (b < g1) ? g1d : g2d;
        i32 off = (b < g1) ? b * g1d : g1 * g1d + (b - g1) * g2d;
        i32 nt = len + ecw;
        for (i32 c = 0; c < len; ++c)
        {
            block[c] = dcw[off + c];
        }
        for (i32 c = 0; c < ecw; ++c)
        {
            block[len + c] = ec[b * ecw + c];
        }
        usize corrected = 0;
        if (!mel_rs_decode(f, 2, 0, block, (usize)nt, (usize)ecw, NULL, 0, &corrected, a))
        {
            ok = false;
            break;
        }
        for (i32 c = 0; c < len; ++c)
        {
            dcw[off + c] = (u8)block[c];
        }
    }

    mel_dealloc(a, block);
    if (own_gf)
    {
        mel_gf_free(f);
    }
    mel_dealloc(a, ec);

    if (!ok)
    {
        mel_dealloc(a, dcw);
        return false;
    }

    char* text = NULL;
    usize len = 0;
    if (!mel__qr_parse_segments(dcw, (usize)dcw_total, version, &text, &len, a))
    {
        mel_dealloc(a, dcw);
        return false;
    }
    mel_dealloc(a, dcw);

    out->text = text;
    out->len = len;
    out->version = version;
    out->ecc = (mel_qr_ecc){ (u8)rank };
    out->mask = mask;
    return true;
}

bool mel_qr_decode(const mel_barcode_matrix* in, mel_qr_decoded* out, const Mel_Alloc* a) { return mel_qr_decode_gf(in, out, NULL, a); }

void mel_qr_decoded_free(mel_qr_decoded* d, const Mel_Alloc* a)
{
    if (d == NULL)
    {
        return;
    }
    if (d->text != NULL && a != NULL)
    {
        mel_dealloc(a, d->text);
    }
    d->text = NULL;
    d->len = 0;
}
