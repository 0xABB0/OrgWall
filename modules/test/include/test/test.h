#pragma once

#include <core/types.h>

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

#define MEL_EXPECT(cond)                                                           \
    do { if (!(cond)) mel_test_fail(__FILE__, __LINE__, "expected: %s", #cond); }  \
    while (0)

#define MEL_REQUIRE(cond)                                                          \
    do { if (!(cond)) {                                                            \
        mel_test_fail(__FILE__, __LINE__, "required: %s", #cond);                  \
        mel_test_abort();                                                          \
    } } while (0)

#define MEL_EXPECT_EQ_INT(a, b)                                                    \
    do { i64 mel__a = (i64)(a), mel__b = (i64)(b);                                 \
        if (mel__a != mel__b)                                                      \
            mel_test_fail(__FILE__, __LINE__, "expected %s == %s (%lld vs %lld)",  \
                          #a, #b, (long long)mel__a, (long long)mel__b);           \
    } while (0)

// Requires <string/str8.h> at the use site.
#define MEL_EXPECT_EQ_STR8(a, b)                                                   \
    do { if (!str8_equals((a), (b)))                                               \
        mel_test_fail(__FILE__, __LINE__, "expected %s == %s", #a, #b); }          \
    while (0)

#define MEL_FAIL(msg)    mel_test_fail(__FILE__, __LINE__, "%s", (msg))
#define MEL_SKIP(reason) do { mel_test_skip(reason); mel_test_abort(); } while (0)
