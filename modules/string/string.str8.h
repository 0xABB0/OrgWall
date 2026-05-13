#pragma once

#include <core/compiler.h>

#include "string.str8.fwd.h"
#include "core.defs.h"
#include "allocator.fwd.h"
#include "allocator.arena.fwd.h"
#include "collection.array.h"
#include "allocator.heap.h"

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
static inline bool str8_ieq(str8 a, str8 b);
static inline bool str8_ieq_cstr(str8 a, const char* b);
static inline i32  str8_compare(str8 a, str8 b);
static inline bool str8_starts_with(str8 s, str8 prefix);
static inline bool str8_ends_with(str8 s, str8 suffix);
static inline str8 str8_slice(str8 s, size start, size len);
static inline str8 str8_prefix(str8 s, size len);
static inline str8 str8_suffix(str8 s, size len);
static inline size str8_to_buf(str8 s, char* buf, size buf_size);

bool        str8_contains(str8 haystack, str8 needle);
size        str8_find(str8 haystack, str8 needle);
size        str8_rfind(str8 haystack, str8 needle);
str8        str8_trim(str8 s);
str8        str8_trim_left(str8 s);
str8        str8_trim_right(str8 s);
u64         str8_hash(str8 s);

str8        str8_dup_arena(str8 s, Mel_Arena* arena);
str8        str8_dup_alloc(str8 s, const Mel_Alloc* alloc);

const char* str8_to_cstr_arena(str8 s, Mel_Arena* arena);
const char* str8_to_cstr_alloc(str8 s, const Mel_Alloc* alloc);

str8        str8_fmt_arena(Mel_Arena* arena, const char* fmt, ...) MEL_PRINTF_FORMAT(2, 3);
str8        str8_fmt_alloc(const Mel_Alloc* alloc, const char* fmt, ...) MEL_PRINTF_FORMAT(2, 3);

size        str8_levenshtein_arena(str8 a, str8 b, Mel_Arena* arena);
size        str8_levenshtein_alloc(str8 a, str8 b, const Mel_Alloc* alloc);

#define str8_dup(s, allocator) _Generic((allocator), \
    Mel_Arena*: str8_dup_arena, \
    const Mel_Alloc*: str8_dup_alloc \
)(s, allocator)

#define str8_to_cstr(s, allocator) _Generic((allocator), \
    Mel_Arena*: str8_to_cstr_arena, \
    const Mel_Alloc*: str8_to_cstr_alloc \
)(s, allocator)

#define str8_fmt(allocator, fmt, ...) _Generic((allocator), \
    Mel_Arena*: str8_fmt_arena, \
    const Mel_Alloc*: str8_fmt_alloc \
)(allocator, fmt, ##__VA_ARGS__)

#define str8_levenshtein(a, b, allocator) _Generic((allocator), \
    Mel_Arena*: str8_levenshtein_arena, \
    const Mel_Alloc*: str8_levenshtein_alloc \
)(a, b, allocator)

typedef Mel_Array(char) Mel_String_Builder;

#define mel_sb_append_buf(sb, buf, size)                            \
    do {                                                            \
        if ((sb)->allocator == NULL) mel_array_init((sb), mel_alloc_heap()); \
        usize __mel_sb_n = (size);                                  \
        mel_array_reserve((sb), (sb)->count + __mel_sb_n);           \
        memcpy((sb)->items + (sb)->count, (buf), __mel_sb_n);        \
        (sb)->count += __mel_sb_n;                                   \
    } while (0)

#define mel_sb_append_cstr(sb, cstr)                \
    do {                                            \
        const char *__mel_s = (cstr);               \
        mel_sb_append_buf((sb), __mel_s, strlen(__mel_s)); \
    } while (0)

#define mel_sb_append_null(sb) mel_array_push((sb), '\0')

#define mel_sb_free(sb) mel_array_free(sb)

#define mel_sb_to_str8(sb) str8_from_parts((u8*)(sb)->items, (sb)->count)

#include "string.str8.inl"
