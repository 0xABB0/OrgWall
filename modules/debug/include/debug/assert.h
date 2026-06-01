#pragma once

#include "assert.cfg.h"

#if MEL_ASSERT_ENABLED

#include <core/compiler.h>
#include <string/str8.h>

void mel_assert_fail(str8 condition, str8 location);

#define MEL__ASSERT_STR2(x) #x
#define MEL__ASSERT_STR(x)  MEL__ASSERT_STR2(x)

#define mel_assert(...)                                                                    \
    do                                                                                     \
    {                                                                                      \
        if (!(__VA_ARGS__))                                                                \
        {                                                                                  \
            mel_assert_fail(S8(#__VA_ARGS__), S8(__FILE__ ":" MEL__ASSERT_STR(__LINE__))); \
            MEL_BREAKPOINT();                                                              \
        }                                                                                  \
    } while (0)

#else

#define mel_assert(...) ((void)sizeof(__VA_ARGS__))

#endif
