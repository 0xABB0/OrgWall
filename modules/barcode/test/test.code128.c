#include <barcode/code128.h>
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

MEL_TEST(barcode_code128, single_char_pattern)
{
    static const char* expected = "11010010000"
                                  "10100011000"
                                  "10001011000"
                                  "1100011101011";

    mel_barcode_matrix m;
    MEL_REQUIRE(mel_code128_encode(&m, "A", 30, mel_alloc_heap()));
    MEL_REQUIRE_EQ(m.quiet_zone, 10);
    mel__expect_row(&m, expected);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_code128, code_c_digit_run)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_code128_encode(&m, "1234", 10, mel_alloc_heap()));
    MEL_REQUIRE_EQ(m.width, 11 * 4 + 13);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_code128, rejects_invalid)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(!mel_code128_encode(&m, "", 10, mel_alloc_heap()));
    MEL_REQUIRE(!mel_code128_encode(&m, "ab\x01", 10, mel_alloc_heap()));
}
