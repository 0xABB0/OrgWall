#pragma once

#include "string.str8.fwd.h"
#include "defs.h"
#include "allocator.fwd.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define S8(s) ((str8){(u8*)(s), lengthof(s)})
#define STR8_EMPTY ((str8){0})

static inline str8 str8_from_parts(u8* data, size len);
static inline str8 str8_from_range(u8* begin, u8* end);
static inline str8 str8_from_cstr(const char* s);
static inline bool str8_is_empty(str8 s);
static inline bool str8_equals(str8 a, str8 b);
static inline i32  str8_compare(str8 a, str8 b);
static inline bool str8_starts_with(str8 s, str8 prefix);
static inline bool str8_ends_with(str8 s, str8 suffix);
static inline str8 str8_slice(str8 s, size start, size len);
static inline str8 str8_prefix(str8 s, size len);
static inline str8 str8_suffix(str8 s, size len);
static inline size str8_to_buf(str8 s, char* buf, size buf_size);

bool     str8_contains(str8 haystack, str8 needle);
size     str8_find(str8 haystack, str8 needle);
size     str8_rfind(str8 haystack, str8 needle);
str8     str8_trim(str8 s);
str8     str8_trim_left(str8 s);
str8     str8_trim_right(str8 s);
u64      str8_hash(str8 s);
str8     str8_dup(str8 s, const Mel_Alloc* alloc);
const char* str8_to_cstr(str8 s, const Mel_Alloc* alloc);
str8     str8_fmt(const Mel_Alloc* alloc, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

#include "string.str8.inl"
