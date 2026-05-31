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
