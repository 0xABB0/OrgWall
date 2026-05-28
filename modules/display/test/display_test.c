#include <test/test.h>
#include <display/display.h>

MEL_TEST(display, dead_handle_is_loud_not_fatal) {
    Mel_Display bogus = { .h = { .index = 9999, .generation = 7 } };
    MEL_EXPECT(!mel_display_alive(bogus));

    Mel_Display_Describe_Result r = mel_display_describe(bogus);
    MEL_EXPECT_EQ(r.status, MEL_DISPLAY_STATUS_INVALID_HANDLE);

    Mel_Display_Native_Handle nh = mel_display_native_handle(bogus);
    MEL_EXPECT_EQ(nh.kind, MEL_DISPLAY_NATIVE_LOST);
}

MEL_TEST(display, null_handle_is_dead) {
    Mel_Display null = MEL_DISPLAY_NULL;
    MEL_EXPECT(!mel_display_alive(null));
    MEL_EXPECT(mel_display_equal(null, null));
}

MEL_TEST(display, equal_compares_index_and_generation) {
    Mel_Display a = { .h = { .index = 3, .generation = 1 } };
    Mel_Display b = { .h = { .index = 3, .generation = 2 } };
    Mel_Display c = { .h = { .index = 3, .generation = 1 } };
    MEL_EXPECT(!mel_display_equal(a, b));
    MEL_EXPECT(mel_display_equal(a, c));
}
