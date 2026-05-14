#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef char byte;

typedef float  f32;
typedef double f64;

typedef size_t    usize;
typedef ptrdiff_t isize;
typedef isize size;

static_assert(sizeof(f32) == 4, "f32 must be 4 bytes");
static_assert(sizeof(f64) == 8, "f64 must be 8 bytes");

#define MEL_UNUSED(x) ((void)(x))

#ifdef __cplusplus
template<class T, size_t N> char (&mel__countof_helper(T (&)[N]))[N];
#define countof(a)    (size)(sizeof(mel__countof_helper(a)))
#else
#define countof(a)    (size)(sizeof(a) / sizeof(*(a)) \
    + 0 * sizeof(char[1 - 2 * __builtin_types_compatible_p(__typeof__(a), __typeof__(&(a)[0]))]))
#endif
#define lengthof(s)   (countof(s) - 1)
