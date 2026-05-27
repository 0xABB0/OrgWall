#pragma once

#include <core/types.h>
#include <string.h>

typedef void (*Mel_Test_Fn)(void);

typedef struct Mel_Test {
    const char*      suite;
    const char*      name;
    Mel_Test_Fn      fn;
    const char*      file;
    int              line;
    struct Mel_Test* next;
} Mel_Test;

void mel_test_register(Mel_Test* node);
void mel_test_fail     (const char* file, int line, const char* fmt, ...);
void mel_test_skip     (const char* reason);
void mel_test_abort    (void);

#define MEL_TEST(suite_, name_)                                                    \
    static void mel__test_##suite_##_##name_(void);                                \
    static Mel_Test mel__node_##suite_##_##name_ =                                 \
        { #suite_, #name_, mel__test_##suite_##_##name_, __FILE__, __LINE__, 0 };  \
    __attribute__((constructor))                                                   \
    static void mel__ctor_##suite_##_##name_(void) {                               \
        mel_test_register(&mel__node_##suite_##_##name_);                          \
    }                                                                              \
    static void mel__test_##suite_##_##name_(void)

// Two families share one core. MEL_EXPECT_* records the failure and continues,
// so one run reports every broken check. MEL_REQUIRE_* records and then aborts
// the test (longjmp back to the harness), for guards whose failure makes the
// rest of the test meaningless or unsafe (a null deref to follow, say).
#define MEL__CHECK(abort_, cond_, ...)                                             \
    do { if (!(cond_)) {                                                           \
        mel_test_fail(__FILE__, __LINE__, __VA_ARGS__);                            \
        if (abort_) mel_test_abort();                                              \
    } } while (0)

#define MEL_EXPECT(c)            MEL__CHECK(0, (c), "expected: %s", #c)
#define MEL_REQUIRE(c)           MEL__CHECK(1, (c), "required: %s", #c)

#define MEL_EXPECT_EQ(a, b)      MEL__CHECK(0, (a) == (b), "expected %s == %s", #a, #b)
#define MEL_REQUIRE_EQ(a, b)     MEL__CHECK(1, (a) == (b), "expected %s == %s", #a, #b)
#define MEL_EXPECT_NEQ(a, b)     MEL__CHECK(0, (a) != (b), "expected %s != %s", #a, #b)
#define MEL_REQUIRE_NEQ(a, b)    MEL__CHECK(1, (a) != (b), "expected %s != %s", #a, #b)
#define MEL_EXPECT_LT(a, b)      MEL__CHECK(0, (a) <  (b), "expected %s < %s",  #a, #b)
#define MEL_REQUIRE_LT(a, b)     MEL__CHECK(1, (a) <  (b), "expected %s < %s",  #a, #b)
#define MEL_EXPECT_LE(a, b)      MEL__CHECK(0, (a) <= (b), "expected %s <= %s", #a, #b)
#define MEL_REQUIRE_LE(a, b)     MEL__CHECK(1, (a) <= (b), "expected %s <= %s", #a, #b)
#define MEL_EXPECT_GT(a, b)      MEL__CHECK(0, (a) >  (b), "expected %s > %s",  #a, #b)
#define MEL_REQUIRE_GT(a, b)     MEL__CHECK(1, (a) >  (b), "expected %s > %s",  #a, #b)
#define MEL_EXPECT_GE(a, b)      MEL__CHECK(0, (a) >= (b), "expected %s >= %s", #a, #b)
#define MEL_REQUIRE_GE(a, b)     MEL__CHECK(1, (a) >= (b), "expected %s >= %s", #a, #b)

#define MEL_EXPECT_NULL(p)       MEL__CHECK(0, (p) == NULL, "expected %s == NULL", #p)
#define MEL_REQUIRE_NULL(p)      MEL__CHECK(1, (p) == NULL, "expected %s == NULL", #p)
#define MEL_EXPECT_NOT_NULL(p)   MEL__CHECK(0, (p) != NULL, "expected %s != NULL", #p)
#define MEL_REQUIRE_NOT_NULL(p)  MEL__CHECK(1, (p) != NULL, "expected %s != NULL", #p)

// C-string equality (strcmp). For str8, use MEL_*_EQ_STR8 with <string/str8.h>.
#define MEL_EXPECT_STR_EQ(a, b)  MEL__CHECK(0, strcmp((a), (b)) == 0, "expected %s == %s (cstr)", #a, #b)
#define MEL_REQUIRE_STR_EQ(a, b) MEL__CHECK(1, strcmp((a), (b)) == 0, "expected %s == %s (cstr)", #a, #b)

#define MEL_EXPECT_EQ_STR8(a, b)  MEL__CHECK(0, str8_equals((a), (b)), "expected %s == %s (str8)", #a, #b)
#define MEL_REQUIRE_EQ_STR8(a, b) MEL__CHECK(1, str8_equals((a), (b)), "expected %s == %s (str8)", #a, #b)

#define MEL_EXPECT_FLOAT_EQ(a, b, eps)  MEL__CHECK(0, __builtin_fabs((double)((a) - (b))) <= (eps), "expected %s ~= %s", #a, #b)
#define MEL_REQUIRE_FLOAT_EQ(a, b, eps) MEL__CHECK(1, __builtin_fabs((double)((a) - (b))) <= (eps), "expected %s ~= %s", #a, #b)

#define MEL_FAIL(msg)    do { mel_test_fail(__FILE__, __LINE__, "%s", (msg)); mel_test_abort(); } while (0)
#define MEL_SKIP(reason) do { mel_test_skip(reason); mel_test_abort(); } while (0)
