#include <barcode/bitreader.h>
#include <barcode/bitwriter.h>
#include <barcode/rs.h>

#include <allocator/heap.h>
#include <test/test.h>

#include <string.h>

static void mel__rs_build_codeword(mel_gf* f, const u16* data, usize n_data, usize n_ecc, u16* code_out)
{
    u16 ecc[64];
    MEL_REQUIRE(n_ecc <= 64);
    MEL_REQUIRE(mel_rs_generate(f, 2, 0, data, n_data, n_ecc, ecc));
    for (usize i = 0; i < n_data; ++i)
    {
        code_out[i] = data[i];
    }
    for (usize i = 0; i < n_ecc; ++i)
    {
        code_out[n_data + i] = ecc[i];
    }
}

MEL_TEST(barcode_rs_decode, single_error_every_position)
{
    mel_gf f;
    MEL_REQUIRE(mel_gf_binary_init(&f, 256, 0x11D, mel_alloc_heap()));

    static const u16 data[16] = { 32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17 };
    usize            n_data = 16, n_ecc = 10, n_total = 26;

    u16 base[26];
    mel__rs_build_codeword(&f, data, n_data, n_ecc, base);

    for (usize p = 0; p < n_total; ++p)
    {
        u16 cw[26];
        memcpy(cw, base, sizeof(base));
        cw[p] = (u16)(cw[p] ^ 0x5Au);

        usize corrected = 999;
        MEL_REQUIRE(mel_rs_decode(&f, 2, 0, cw, n_total, n_ecc, NULL, 0, &corrected, mel_alloc_heap()));
        MEL_REQUIRE_EQ((i32)corrected, 1);
        MEL_REQUIRE_EQ(memcmp(cw, base, sizeof(base)), 0);
    }

    mel_gf_free(&f);
}

MEL_TEST(barcode_rs_decode, corrects_up_to_t_errors)
{
    mel_gf f;
    MEL_REQUIRE(mel_gf_binary_init(&f, 256, 0x11D, mel_alloc_heap()));

    static const u16 data[16] = { 32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17 };
    usize            n_data = 16, n_ecc = 10, n_total = 26;
    i32              t = (i32)(n_ecc / 2);

    u16 base[26];
    mel__rs_build_codeword(&f, data, n_data, n_ecc, base);

    static const usize positions[5] = { 0, 7, 13, 19, 25 };
    for (i32 k = 1; k <= t; ++k)
    {
        u16 cw[26];
        memcpy(cw, base, sizeof(base));
        for (i32 j = 0; j < k; ++j)
        {
            cw[positions[j]] = (u16)(cw[positions[j]] ^ (u16)(0x31u + 11u * (u32)j));
        }

        usize corrected = 999;
        MEL_REQUIRE(mel_rs_decode(&f, 2, 0, cw, n_total, n_ecc, NULL, 0, &corrected, mel_alloc_heap()));
        MEL_REQUIRE_EQ((i32)corrected, k);
        MEL_REQUIRE_EQ(memcmp(cw, base, sizeof(base)), 0);
    }

    mel_gf_free(&f);
}

MEL_TEST(barcode_rs_decode, clean_codeword_corrects_nothing)
{
    mel_gf f;
    MEL_REQUIRE(mel_gf_binary_init(&f, 256, 0x11D, mel_alloc_heap()));

    static const u16 data[16] = { 32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17 };
    usize            n_data = 16, n_ecc = 10, n_total = 26;

    u16 base[26];
    mel__rs_build_codeword(&f, data, n_data, n_ecc, base);

    u16 cw[26];
    memcpy(cw, base, sizeof(base));
    usize corrected = 999;
    MEL_REQUIRE(mel_rs_decode(&f, 2, 0, cw, n_total, n_ecc, NULL, 0, &corrected, mel_alloc_heap()));
    MEL_REQUIRE_EQ((i32)corrected, 0);
    MEL_REQUIRE_EQ(memcmp(cw, base, sizeof(base)), 0);

    mel_gf_free(&f);
}

MEL_TEST(barcode_rs_decode, beyond_capacity_returns_false)
{
    mel_gf f;
    MEL_REQUIRE(mel_gf_binary_init(&f, 256, 0x11D, mel_alloc_heap()));

    static const u16 data[16] = { 32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17 };
    usize            n_data = 16, n_ecc = 10, n_total = 26;
    i32              t = (i32)(n_ecc / 2);

    u16 base[26];
    mel__rs_build_codeword(&f, data, n_data, n_ecc, base);

    static const usize positions[6] = { 0, 5, 10, 15, 20, 25 };
    u16                cw[26];
    memcpy(cw, base, sizeof(base));
    for (i32 j = 0; j <= t; ++j)
    {
        cw[positions[j]] = (u16)(cw[positions[j]] ^ (u16)(0x40u + (u32)j));
    }

    usize corrected = 999;
    MEL_REQUIRE(!mel_rs_decode(&f, 2, 0, cw, n_total, n_ecc, NULL, 0, &corrected, mel_alloc_heap()));

    mel_gf_free(&f);
}

MEL_TEST(barcode_rs_decode, erasures_plus_errors_within_capacity)
{
    mel_gf f;
    MEL_REQUIRE(mel_gf_binary_init(&f, 256, 0x11D, mel_alloc_heap()));

    static const u16 data[16] = { 32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17 };
    usize            n_data = 16, n_ecc = 10, n_total = 26;

    u16 base[26];
    mel__rs_build_codeword(&f, data, n_data, n_ecc, base);

    u16 cw[26];
    memcpy(cw, base, sizeof(base));

    static const u16 erasures[4] = { 1, 8, 16, 23 };
    for (i32 j = 0; j < 4; ++j)
    {
        cw[erasures[j]] = (u16)(cw[erasures[j]] ^ (u16)(0x11u * (u32)(j + 1)));
    }
    cw[3] = (u16)(cw[3] ^ 0x77u);
    cw[12] = (u16)(cw[12] ^ 0x33u);
    cw[21] = (u16)(cw[21] ^ 0x09u);

    usize corrected = 999;
    MEL_REQUIRE(mel_rs_decode(&f, 2, 0, cw, n_total, n_ecc, erasures, 4, &corrected, mel_alloc_heap()));
    MEL_REQUIRE_EQ((i32)corrected, 7);
    MEL_REQUIRE_EQ(memcmp(cw, base, sizeof(base)), 0);

    mel_gf_free(&f);
}

MEL_TEST(barcode_bitreader, roundtrips_bitwriter_across_byte_boundaries)
{
    static const u32 values[8] = { 0x5, 0x1, 0x2AA, 0xC3, 0x7, 0x0, 0x1FF, 0x3 };
    static const u32 widths[8] = { 3, 1, 10, 8, 3, 4, 9, 2 };

    mel_bitwriter w;
    mel_bitwriter_init(&w, mel_alloc_heap());
    for (u32 i = 0; i < 8; ++i)
    {
        mel_bitwriter_put(&w, values[i], widths[i]);
    }
    mel_bitwriter_pad_to_byte(&w);

    mel_bitreader r;
    mel_bitreader_init(&r, mel_bitwriter_bytes(&w), mel_bitwriter_byte_count(&w));
    for (u32 i = 0; i < 8; ++i)
    {
        u32 got = mel_bitreader_get(&r, widths[i]);
        u32 want = values[i] & ((1u << widths[i]) - 1u);
        MEL_REQUIRE_EQ((i32)got, (i32)want);
    }

    mel_bitwriter_free(&w);
}

MEL_TEST(barcode_bitreader, reads_past_end_yield_zero)
{
    static const u8 bytes[2] = { 0xB5, 0x6C };

    mel_bitreader r;
    mel_bitreader_init(&r, bytes, 2);

    MEL_REQUIRE_EQ((i32)mel_bitreader_remaining(&r), 16);
    MEL_REQUIRE_EQ((i32)mel_bitreader_get(&r, 4), 0xB);
    MEL_REQUIRE_EQ((i32)mel_bitreader_get(&r, 8), 0x56);
    MEL_REQUIRE_EQ((i32)mel_bitreader_remaining(&r), 4);
    MEL_REQUIRE(!mel_bitreader_exhausted(&r));

    MEL_REQUIRE_EQ((i32)mel_bitreader_get(&r, 8), 0xC0);
    MEL_REQUIRE(mel_bitreader_exhausted(&r));
    MEL_REQUIRE_EQ((i32)mel_bitreader_remaining(&r), 0);
    MEL_REQUIRE_EQ((i32)mel_bitreader_get(&r, 16), 0);
}
