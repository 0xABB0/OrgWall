#include <barcode/qr.h>

#include <barcode/bitwriter.h>
#include <barcode/galois.h>
#include <barcode/rs.h>

#include <string.h>

#define MEL__QR_MAXV 10

static const i32 MEL__QR_EC[MEL__QR_MAXV][4][5] = {
    {{7, 1, 19, 0, 0}, {10, 1, 16, 0, 0}, {13, 1, 13, 0, 0}, {17, 1, 9, 0, 0}},
    {{10, 1, 34, 0, 0}, {16, 1, 28, 0, 0}, {22, 1, 22, 0, 0}, {28, 1, 16, 0, 0}},
    {{15, 1, 55, 0, 0}, {26, 1, 44, 0, 0}, {18, 2, 17, 0, 0}, {22, 2, 13, 0, 0}},
    {{20, 1, 80, 0, 0}, {18, 2, 32, 0, 0}, {26, 2, 24, 0, 0}, {16, 4, 9, 0, 0}},
    {{26, 1, 108, 0, 0}, {24, 2, 43, 0, 0}, {18, 2, 15, 2, 16}, {22, 2, 11, 2, 12}},
    {{18, 2, 68, 0, 0}, {16, 4, 27, 0, 0}, {24, 4, 19, 0, 0}, {28, 4, 15, 0, 0}},
    {{20, 2, 78, 0, 0}, {18, 4, 31, 0, 0}, {18, 2, 14, 4, 15}, {26, 4, 13, 1, 14}},
    {{24, 2, 97, 0, 0}, {22, 2, 38, 2, 39}, {22, 4, 18, 2, 19}, {26, 4, 14, 2, 15}},
    {{30, 2, 116, 0, 0}, {22, 3, 36, 2, 37}, {20, 4, 16, 4, 17}, {24, 4, 12, 4, 13}},
    {{18, 2, 68, 2, 69}, {26, 4, 43, 1, 44}, {24, 6, 19, 2, 20}, {28, 6, 15, 2, 16}},
};

static const i32 MEL__QR_TOTAL[MEL__QR_MAXV] = {
    26, 44, 70, 100, 134, 172, 196, 242, 292, 346,
};

static const char MEL__QR_ALNUM[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

mel_qr_ecc mel_qr_ecc_l(void) { return (mel_qr_ecc){0}; }
mel_qr_ecc mel_qr_ecc_m(void) { return (mel_qr_ecc){1}; }
mel_qr_ecc mel_qr_ecc_q(void) { return (mel_qr_ecc){2}; }
mel_qr_ecc mel_qr_ecc_h(void) { return (mel_qr_ecc){3}; }

static i32 mel__qr_alnum_value(char c) {
    const char* p = strchr(MEL__QR_ALNUM, c);
    return (p == NULL || c == '\0') ? -1 : (i32)(p - MEL__QR_ALNUM);
}

static i32 mel__qr_mode(const char* s, usize n) {
    bool numeric = true;
    bool alnum = true;
    for (usize i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            numeric = false;
        }
        if (mel__qr_alnum_value(s[i]) < 0) {
            alnum = false;
        }
    }
    return numeric ? 0 : (alnum ? 1 : 2);
}

static i32 mel__qr_count_bits(i32 version, i32 mode) {
    static const i32 width[3][3] = {{10, 9, 8}, {12, 11, 16}, {14, 13, 16}};
    i32 group = version <= 9 ? 0 : (version <= 26 ? 1 : 2);
    return width[group][mode];
}

static usize mel__qr_data_bits(i32 mode, usize n) {
    if (mode == 0) {
        usize r = n % 3;
        return (n / 3) * 10 + (r == 2 ? 7 : (r == 1 ? 4 : 0));
    }
    if (mode == 1) {
        return (n / 2) * 11 + ((n % 2) ? 6 : 0);
    }
    return n * 8;
}

static i32 mel__qr_data_cw(i32 version, i32 rank) {
    const i32* e = MEL__QR_EC[version - 1][rank];
    return e[1] * e[2] + e[3] * e[4];
}

static void mel__qr_emit_data(mel_bitwriter* w, i32 mode, const char* data,
                              usize n) {
    if (mode == 0) {
        usize i = 0;
        for (; i + 3 <= n; i += 3) {
            u32 v = (u32)((data[i] - '0') * 100 + (data[i + 1] - '0') * 10 +
                          (data[i + 2] - '0'));
            mel_bitwriter_put(w, v, 10);
        }
        usize r = n - i;
        if (r == 2) {
            mel_bitwriter_put(w, (u32)((data[i] - '0') * 10 + (data[i + 1] - '0')),
                              7);
        } else if (r == 1) {
            mel_bitwriter_put(w, (u32)(data[i] - '0'), 4);
        }
    } else if (mode == 1) {
        usize i = 0;
        for (; i + 2 <= n; i += 2) {
            i32 hi = mel__qr_alnum_value(data[i]);
            i32 lo = mel__qr_alnum_value(data[i + 1]);
            mel_bitwriter_put(w, (u32)(45 * hi + lo), 11);
        }
        if (n % 2) {
            mel_bitwriter_put(w, (u32)mel__qr_alnum_value(data[n - 1]), 6);
        }
    } else {
        for (usize i = 0; i < n; ++i) {
            mel_bitwriter_put(w, (u8)data[i], 8);
        }
    }
}

bool mel_qr_codewords(const char* data, mel_qr_ecc ecc, i32 version_hint,
                      const Mel_Alloc* a, u8** out_cw, usize* out_count,
                      i32* out_version) {
    if (data == NULL || out_cw == NULL || out_count == NULL ||
        out_version == NULL) {
        return false;
    }
    i32 rank = ecc.rank;
    if (rank < 0 || rank > 3 || version_hint > MEL__QR_MAXV) {
        return false;
    }

    usize n = strlen(data);
    i32 mode = mel__qr_mode(data, n);

    i32 start = version_hint > 0 ? version_hint : 1;
    i32 end = version_hint > 0 ? version_hint : MEL__QR_MAXV;
    i32 v = 0;
    for (i32 t = start; t <= end; ++t) {
        usize need =
            4 + (usize)mel__qr_count_bits(t, mode) + mel__qr_data_bits(mode, n);
        if (need <= (usize)mel__qr_data_cw(t, rank) * 8) {
            v = t;
            break;
        }
    }
    if (v == 0) {
        return false;
    }

    i32 dcw_total = mel__qr_data_cw(v, rank);

    mel_bitwriter w;
    mel_bitwriter_init(&w, a);
    static const u32 mode_indicator[3] = {0x1, 0x2, 0x4};
    mel_bitwriter_put(&w, mode_indicator[mode], 4);
    mel_bitwriter_put(&w, (u32)n, (u32)mel__qr_count_bits(v, mode));
    mel__qr_emit_data(&w, mode, data, n);

    usize cap_bits = (usize)dcw_total * 8;
    usize term = cap_bits - mel_bitwriter_bit_length(&w);
    if (term > 4) {
        term = 4;
    }
    mel_bitwriter_put(&w, 0, (u32)term);
    mel_bitwriter_pad_to_byte(&w);
    u32 pad = 0xEC;
    while (mel_bitwriter_byte_count(&w) < (usize)dcw_total) {
        mel_bitwriter_put(&w, pad, 8);
        pad = (pad == 0xEC) ? 0x11 : 0xEC;
    }

    const u8* dcw = mel_bitwriter_bytes(&w);
    const i32* e = MEL__QR_EC[v - 1][rank];
    i32 ecw = e[0], g1 = e[1], g1d = e[2], g2 = e[3], g2d = e[4];
    i32 nblocks = g1 + g2;
    i32 total = MEL__QR_TOTAL[v - 1];

    mel_gf f;
    if (!mel_gf_binary_init(&f, 256, 0x11D, a)) {
        mel_bitwriter_free(&w);
        return false;
    }

    i32 maxd = g1d > g2d ? g1d : g2d;
    u8* ec = mel_alloc(a, (usize)nblocks * (usize)ecw);
    u16* tmpd = mel_alloc(a, sizeof(u16) * (usize)maxd);
    u16* tmpe = mel_alloc(a, sizeof(u16) * (usize)ecw);
    u8* out = mel_alloc(a, (usize)total);
    if (ec == NULL || tmpd == NULL || tmpe == NULL || out == NULL) {
        mel_dealloc(a, ec);
        mel_dealloc(a, tmpd);
        mel_dealloc(a, tmpe);
        mel_dealloc(a, out);
        mel_gf_free(&f);
        mel_bitwriter_free(&w);
        return false;
    }

    for (i32 b = 0; b < nblocks; ++b) {
        i32 len = (b < g1) ? g1d : g2d;
        i32 off = (b < g1) ? b * g1d : g1 * g1d + (b - g1) * g2d;
        for (i32 c = 0; c < len; ++c) {
            tmpd[c] = dcw[off + c];
        }
        mel_rs_generate(&f, 2, 0, tmpd, (usize)len, (usize)ecw, tmpe);
        for (i32 c = 0; c < ecw; ++c) {
            ec[b * ecw + c] = (u8)tmpe[c];
        }
    }

    i32 idx = 0;
    for (i32 c = 0; c < maxd; ++c) {
        for (i32 b = 0; b < nblocks; ++b) {
            i32 len = (b < g1) ? g1d : g2d;
            i32 off = (b < g1) ? b * g1d : g1 * g1d + (b - g1) * g2d;
            if (c < len) {
                out[idx++] = dcw[off + c];
            }
        }
    }
    for (i32 c = 0; c < ecw; ++c) {
        for (i32 b = 0; b < nblocks; ++b) {
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
