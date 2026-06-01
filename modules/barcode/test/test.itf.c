#include <barcode/itf.h>
#include <allocator/heap.h>
#include <test/test.h>

#include <string.h>

static void mel__expect_row(mel_barcode_matrix* m, const char* bits)
{
    MEL_REQUIRE_EQ(m->width, (i32)strlen(bits));
    for (i32 x = 0; x < m->width; ++x)
    {
        MEL_REQUIRE_EQ(mel_barcode_matrix_get(m, x, 0), bits[x] == '1');
    }
}

MEL_TEST(barcode_itf, pair_pattern)
{
    static const char* expected = "1010"
                                  "111010001010111000"
                                  "11101";

    mel_barcode_matrix m;
    MEL_REQUIRE(mel_itf_encode(&m, "12", 20, (mel_itf_opt){ 0 }, mel_alloc_heap()));
    MEL_REQUIRE_EQ(m.quiet_zone, 10);
    mel__expect_row(&m, expected);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_itf, width_formula)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_itf_encode(&m, "1234", 10, (mel_itf_opt){ 0 }, mel_alloc_heap()));
    MEL_REQUIRE_EQ(m.width, 4 + 18 * 2 + 5);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_itf, odd_length_policy)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(!mel_itf_encode(&m, "123", 10, (mel_itf_opt){ 0 }, mel_alloc_heap()));
    MEL_REQUIRE(mel_itf_encode(&m, "123", 10, (mel_itf_opt){ .pad_odd = true }, mel_alloc_heap()));
    MEL_REQUIRE_EQ(m.width, 4 + 18 * 2 + 5);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_itf, checksum_then_pad)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_itf_encode(&m, "1234", 10, (mel_itf_opt){ .checksum = true, .pad_odd = true }, mel_alloc_heap()));
    MEL_REQUIRE_EQ(m.width, 4 + 18 * 3 + 5);
    mel_barcode_matrix_free(&m);

    MEL_REQUIRE(!mel_itf_encode(&m, "12a4", 10, (mel_itf_opt){ 0 }, mel_alloc_heap()));
}
