#include <barcode/rs.h>
#include <allocator/heap.h>
#include <test/test.h>

static u16 mel__poly_eval(const mel_gf* f, const u16* coef, usize n, u16 x)
{
    u16 acc = 0;
    for (usize i = 0; i < n; ++i)
    {
        acc = f->add(f, f->mul(f, acc, x), coef[i]);
    }
    return acc;
}

MEL_TEST(barcode_rs, qr_hello_world_reference)
{
    static const u16 data[16] = {
        32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17,
    };
    static const u16 expect[10] = {
        196, 35, 39, 119, 235, 215, 231, 226, 93, 23,
    };

    mel_gf f;
    MEL_REQUIRE(mel_gf_binary_init(&f, 256, 0x11D, mel_alloc_heap()));

    u16 ecc[10];
    MEL_REQUIRE(mel_rs_generate(&f, 2, 0, data, 16, 10, ecc));
    for (i32 i = 0; i < 10; ++i)
    {
        MEL_REQUIRE_EQ((i32)ecc[i], (i32)expect[i]);
    }

    u16 code[26];
    for (i32 i = 0; i < 16; ++i)
    {
        code[i] = data[i];
    }
    for (i32 i = 0; i < 10; ++i)
    {
        code[16 + i] = ecc[i];
    }
    for (u32 i = 0; i < 10; ++i)
    {
        MEL_REQUIRE_EQ((i32)mel__poly_eval(&f, code, 26, mel_gf_pow(&f, 2, i)), 0);
    }

    mel_gf_free(&f);
}

MEL_TEST(barcode_rs, prime_field_codeword_roots)
{
    static const u16 data[5] = { 10, 20, 30, 40, 50 };

    mel_gf f;
    MEL_REQUIRE(mel_gf_prime_init(&f, 929, mel_alloc_heap()));

    u16 ecc[4];
    MEL_REQUIRE(mel_rs_generate(&f, 3, 1, data, 5, 4, ecc));

    u16 code[9];
    for (i32 i = 0; i < 5; ++i)
    {
        code[i] = data[i];
    }
    for (i32 i = 0; i < 4; ++i)
    {
        code[5 + i] = ecc[i];
    }
    for (u32 i = 0; i < 4; ++i)
    {
        MEL_REQUIRE_EQ((i32)mel__poly_eval(&f, code, 9, mel_gf_pow(&f, 3, 1 + i)), 0);
    }

    mel_gf_free(&f);
}
