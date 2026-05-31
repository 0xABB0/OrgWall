#include <barcode/qr.h>
#include <barcode/galois.h>
#include <barcode/rs.h>
#include <allocator/heap.h>
#include <test/test.h>

static const i32 MEL__QR_EC_T[10][4][5] = {
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
static const i32 MEL__QR_TOTAL_T[10] = {26, 44, 70, 100, 134, 172, 196, 242, 292, 346};

static u16 mel__poly_eval(const mel_gf* f, const u16* c, usize n, u16 x) {
    u16 a = 0;
    for (usize i = 0; i < n; ++i) {
        a = f->add(f, f->mul(f, a, x), c[i]);
    }
    return a;
}

MEL_TEST(barcode_qr, ec_table_matches_total_codewords) {
    for (i32 v = 0; v < 10; ++v) {
        for (i32 r = 0; r < 4; ++r) {
            const i32* e = MEL__QR_EC_T[v][r];
            MEL_REQUIRE_EQ(e[1] * e[2] + e[3] * e[4] + (e[1] + e[3]) * e[0],
                           MEL__QR_TOTAL_T[v]);
        }
    }
}

MEL_TEST(barcode_qr, iso_numeric_example) {
    static const u8 data_anchor[16] = {
        0x10, 0x20, 0x0C, 0x56, 0x61, 0x80, 0xEC, 0x11,
        0xEC, 0x11, 0xEC, 0x11, 0xEC, 0x11, 0xEC, 0x11,
    };

    u8* cw = NULL;
    usize count = 0;
    i32 version = 0;
    MEL_REQUIRE(mel_qr_codewords("01234567", mel_qr_ecc_m(), 0, mel_alloc_heap(),
                                 &cw, &count, &version));
    MEL_REQUIRE_EQ(version, 1);
    MEL_REQUIRE_EQ((i32)count, 26);
    for (i32 i = 0; i < 16; ++i) {
        MEL_REQUIRE_EQ((i32)cw[i], (i32)data_anchor[i]);
    }

    mel_gf f;
    MEL_REQUIRE(mel_gf_binary_init(&f, 256, 0x11D, mel_alloc_heap()));
    u16 code[26];
    for (i32 i = 0; i < 26; ++i) {
        code[i] = cw[i];
    }
    for (u32 i = 0; i < 10; ++i) {
        MEL_REQUIRE_EQ((i32)mel__poly_eval(&f, code, 26, mel_gf_pow(&f, 2, i)), 0);
    }
    mel_gf_free(&f);
    mel_dealloc(mel_alloc_heap(), cw);
}

MEL_TEST(barcode_qr, version_selection_and_capacity) {
    u8* cw = NULL;
    usize count = 0;
    i32 version = 0;
    MEL_REQUIRE(mel_qr_codewords("HELLO WORLD", mel_qr_ecc_q(), 0,
                                 mel_alloc_heap(), &cw, &count, &version));
    MEL_REQUIRE_EQ(version, 1);
    mel_dealloc(mel_alloc_heap(), cw);

    char big[2049];
    for (i32 i = 0; i < 2048; ++i) {
        big[i] = 'a';
    }
    big[2048] = '\0';
    MEL_REQUIRE(!mel_qr_codewords(big, mel_qr_ecc_h(), 0, mel_alloc_heap(), &cw,
                                  &count, &version));
}

MEL_TEST(barcode_qr, multiblock_interleave_roots) {
    char payload[33];
    for (i32 i = 0; i < 32; ++i) {
        payload[i] = 'a';
    }
    payload[32] = '\0';

    u8* cw = NULL;
    usize count = 0;
    i32 version = 0;
    MEL_REQUIRE(mel_qr_codewords(payload, mel_qr_ecc_q(), 0, mel_alloc_heap(),
                                 &cw, &count, &version));
    MEL_REQUIRE_EQ(version, 3);
    MEL_REQUIRE_EQ((i32)count, 70);

    mel_gf f;
    MEL_REQUIRE(mel_gf_binary_init(&f, 256, 0x11D, mel_alloc_heap()));

    for (i32 b = 0; b < 2; ++b) {
        u16 block[35];
        for (i32 c = 0; c < 17; ++c) {
            block[c] = cw[2 * c + b];
        }
        for (i32 c = 0; c < 18; ++c) {
            block[17 + c] = cw[34 + 2 * c + b];
        }
        for (u32 i = 0; i < 18; ++i) {
            MEL_REQUIRE_EQ((i32)mel__poly_eval(&f, block, 35, mel_gf_pow(&f, 2, i)),
                           0);
        }
    }
    mel_gf_free(&f);
    mel_dealloc(mel_alloc_heap(), cw);
}

static const i32 MEL__QR_ALIGN_T[10][4] = {
    {0, 0, 0, 0}, {2, 6, 18, 0}, {2, 6, 22, 0}, {2, 6, 26, 0}, {2, 6, 30, 0},
    {2, 6, 34, 0}, {3, 6, 22, 38}, {3, 6, 24, 42}, {3, 6, 26, 46}, {3, 6, 28, 50},
};

static bool mel__qr_maskc(i32 m, i32 r, i32 c) {
    switch (m) {
        case 0: return (r + c) % 2 == 0;
        case 1: return r % 2 == 0;
        case 2: return c % 3 == 0;
        case 3: return (r + c) % 3 == 0;
        case 4: return (r / 2 + c / 3) % 2 == 0;
        case 5: return (r * c) % 2 + (r * c) % 3 == 0;
        case 6: return ((r * c) % 2 + (r * c) % 3) % 2 == 0;
        default: return ((r + c) % 2 + (r * c) % 3) % 2 == 0;
    }
}

static bool mel__qr_isfunc(i32 size, i32 ver, i32 r, i32 c) {
    if ((r < 8 && c < 8) || (r < 8 && c >= size - 8) || (r >= size - 8 && c < 8)) {
        return true;
    }
    if (r == 6 || c == 6) {
        return true;
    }
    if (r == 8 && (c <= 8 || c >= size - 8)) {
        return true;
    }
    if (c == 8 && (r <= 8 || r >= size - 8)) {
        return true;
    }
    if (ver >= 7) {
        if (r < 6 && c >= size - 11 && c <= size - 9) {
            return true;
        }
        if (c < 6 && r >= size - 11 && r <= size - 9) {
            return true;
        }
    }
    const i32* al = MEL__QR_ALIGN_T[ver - 1];
    for (i32 a = 1; a <= al[0]; ++a) {
        for (i32 b = 1; b <= al[0]; ++b) {
            bool corner = (a == 1 && b == 1) || (a == 1 && b == al[0]) ||
                          (a == al[0] && b == 1);
            if (corner) {
                continue;
            }
            i32 dr = r - al[a], dc = c - al[b];
            if (dr >= -2 && dr <= 2 && dc >= -2 && dc <= 2) {
                return true;
            }
        }
    }
    return false;
}

static void mel__qr_roundtrip(const char* data, mel_qr_opt opt,
                              i32 expect_version) {
    const Mel_Alloc* a = mel_alloc_heap();
    u8* cw = NULL;
    usize count = 0;
    i32 ver = 0;
    MEL_REQUIRE(mel_qr_codewords(data, opt.ecc, opt.version, a, &cw, &count, &ver));
    MEL_REQUIRE_EQ(ver, expect_version);

    mel_barcode_matrix m;
    MEL_REQUIRE(mel_qr_encode(&m, data, opt, a));
    i32 size = m.width;
    MEL_REQUIRE_EQ(size, 4 * ver + 17);
    MEL_REQUIRE_EQ(m.quiet_zone, 4);

    MEL_REQUIRE(mel_barcode_matrix_get(&m, 0, 0));
    MEL_REQUIRE(!mel_barcode_matrix_get(&m, 1, 1));
    MEL_REQUIRE(mel_barcode_matrix_get(&m, 8, size - 8));
    for (i32 i = 8; i < size - 8; ++i) {
        MEL_REQUIRE_EQ(mel_barcode_matrix_get(&m, i, 6), (i % 2) == 0);
    }

    u32 fb = 0;
    for (i32 i = 0; i <= 5; ++i) {
        fb |= (u32)mel_barcode_matrix_get(&m, i, 8) << i;
    }
    fb |= (u32)mel_barcode_matrix_get(&m, 7, 8) << 6;
    fb |= (u32)mel_barcode_matrix_get(&m, 8, 8) << 7;
    fb |= (u32)mel_barcode_matrix_get(&m, 8, 7) << 8;
    for (i32 i = 9; i < 15; ++i) {
        fb |= (u32)mel_barcode_matrix_get(&m, 8, 14 - i) << i;
    }
    u32 fdata = (fb ^ 0x5412u) >> 10;
    i32 mask = (i32)(fdata & 7);
    static const i32 ind[4] = {1, 0, 3, 2};
    MEL_REQUIRE_EQ((i32)(fdata >> 3), ind[opt.ecc.rank]);

    u8* got = mel_calloc(a, count);
    usize bit = 0;
    for (i32 right = size - 1; right >= 1; right -= 2) {
        if (right == 6) {
            right = 5;
        }
        for (i32 vert = 0; vert < size; ++vert) {
            for (i32 j = 0; j < 2; ++j) {
                i32 col = right - j;
                bool up = ((right + 1) & 2) == 0;
                i32 row = up ? size - 1 - vert : vert;
                if (!mel__qr_isfunc(size, ver, row, col) && bit < count * 8) {
                    bool d = mel_barcode_matrix_get(&m, col, row);
                    if (mel__qr_maskc(mask, row, col)) {
                        d = !d;
                    }
                    got[bit >> 3] |= (u8)(d << (7 - (bit & 7)));
                    bit += 1;
                }
            }
        }
    }
    for (usize i = 0; i < count; ++i) {
        MEL_REQUIRE_EQ((i32)got[i], (i32)cw[i]);
    }
    mel_dealloc(a, got);
    mel_barcode_matrix_free(&m);
    mel_dealloc(a, cw);
}

MEL_TEST(barcode_qr, encode_roundtrip_v1) {
    mel__qr_roundtrip("01234567",
                      (mel_qr_opt){.ecc = mel_qr_ecc_m(), .version = 0, .mask = -1},
                      1);
}

MEL_TEST(barcode_qr, encode_roundtrip_multiblock) {
    char payload[33];
    for (i32 i = 0; i < 32; ++i) {
        payload[i] = 'a';
    }
    payload[32] = '\0';
    mel__qr_roundtrip(payload,
                      (mel_qr_opt){.ecc = mel_qr_ecc_q(), .version = 0, .mask = -1},
                      3);
}

MEL_TEST(barcode_qr, encode_roundtrip_v7_with_version_info) {
    mel__qr_roundtrip("ABCDEFGHIJ",
                      (mel_qr_opt){.ecc = mel_qr_ecc_m(), .version = 7, .mask = -1},
                      7);
}
