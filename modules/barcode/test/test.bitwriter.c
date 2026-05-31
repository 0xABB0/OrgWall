#include <barcode/bitwriter.h>
#include <allocator/heap.h>
#include <test/test.h>

MEL_TEST(barcode_bitwriter, msb_first_packing) {
    mel_bitwriter w;
    mel_bitwriter_init(&w, mel_alloc_heap());

    mel_bitwriter_put(&w, 0x5, 3);
    mel_bitwriter_put(&w, 0x1, 1);
    MEL_REQUIRE_EQ((i32)mel_bitwriter_bit_length(&w), 4);

    mel_bitwriter_pad_to_byte(&w);
    MEL_REQUIRE_EQ((i32)mel_bitwriter_byte_count(&w), 1);
    MEL_REQUIRE_EQ((i32)mel_bitwriter_bytes(&w)[0], 0xB0);

    mel_bitwriter_free(&w);
}

MEL_TEST(barcode_bitwriter, byte_aligned_stream) {
    mel_bitwriter w;
    mel_bitwriter_init(&w, mel_alloc_heap());

    mel_bitwriter_put(&w, 0xC3, 8);
    mel_bitwriter_put(&w, 0xA5, 8);
    MEL_REQUIRE_EQ((i32)mel_bitwriter_byte_count(&w), 2);
    MEL_REQUIRE_EQ((i32)mel_bitwriter_bytes(&w)[0], 0xC3);
    MEL_REQUIRE_EQ((i32)mel_bitwriter_bytes(&w)[1], 0xA5);
    MEL_REQUIRE_EQ((i32)mel_bitwriter_bit_length(&w), 16);

    mel_bitwriter_free(&w);
}

MEL_TEST(barcode_bitwriter, spanning_byte_boundary) {
    mel_bitwriter w;
    mel_bitwriter_init(&w, mel_alloc_heap());

    mel_bitwriter_put(&w, 0x2AA, 10);
    mel_bitwriter_pad_to_byte(&w);
    MEL_REQUIRE_EQ((i32)mel_bitwriter_byte_count(&w), 2);
    MEL_REQUIRE_EQ((i32)mel_bitwriter_bytes(&w)[0], 0xAA);
    MEL_REQUIRE_EQ((i32)mel_bitwriter_bytes(&w)[1], 0x80);

    mel_bitwriter_free(&w);
}
