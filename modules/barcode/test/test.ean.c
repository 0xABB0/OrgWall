#include <barcode/ean.h>
#include <allocator/heap.h>
#include <test/test.h>

#include <string.h>

MEL_TEST(barcode_ean, checkdigit_vectors)
{
    MEL_REQUIRE_EQ(mel_ean13_checkdigit("590123412345"), 7);
    MEL_REQUIRE_EQ(mel_ean13_checkdigit("400638133393"), 1);
    MEL_REQUIRE_EQ(mel_ean8_checkdigit("9638507"), 4);
}

MEL_TEST(barcode_ean, ean13_module_pattern)
{
    static const char* expected = "101"
                                  "0001011"
                                  "0100111"
                                  "0110011"
                                  "0010011"
                                  "0111101"
                                  "0011101"
                                  "01010"
                                  "1100110"
                                  "1101100"
                                  "1000010"
                                  "1011100"
                                  "1001110"
                                  "1000100"
                                  "101";

    mel_barcode_matrix m;
    MEL_REQUIRE(mel_ean13_encode(&m, "590123412345", 20, mel_alloc_heap()));
    MEL_REQUIRE_EQ(m.width, 95);
    MEL_REQUIRE_EQ(m.height, 20);
    MEL_REQUIRE_EQ((i32)strlen(expected), 95);

    for (i32 x = 0; x < 95; ++x)
    {
        bool want = expected[x] == '1';
        MEL_REQUIRE_EQ(mel_barcode_matrix_get(&m, x, 0), want);
        MEL_REQUIRE_EQ(mel_barcode_matrix_get(&m, x, 19), want);
    }
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_ean, ean13_verifies_supplied_checkdigit)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_ean13_encode(&m, "5901234123457", 10, mel_alloc_heap()));
    mel_barcode_matrix_free(&m);

    MEL_REQUIRE(!mel_ean13_encode(&m, "5901234123450", 10, mel_alloc_heap()));
    MEL_REQUIRE(!mel_ean13_encode(&m, "59012341234", 10, mel_alloc_heap()));
    MEL_REQUIRE(!mel_ean13_encode(&m, "59012341234a7", 10, mel_alloc_heap()));
}

MEL_TEST(barcode_ean, ean8_dimensions)
{
    mel_barcode_matrix m;
    MEL_REQUIRE(mel_ean8_encode(&m, "9638507", 12, mel_alloc_heap()));
    MEL_REQUIRE_EQ(m.width, 67);
    MEL_REQUIRE_EQ(m.quiet_zone, 7);
    mel_barcode_matrix_free(&m);
}

MEL_TEST(barcode_ean, upca_is_ean13_with_leading_zero)
{
    mel_barcode_matrix upc;
    mel_barcode_matrix ean;
    MEL_REQUIRE(mel_upca_encode(&upc, "03600029145", 10, mel_alloc_heap()));
    MEL_REQUIRE(mel_ean13_encode(&ean, "003600029145", 10, mel_alloc_heap()));
    MEL_REQUIRE_EQ(upc.width, ean.width);
    for (i32 x = 0; x < upc.width; ++x)
    {
        MEL_REQUIRE_EQ(mel_barcode_matrix_get(&upc, x, 0), mel_barcode_matrix_get(&ean, x, 0));
    }
    mel_barcode_matrix_free(&upc);
    mel_barcode_matrix_free(&ean);
}
