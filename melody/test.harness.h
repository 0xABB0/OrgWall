#pragma once

#include "core.types.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef struct Mel_Test_Entry Mel_Test_Entry;

struct Mel_Test_Entry
{
    const char* name;
    const char* file;
    const char* tags;
    void (*func)(void);
    Mel_Test_Entry* next;
    u32 id;
};

typedef struct
{
    u32 failed;
    const char* current_test;
    const char* current_file;
    i32 current_line;
} Mel_Test_Context;

extern Mel_Test_Context s_test_ctx;

void mel__test_register(Mel_Test_Entry* entry);
int mel_test_main(int argc, char** argv);

#define MEL_TEST(tname, ...) \
    static void test_##tname(void); \
    static Mel_Test_Entry mel__test_entry_##tname = { \
        .name = #tname, \
        .file = __FILE__, \
        .func = test_##tname, \
        __VA_ARGS__ \
    }; \
    __attribute__((constructor)) static void mel__test_register_##tname(void) { \
        mel__test_register(&mel__test_entry_##tname); \
    } \
    static void test_##tname(void)

#define MEL_TEST_MAIN() \
    int main(int argc, char** argv) { return mel_test_main(argc, argv); }

#define MEL_TEST_LOC() do { \
    s_test_ctx.current_file = __FILE__; \
    s_test_ctx.current_line = __LINE__; \
} while (0)

#define MEL_FAIL(msg) do { \
    MEL_TEST_LOC(); \
    printf("  FAIL: %s:%d - %s\n", s_test_ctx.current_file, s_test_ctx.current_line, msg); \
    s_test_ctx.failed++; \
    return; \
} while (0)

#define MEL_ASSERT(cond) do { \
    MEL_TEST_LOC(); \
    if (!(cond)) { MEL_FAIL(#cond); } \
} while (0)

#define MEL_ASSERT_EQ(a, b) do { \
    MEL_TEST_LOC(); \
    if ((a) != (b)) { MEL_FAIL(#a " != " #b); } \
} while (0)

#define MEL_ASSERT_NEQ(a, b) do { \
    MEL_TEST_LOC(); \
    if ((a) == (b)) { MEL_FAIL(#a " == " #b); } \
} while (0)

#define MEL_ASSERT_LT(a, b) do { \
    MEL_TEST_LOC(); \
    if (!((a) < (b))) { MEL_FAIL(#a " >= " #b); } \
} while (0)

#define MEL_ASSERT_LE(a, b) do { \
    MEL_TEST_LOC(); \
    if (!((a) <= (b))) { MEL_FAIL(#a " > " #b); } \
} while (0)

#define MEL_ASSERT_GT(a, b) do { \
    MEL_TEST_LOC(); \
    if (!((a) > (b))) { MEL_FAIL(#a " <= " #b); } \
} while (0)

#define MEL_ASSERT_GE(a, b) do { \
    MEL_TEST_LOC(); \
    if (!((a) >= (b))) { MEL_FAIL(#a " < " #b); } \
} while (0)

#define MEL_ASSERT_FLOAT_EQ(a, b, eps) do { \
    MEL_TEST_LOC(); \
    if (fabsf((a) - (b)) > (eps)) { MEL_FAIL(#a " !~= " #b); } \
} while (0)

#define MEL_ASSERT_NULL(ptr) do { \
    MEL_TEST_LOC(); \
    if ((ptr) != nullptr) { MEL_FAIL(#ptr " is not null"); } \
} while (0)

#define MEL_ASSERT_NOT_NULL(ptr) do { \
    MEL_TEST_LOC(); \
    if ((ptr) == nullptr) { MEL_FAIL(#ptr " is null"); } \
} while (0)

#define MEL_ASSERT_STR_EQ(a, b) do { \
    MEL_TEST_LOC(); \
    if (strcmp((a), (b)) != 0) { MEL_FAIL(#a " != " #b); } \
} while (0)
