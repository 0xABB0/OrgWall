#pragma once

#include "core.types.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef struct
{
    const char* name;
    void (*func)(void);
} Mel_Test;

typedef struct
{
    u32 passed;
    u32 failed;
    u32 total;
    const char* current_test;
    const char* current_file;
    i32 current_line;
} Mel_Test_Context;

static Mel_Test_Context s_test_ctx = {0};

#define MEL_TEST(name) static void test_##name(void)

#define MEL_RUN_TEST(name) do { \
    s_test_ctx.current_test = #name; \
    s_test_ctx.total++; \
    test_##name(); \
} while (0)

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

#define MEL_PASS() do { \
    s_test_ctx.passed++; \
    printf("  PASS: %s\n", s_test_ctx.current_test); \
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

#define MEL_TEST_BEGIN(suite_name) do { \
    printf("\n=== %s ===\n", suite_name); \
    s_test_ctx = (Mel_Test_Context){0}; \
} while (0)

#define MEL_TEST_END() do { \
    printf("\nResults: %u/%u passed", s_test_ctx.passed, s_test_ctx.total); \
    if (s_test_ctx.failed > 0) { \
        printf(" (%u FAILED)", s_test_ctx.failed); \
    } \
    printf("\n\n"); \
} while (0)

#define MEL_TEST_EXIT_CODE() (s_test_ctx.failed > 0 ? 1 : 0)
