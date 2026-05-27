#include <test/test.h>

MEL_TEST(sanity, arithmetic) {
    MEL_EXPECT(1 + 1 == 2);
    MEL_EXPECT_EQ(6 * 7, 42);
}

MEL_TEST(sanity, require_short_circuits) {
    int* p = NULL;
    MEL_REQUIRE(p == NULL);
    MEL_EXPECT(p == NULL);
}

MEL_TEST(sanity, skip_example) {
    MEL_SKIP("demonstrates the skip path");
    MEL_FAIL("unreachable after skip");
}
