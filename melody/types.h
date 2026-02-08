#ifndef MEL_TYPES_H
#define MEL_TYPES_H

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

typedef f32 f32x2 __attribute__((ext_vector_type(2)));
typedef f32 f32x3 __attribute__((ext_vector_type(3)));
typedef f32 f32x4 __attribute__((ext_vector_type(4)));

typedef i32 i32x2 __attribute__((ext_vector_type(2)));
typedef i32 i32x3 __attribute__((ext_vector_type(3)));
typedef i32 i32x4 __attribute__((ext_vector_type(4)));

typedef u32 u32x2 __attribute__((ext_vector_type(2)));
typedef u32 u32x3 __attribute__((ext_vector_type(3)));
typedef u32 u32x4 __attribute__((ext_vector_type(4)));

typedef f64 f64x2 __attribute__((ext_vector_type(2)));
typedef f64 f64x3 __attribute__((ext_vector_type(3)));
typedef f64 f64x4 __attribute__((ext_vector_type(4)));

#define MEL_UNUSED(x) ((void)(x))

#endif
