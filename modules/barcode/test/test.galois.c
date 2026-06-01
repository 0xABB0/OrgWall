#include <barcode/galois.h>
#include <allocator/heap.h>
#include <test/test.h>

MEL_TEST(barcode_galois, binary_field_tables)
{
    mel_gf f;
    MEL_REQUIRE(mel_gf_binary_init(&f, 256, 0x11D, mel_alloc_heap()));

    MEL_REQUIRE_EQ((i32)f.exp[0], 1);
    MEL_REQUIRE_EQ((i32)f.exp[1], 2);
    MEL_REQUIRE_EQ((i32)f.exp[8], 0x1D);

    for (u16 a = 1; a < 256; ++a)
    {
        MEL_REQUIRE_EQ((i32)f.exp[f.log[a]], (i32)a);
        MEL_REQUIRE_EQ((i32)mel_gf_mul(&f, a, mel_gf_inv(&f, a)), 1);
    }

    MEL_REQUIRE_EQ((i32)mel_gf_mul(&f, 2, 0x80), 0x1D);
    MEL_REQUIRE_EQ((i32)mel_gf_add(&f, 0xA5, 0x5A), 0xFF);

    mel_gf_free(&f);
}

MEL_TEST(barcode_galois, prime_field_arithmetic)
{
    mel_gf f;
    MEL_REQUIRE(mel_gf_prime_init(&f, 929, mel_alloc_heap()));

    MEL_REQUIRE_EQ((i32)mel_gf_mul(&f, 3, 310), 1);
    MEL_REQUIRE_EQ((i32)mel_gf_inv(&f, 3), 310);
    MEL_REQUIRE_EQ((i32)mel_gf_add(&f, 900, 100), 71);
    MEL_REQUIRE_EQ((i32)mel_gf_sub(&f, 100, 900), 129);

    for (u16 a = 1; a < 929; ++a)
    {
        MEL_REQUIRE_EQ((i32)mel_gf_mul(&f, a, mel_gf_inv(&f, a)), 1);
    }

    mel_gf_free(&f);
}
