#ifdef _CLANGD
#pragma once
#include "string.str8.h"
#endif

static inline str8 str8_from_parts(u8* data, size len)
{
    return (str8){ .data = data, .len = len };
}

static inline str8 str8_from_range(u8* begin, u8* end)
{
    return (str8){ .data = begin, .len = end - begin };
}

static inline str8 str8_from_cstr(const char* s)
{
    if (!s) return (str8){0};
    return (str8){ .data = (u8*)s, .len = (size)strlen(s) };
}

static inline bool str8_is_empty(str8 s)
{
    return s.len <= 0 || s.data == nullptr;
}

static inline bool str8_equals(str8 a, str8 b)
{
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return memcmp(a.data, b.data, (usize)a.len) == 0;
}

static inline bool str8_ieq(str8 a, str8 b)
{
    if (a.len != b.len) return false;
    for (size i = 0; i < a.len; i++)
    {
        u8 ac = a.data[i]; if (ac >= 'A' && ac <= 'Z') ac += 32;
        u8 bc = b.data[i]; if (bc >= 'A' && bc <= 'Z') bc += 32;
        if (ac != bc) return false;
    }
    return true;
}

static inline bool str8_ieq_cstr(str8 a, const char* b)
{
    size blen = (size)strlen(b);
    if (a.len != blen) return false;
    for (size i = 0; i < blen; i++)
    {
        u8 ac = a.data[i]; if (ac >= 'A' && ac <= 'Z') ac += 32;
        u8 bc = (u8)b[i]; if (bc >= 'A' && bc <= 'Z') bc += 32;
        if (ac != bc) return false;
    }
    return true;
}

static inline i32 str8_compare(str8 a, str8 b)
{
    size min_len = a.len < b.len ? a.len : b.len;
    if (min_len > 0)
    {
        i32 cmp = memcmp(a.data, b.data, (usize)min_len);
        if (cmp != 0) return cmp;
    }
    if (a.len < b.len) return -1;
    if (a.len > b.len) return  1;
    return 0;
}

static inline bool str8_starts_with(str8 s, str8 prefix)
{
    if (prefix.len > s.len) return false;
    return memcmp(s.data, prefix.data, (usize)prefix.len) == 0;
}

static inline bool str8_ends_with(str8 s, str8 suffix)
{
    if (suffix.len > s.len) return false;
    return memcmp(s.data + s.len - suffix.len, suffix.data, (usize)suffix.len) == 0;
}

static inline str8 str8_slice(str8 s, size start, size len)
{
    assert(start >= 0 && start <= s.len);
    assert(len >= 0 && start + len <= s.len);
    return (str8){ .data = s.data + start, .len = len };
}

static inline str8 str8_prefix(str8 s, size len)
{
    assert(len >= 0 && len <= s.len);
    return (str8){ .data = s.data, .len = len };
}

static inline str8 str8_suffix(str8 s, size len)
{
    assert(len >= 0 && len <= s.len);
    return (str8){ .data = s.data + s.len - len, .len = len };
}

static inline size str8_to_buf(str8 s, char* buf, size buf_size)
{
    assert(buf != nullptr);
    assert(buf_size > 0);
    size copy_len = s.len < buf_size - 1 ? s.len : buf_size - 1;
    if (copy_len > 0) memcpy(buf, s.data, (usize)copy_len);
    buf[copy_len] = '\0';
    return copy_len;
}
