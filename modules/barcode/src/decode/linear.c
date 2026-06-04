#include <barcode/decode.h>

#include <allocator/allocator.h>

#include <string.h>

typedef struct
{
    i32* widths;
    i32* values;
    i32  values_cap;
    bool first_dark;
    i32  count;
    i32  capacity;
    i32  x_origin;
} Runs;

typedef struct
{
    const char* symbology;
    char*       text;
    i32         text_len;
    i32         elem_first;
    i32         elem_count;
    bool        ok;
} Decoded;

static i32 mel__row_threshold(const u8* restrict row, i32 w)
{
    i32 lo = 255;
    i32 hi = 0;
    for (i32 x = 0; x < w; ++x)
    {
        i32 v = row[x];
        if (v < lo)
            lo = v;
        if (v > hi)
            hi = v;
    }
    if (hi - lo < 24)
        return -1;

    i32 hist[256];
    memset(hist, 0, sizeof hist);
    for (i32 x = 0; x < w; ++x)
        hist[row[x]] += 1;

    i64 total = w;
    i64 sum_all = 0;
    for (i32 i = 0; i < 256; ++i)
        sum_all += (i64)i * hist[i];

    i64 sum_bg = 0;
    i64 weight_bg = 0;
    i64 best_var = -1;
    i32 best_t = (lo + hi) / 2;
    for (i32 t = 0; t < 256; ++t)
    {
        weight_bg += hist[t];
        if (weight_bg == 0)
            continue;
        i64 weight_fg = total - weight_bg;
        if (weight_fg == 0)
            break;
        sum_bg += (i64)t * hist[t];
        i64 mean_bg_num = sum_bg;
        i64 mean_fg_num = sum_all - sum_bg;
        f64 mean_bg = (f64)mean_bg_num / (f64)weight_bg;
        f64 mean_fg = (f64)mean_fg_num / (f64)weight_fg;
        f64 diff = mean_bg - mean_fg;
        f64 var = (f64)weight_bg * (f64)weight_fg * diff * diff;
        if ((i64)var > best_var)
        {
            best_var = (i64)var;
            best_t = t;
        }
    }
    return best_t;
}

static bool mel__runlength(const u8* restrict row, i32 w, i32 threshold, Runs* restrict runs)
{
    runs->count = 0;
    runs->first_dark = true;

    i32 lead = 0;
    while (lead < w && !(row[lead] <= threshold))
        lead += 1;
    if (lead >= w)
        return false;
    runs->x_origin = lead;

    i32  start = lead;
    bool dark = true;
    for (i32 x = lead; x < w; ++x)
    {
        bool d = row[x] <= threshold;
        if (d != dark)
        {
            if (runs->count >= runs->capacity)
                return false;
            runs->widths[runs->count++] = x - start;
            start = x;
            dark = d;
        }
    }
    if (dark)
    {
        if (runs->count >= runs->capacity)
            return false;
        runs->widths[runs->count++] = w - start;
    }

    return runs->count >= 3 && (runs->count % 2) == 1;
}

static i32 mel__sum_runs(const Runs* r, i32 first, i32 count)
{
    i32 s = 0;
    for (i32 i = 0; i < count; ++i)
        s += r->widths[first + i];
    return s;
}

static i32 mel__elem_x(const Runs* r, i32 first)
{
    i32 x = r->x_origin;
    for (i32 i = 0; i < first; ++i)
        x += r->widths[i];
    return x;
}

static const u8 MEL__EAN_FIRST[10] = { 0x00, 0x0B, 0x0D, 0x0E, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A };

static const i32 MEL__EAN_L_W[10][4] = {
    { 3, 2, 1, 1 }, { 2, 2, 2, 1 }, { 2, 1, 2, 2 }, { 1, 4, 1, 1 }, { 1, 1, 3, 2 }, { 1, 2, 3, 1 }, { 1, 1, 1, 4 }, { 1, 3, 1, 2 }, { 1, 2, 1, 3 }, { 3, 1, 1, 2 },
};
static const i32 MEL__EAN_G_W[10][4] = {
    { 1, 1, 2, 3 }, { 1, 2, 2, 2 }, { 2, 2, 1, 2 }, { 1, 1, 4, 1 }, { 2, 3, 1, 1 }, { 1, 3, 2, 1 }, { 4, 1, 1, 1 }, { 2, 1, 3, 1 }, { 3, 1, 2, 1 }, { 2, 1, 1, 3 },
};

static f64 mel__cell_err(const i32* restrict widths, const i32* restrict ref, f64 module_px)
{
    f64 err = 0.0;
    for (i32 k = 0; k < 4; ++k)
    {
        f64 diff = (f64)widths[k] - (f64)ref[k] * module_px;
        err += diff * diff;
    }
    return err;
}

static i32 mel__match_digit_cell(const i32* restrict widths, i32 n, f64 module_px)
{
    if (n != 4)
        return -1;
    i32 best = -1;
    f64 best_err = 1e18;
    for (i32 d = 0; d < 10; ++d)
    {
        f64 err = mel__cell_err(widths, MEL__EAN_L_W[d], module_px);
        if (err < best_err)
        {
            best_err = err;
            best = d;
        }
    }
    f64 tol = module_px * module_px * 4.0;
    if (best_err > tol)
        return -1;
    return best;
}

static i32 mel__match_digit_lg(const i32* restrict widths, i32 n, f64 module_px, i32* out_is_g)
{
    if (n != 4)
        return -1;
    i32 best = -1;
    i32 best_g = 0;
    f64 best_err = 1e18;
    for (i32 g = 0; g < 2; ++g)
    {
        const i32(*table)[4] = g ? MEL__EAN_G_W : MEL__EAN_L_W;
        for (i32 d = 0; d < 10; ++d)
        {
            f64 err = mel__cell_err(widths, table[d], module_px);
            if (err < best_err)
            {
                best_err = err;
                best = d;
                best_g = g;
            }
        }
    }
    f64 tol = module_px * module_px * 4.0;
    if (best_err > tol)
        return -1;
    *out_is_g = best_g;
    return best;
}

static i32 mel__ean_checkdigit(const char* digits, i32 n)
{
    i32 sum = 0;
    for (i32 i = 0; i < n; ++i)
    {
        i32 d = digits[i] - '0';
        i32 weight = (((n - 1 - i) & 1) == 0) ? 3 : 1;
        sum += d * weight;
    }
    return (10 - (sum % 10)) % 10;
}

static bool mel__decode_ean13(const Runs* r, const Mel_Alloc* a, Decoded* out)
{
    if (r->count != 59)
        return false;
    i32 total = mel__sum_runs(r, 0, r->count);
    if (total <= 0)
        return false;
    f64 module_px = (f64)total / 95.0;
    if (module_px < 0.6)
        return false;

    i32 idx = 0;
    if (r->widths[0] < module_px * 0.5 || r->widths[1] < module_px * 0.5 || r->widths[2] < module_px * 0.5)
        return false;
    idx += 3;

    char digits[14];
    i32  parity_bits = 0;
    for (i32 i = 0; i < 6; ++i)
    {
        if (idx + 4 > r->count)
            return false;
        i32 is_g = 0;
        i32 d = mel__match_digit_lg(&r->widths[idx], 4, module_px, &is_g);
        if (d < 0)
            return false;
        digits[1 + i] = (char)('0' + d);
        parity_bits = (parity_bits << 1) | (is_g ? 1 : 0);
        idx += 4;
    }

    idx += 5;
    if (idx > r->count)
        return false;

    for (i32 i = 0; i < 6; ++i)
    {
        if (idx + 4 > r->count)
            return false;
        i32 d = mel__match_digit_cell(&r->widths[idx], 4, module_px);
        if (d < 0)
            return false;
        digits[7 + i] = (char)('0' + d);
        idx += 4;
    }

    i32 first = -1;
    for (i32 v = 0; v < 10; ++v)
    {
        if (MEL__EAN_FIRST[v] == (u8)parity_bits)
        {
            first = v;
            break;
        }
    }
    if (first < 0)
        return false;
    digits[0] = (char)('0' + first);
    digits[13] = '\0';

    i32 cd = mel__ean_checkdigit(digits, 12);
    if (cd != digits[12] - '0')
        return false;

    out->text = (char*)mel_alloc(a, 14);
    if (!out->text)
        return false;
    memcpy(out->text, digits, 14);
    out->text_len = 13;
    out->symbology = "EAN-13";
    out->elem_first = 0;
    out->elem_count = r->count;
    out->ok = true;
    return true;
}

static bool mel__decode_ean8(const Runs* r, const Mel_Alloc* a, Decoded* out)
{
    if (r->count != 43)
        return false;
    i32 total = mel__sum_runs(r, 0, r->count);
    if (total <= 0)
        return false;
    f64 module_px = (f64)total / 67.0;
    if (module_px < 0.6)
        return false;

    i32  idx = 3;
    char digits[9];
    for (i32 i = 0; i < 4; ++i)
    {
        if (idx + 4 > r->count)
            return false;
        i32 d = mel__match_digit_cell(&r->widths[idx], 4, module_px);
        if (d < 0)
            return false;
        digits[i] = (char)('0' + d);
        idx += 4;
    }
    idx += 5;
    for (i32 i = 0; i < 4; ++i)
    {
        if (idx + 4 > r->count)
            return false;
        i32 d = mel__match_digit_cell(&r->widths[idx], 4, module_px);
        if (d < 0)
            return false;
        digits[4 + i] = (char)('0' + d);
        idx += 4;
    }
    digits[8] = '\0';
    i32 cd = mel__ean_checkdigit(digits, 7);
    if (cd != digits[7] - '0')
        return false;

    out->text = (char*)mel_alloc(a, 9);
    if (!out->text)
        return false;
    memcpy(out->text, digits, 9);
    out->text_len = 8;
    out->symbology = "EAN-8";
    out->elem_first = 0;
    out->elem_count = r->count;
    out->ok = true;
    return true;
}

static const char        MEL__C39_SET[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. $/+%";
static const char* const MEL__C39_PAT[43] = {
    "000110100", "100100001", "001100001", "101100000", "000110001", "100110000", "001110000", "000100101", "100100100", "001100100", "100001001", "001001001", "101001000", "000011001", "100011000",
    "001011000", "000001101", "100001100", "001001100", "000011100", "100000011", "001000011", "101000010", "000010011", "100010010", "001010010", "000000111", "100000110", "001000110", "000010110",
    "110000001", "011000001", "111000000", "010010001", "110010000", "011010000", "010000101", "110000100", "011000100", "010101000", "010100010", "010001010", "000101010",
};
static const char* const MEL__C39_STAR = "010010100";

static i32 mel__c39_match(const i32* restrict widths, f64 narrow_px)
{
    char pat[10];
    for (i32 k = 0; k < 9; ++k)
    {
        f64 ratio = (f64)widths[k] / narrow_px;
        if (ratio < 2.0)
            pat[k] = '0';
        else
            pat[k] = '1';
    }
    pat[9] = '\0';
    if (strcmp(pat, MEL__C39_STAR) == 0)
        return -2;
    for (i32 i = 0; i < 43; ++i)
        if (strcmp(pat, MEL__C39_PAT[i]) == 0)
            return i;
    return -1;
}

static bool mel__decode_code39(const Runs* r, const Mel_Alloc* a, Decoded* out)
{
    if (r->count < 9 + 1 + 9)
        return false;
    if (((r->count + 1) % 10) != 0)
        return false;
    i32 glyphs = (r->count + 1) / 10;
    if (glyphs < 2)
        return false;

    i32 total = mel__sum_runs(r, 0, 9);
    f64 narrow_px = (f64)total / 13.0;
    if (narrow_px < 0.5)
        return false;

    if (mel__c39_match(&r->widths[0], narrow_px) != -2)
        return false;
    i32 last_glyph_first = (glyphs - 1) * 10;
    if (mel__c39_match(&r->widths[last_glyph_first], narrow_px) != -2)
        return false;

    i32   payload = glyphs - 2;
    char* text = (char*)mel_alloc(a, (usize)payload + 1);
    if (!text)
        return false;
    i32 produced = 0;
    for (i32 gi = 1; gi < glyphs - 1; ++gi)
    {
        i32 first = gi * 10;
        i32 v = mel__c39_match(&r->widths[first], narrow_px);
        if (v < 0)
        {
            mel_dealloc(a, text);
            return false;
        }
        text[produced++] = MEL__C39_SET[v];
    }
    text[produced] = '\0';

    out->text = text;
    out->text_len = produced;
    out->symbology = "Code39";
    out->elem_first = 0;
    out->elem_count = r->count;
    out->ok = true;
    return true;
}

static const char* const MEL__ITF_PAT[10] = {
    "00110", "10001", "01001", "11000", "00101", "10100", "01100", "00011", "10010", "01010",
};

static i32 mel__itf_classify(i32 width, f64 narrow_px) { return ((f64)width / narrow_px >= 2.0) ? 1 : 0; }

static bool mel__decode_itf(const Runs* r, const Mel_Alloc* a, Decoded* out)
{
    if (r->count < 4 + 10 + 3)
        return false;
    i32 body = r->count - 7;
    if (body <= 0 || (body % 10) != 0)
        return false;
    i32 pairs = body / 10;

    i32 start_sum = mel__sum_runs(r, 0, 4);
    f64 narrow_px = (f64)start_sum / 4.0;
    if (narrow_px < 0.5)
        return false;
    for (i32 i = 0; i < 4; ++i)
        if (mel__itf_classify(r->widths[i], narrow_px) != 0)
            return false;

    i32 stop_first = r->count - 3;
    if (mel__itf_classify(r->widths[stop_first], narrow_px) != 1)
        return false;
    if (mel__itf_classify(r->widths[stop_first + 1], narrow_px) != 0)
        return false;
    if (mel__itf_classify(r->widths[stop_first + 2], narrow_px) != 0)
        return false;

    char* text = (char*)mel_alloc(a, (usize)pairs * 2 + 1);
    if (!text)
        return false;
    i32 produced = 0;
    for (i32 p = 0; p < pairs; ++p)
    {
        i32  base = 4 + p * 10;
        char bars[6];
        char spaces[6];
        for (i32 k = 0; k < 5; ++k)
        {
            bars[k] = mel__itf_classify(r->widths[base + 2 * k], narrow_px) ? '1' : '0';
            spaces[k] = mel__itf_classify(r->widths[base + 2 * k + 1], narrow_px) ? '1' : '0';
        }
        bars[5] = '\0';
        spaces[5] = '\0';
        i32 db = -1;
        i32 ds = -1;
        for (i32 d = 0; d < 10; ++d)
        {
            if (strcmp(bars, MEL__ITF_PAT[d]) == 0)
                db = d;
            if (strcmp(spaces, MEL__ITF_PAT[d]) == 0)
                ds = d;
        }
        if (db < 0 || ds < 0)
        {
            mel_dealloc(a, text);
            return false;
        }
        text[produced++] = (char)('0' + db);
        text[produced++] = (char)('0' + ds);
    }
    text[produced] = '\0';

    out->text = text;
    out->text_len = produced;
    out->symbology = "ITF";
    out->elem_first = 0;
    out->elem_count = r->count;
    out->ok = true;
    return true;
}

#define MEL__C128_CODE_C  99
#define MEL__C128_CODE_B  100
#define MEL__C128_START_A 103
#define MEL__C128_START_B 104
#define MEL__C128_START_C 105
#define MEL__C128_STOP    106

static const char* const MEL__C128[107] = {
    "212222", "222122", "222221", "121223", "121322", "131222", "122213", "122312", "132212", "221213", "221312", "231212", "112232", "122132", "122231", "113222", "123122", "123221", "223211",  "221132", "221231", "213212",
    "223112", "312131", "311222", "321122", "321221", "312212", "322112", "322211", "212123", "212321", "232121", "111323", "131123", "131321", "112313", "132113", "132311", "211313", "231113",  "231311", "112133", "112331",
    "132131", "113123", "113321", "133121", "313121", "211331", "231131", "213113", "213311", "213131", "311123", "311321", "331121", "312113", "312311", "332111", "314111", "221411", "431111",  "111224", "111422", "121124",
    "121421", "141122", "141221", "112214", "112412", "122114", "122411", "142112", "142211", "241211", "221114", "413111", "241112", "134111", "111242", "121142", "121241", "114212", "124112",  "124211", "411212", "421112",
    "421211", "212141", "214121", "412121", "111143", "111341", "131141", "114113", "114311", "411113", "411311", "113141", "114131", "311141", "411131", "211412", "211214", "211232", "2331112",
};

static i32 mel__c128_match(const i32* restrict widths, i32 n, f64 module_px)
{
    if (n != 6 && n != 7)
        return -1;
    i32 best = -1;
    f64 best_err = 1e18;
    for (i32 sym = 0; sym < 107; ++sym)
    {
        const char* pat = MEL__C128[sym];
        i32         plen = (sym == 106) ? 7 : 6;
        if (plen != n)
            continue;
        f64 err = 0.0;
        for (i32 k = 0; k < n; ++k)
        {
            f64 ref = (f64)(pat[k] - '0');
            f64 diff = (f64)widths[k] - ref * module_px;
            err += diff * diff;
        }
        if (err < best_err)
        {
            best_err = err;
            best = sym;
        }
    }
    f64 tol = module_px * module_px * (f64)n;
    if (best_err > tol)
        return -1;
    return best;
}

static bool mel__decode_code128(const Runs* r, const Mel_Alloc* a, Decoded* out)
{
    if (r->count < 6 + 6 + 7)
        return false;
    if (((r->count - 7) % 6) != 0)
        return false;
    i32 sym_count = (r->count - 7) / 6;
    if (sym_count < 2)
        return false;

    i32 total = mel__sum_runs(r, 0, r->count);
    i32 modules_total = 11 * sym_count + 13;
    f64 module_px = (f64)total / (f64)modules_total;
    if (module_px < 0.4)
        return false;

    i32 stop = mel__c128_match(&r->widths[6 * sym_count], 7, module_px);
    if (stop != MEL__C128_STOP)
        return false;

    i32 start = mel__c128_match(&r->widths[0], 6, module_px);
    if (start != MEL__C128_START_A && start != MEL__C128_START_B && start != MEL__C128_START_C)
        return false;

    if (sym_count > r->values_cap)
        return false;
    i32* values = r->values;
    for (i32 s = 0; s < sym_count; ++s)
    {
        i32 v = mel__c128_match(&r->widths[6 * s], 6, module_px);
        if (v < 0)
            return false;
        values[s] = v;
    }

    i64 sum = values[0];
    for (i32 k = 1; k < sym_count - 1; ++k)
        sum += (i64)k * values[k];
    i32 check_expect = (i32)(sum % 103);
    i32 check_actual = values[sym_count - 1];
    if (check_expect != check_actual)
        return false;

    char* text = (char*)mel_alloc(a, (usize)sym_count * 2 + 1);
    if (!text)
        return false;
    i32  produced = 0;
    bool set_c = (start == MEL__C128_START_C);
    for (i32 s = 1; s < sym_count - 1; ++s)
    {
        i32 v = values[s];
        if (set_c)
        {
            if (v == MEL__C128_CODE_B)
            {
                set_c = false;
                continue;
            }
            if (v < 0 || v > 99)
            {
                mel_dealloc(a, text);
                return false;
            }
            text[produced++] = (char)('0' + v / 10);
            text[produced++] = (char)('0' + v % 10);
        }
        else
        {
            if (v == MEL__C128_CODE_C)
            {
                set_c = true;
                continue;
            }
            if (v < 0 || v > 94)
            {
                mel_dealloc(a, text);
                return false;
            }
            text[produced++] = (char)(v + 32);
        }
    }
    text[produced] = '\0';

    out->text = text;
    out->text_len = produced;
    out->symbology = "Code128";
    out->elem_first = 0;
    out->elem_count = r->count;
    out->ok = true;
    return true;
}

typedef bool (*Mel_Linear_Decoder)(const Runs* r, const Mel_Alloc* a, Decoded* out);

static const Mel_Linear_Decoder MEL__DECODERS[] = {
    mel__decode_ean13, mel__decode_ean8, mel__decode_code128, mel__decode_itf, mel__decode_code39,
};

static bool mel__scan_row(const u8* row, i32 w, Runs* runs, const Mel_Alloc* a, Decoded* out)
{
    i32 threshold = mel__row_threshold(row, w);
    if (threshold < 0)
        return false;
    if (!mel__runlength(row, w, threshold, runs))
        return false;

    for (usize d = 0; d < sizeof MEL__DECODERS / sizeof MEL__DECODERS[0]; ++d)
    {
        memset(out, 0, sizeof *out);
        if (MEL__DECODERS[d](runs, a, out))
            return true;
    }
    return false;
}

static bool mel__decode_scans(Runs* runs, const Mel_Alloc* a, const mel_image_gray* gray, mel_barcode_decode_result* out)
{
    i32 n_scans = 9;
    for (i32 si = 0; si < n_scans; ++si)
    {
        i32 y = (i32)(((i64)(si + 1) * gray->h) / (n_scans + 1));
        if (y < 0)
            y = 0;
        if (y >= gray->h)
            y = gray->h - 1;
        const u8* row = gray->pixels + (usize)y * (usize)gray->stride;

        Decoded dec;
        memset(&dec, 0, sizeof dec);
        if (mel__scan_row(row, gray->w, runs, a, &dec))
        {
            out->found = true;
            out->symbology = dec.symbology;
            out->text = dec.text;
            out->text_len = dec.text_len;
            out->y = y;
            out->x_start = mel__elem_x(runs, dec.elem_first);
            out->x_end = mel__elem_x(runs, dec.elem_first + dec.elem_count);
            return true;
        }
    }
    return false;
}

bool mel_barcode_decoder_init(mel_barcode_decoder* dec, i32 max_width, const Mel_Alloc* allocator)
{
    if (dec == NULL || allocator == NULL || max_width < 16)
        return false;
    i32 cap = max_width + 4;
    dec->widths = (i32*)mel_alloc(allocator, sizeof(i32) * (usize)cap);
    if (!dec->widths)
        return false;
    dec->values = (i32*)mel_alloc(allocator, sizeof(i32) * (usize)cap);
    if (!dec->values)
    {
        mel_dealloc(allocator, dec->widths);
        dec->widths = NULL;
        return false;
    }
    dec->alloc = allocator;
    dec->widths_cap = cap;
    dec->values_cap = cap;
    return true;
}

void mel_barcode_decoder_free(mel_barcode_decoder* dec)
{
    if (dec == NULL || dec->alloc == NULL)
        return;
    mel_dealloc(dec->alloc, dec->widths);
    mel_dealloc(dec->alloc, dec->values);
    dec->widths = NULL;
    dec->values = NULL;
    dec->widths_cap = 0;
    dec->values_cap = 0;
    dec->alloc = NULL;
}

bool mel_barcode_decoder_decode(mel_barcode_decoder* dec, const mel_image_gray* gray, mel_barcode_decode_result* out)
{
    if (dec == NULL || dec->alloc == NULL || gray == NULL || out == NULL || gray->pixels == NULL)
        return false;
    if (gray->w < 16 || gray->h < 1)
        return false;

    memset(out, 0, sizeof *out);

    i32 need = gray->w + 4;
    if (need > dec->widths_cap)
    {
        i32* grown = (i32*)mel_alloc(dec->alloc, sizeof(i32) * (usize)need);
        if (!grown)
            return false;
        mel_dealloc(dec->alloc, dec->widths);
        dec->widths = grown;
        dec->widths_cap = need;

        i32* grown_v = (i32*)mel_alloc(dec->alloc, sizeof(i32) * (usize)need);
        if (!grown_v)
            return false;
        mel_dealloc(dec->alloc, dec->values);
        dec->values = grown_v;
        dec->values_cap = need;
    }

    Runs runs;
    runs.widths = dec->widths;
    runs.capacity = dec->widths_cap;
    runs.values = dec->values;
    runs.values_cap = dec->values_cap;
    return mel__decode_scans(&runs, dec->alloc, gray, out);
}

bool mel_barcode_decode(const mel_image_gray* gray, mel_barcode_decode_result* out, const Mel_Alloc* allocator)
{
    if (gray == NULL || out == NULL || allocator == NULL || gray->pixels == NULL)
        return false;
    if (gray->w < 16 || gray->h < 1)
        return false;

    mel_barcode_decoder dec;
    if (!mel_barcode_decoder_init(&dec, gray->w, allocator))
        return false;
    bool result = mel_barcode_decoder_decode(&dec, gray, out);
    mel_barcode_decoder_free(&dec);
    return result;
}

void mel_barcode_decode_result_free(mel_barcode_decode_result* r, const Mel_Alloc* allocator)
{
    if (r == NULL)
        return;
    if (r->text != NULL && allocator != NULL)
        mel_dealloc(allocator, r->text);
    r->text = NULL;
    r->text_len = 0;
    r->found = false;
}
