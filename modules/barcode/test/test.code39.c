#include <barcode/code39.h>
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

MEL_TEST(barcode_code39, single_char_pattern)
{
    static const char* expected = "100010111011101"
                                  "0"
                                  "111010100010111"
                                  "0"
                                  "100010111011101";

    mel_barcode_matrix m;
    MEL_REQUIRE(mel_code39_encode(&m, "A", 30, (mel_code39_opt){ 0 }, mel_alloc_heap()));
    MEL_REQUIRE_EQ(m.quiet_zone, 10);
    mel__expect_row(&m, expected);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_code39, width_formula)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_code39_encode(&m, "CODE39", 10, (mel_code39_opt){ 0 }, mel_alloc_heap()));
    MEL_REQUIRE_EQ(m.width, 15 * 8 + 7);
    mel_barcode_matrix_free(&m);

    MEL_REQUIRE(mel_code39_encode(&m, "CODE39", 10, (mel_code39_opt){ .checksum = true }, mel_alloc_heap()));
    MEL_REQUIRE_EQ(m.width, 15 * 9 + 8);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_code39, rejects_invalid)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(!mel_code39_encode(&m, "abc", 10, (mel_code39_opt){ 0 }, mel_alloc_heap()));
    MEL_REQUIRE(!mel_code39_encode(&m, "A*B", 10, (mel_code39_opt){ 0 }, mel_alloc_heap()));
    MEL_REQUIRE(!mel_code39_encode(&m, "", 10, (mel_code39_opt){ 0 }, mel_alloc_heap()));
}
