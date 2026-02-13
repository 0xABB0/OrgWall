#include "string.str8.h"
#include "hash.xxh.h"
#include "allocator.h"

#include <ctype.h>

static bool mel__is_whitespace(u8 c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

size str8_find(str8 haystack, str8 needle)
{
    if (needle.len == 0) return 0;
    if (needle.len > haystack.len) return -1;

    size limit = haystack.len - needle.len;
    for (size i = 0; i <= limit; i++)
    {
        if (memcmp(haystack.data + i, needle.data, (usize)needle.len) == 0)
            return i;
    }
    return -1;
}

size str8_rfind(str8 haystack, str8 needle)
{
    if (needle.len == 0) return haystack.len;
    if (needle.len > haystack.len) return -1;

    for (size i = haystack.len - needle.len; i >= 0; i--)
    {
        if (memcmp(haystack.data + i, needle.data, (usize)needle.len) == 0)
            return i;
    }
    return -1;
}

bool str8_contains(str8 haystack, str8 needle)
{
    return str8_find(haystack, needle) >= 0;
}

str8 str8_trim_left(str8 s)
{
    size i = 0;
    while (i < s.len && mel__is_whitespace(s.data[i])) i++;
    return (str8){ .data = s.data + i, .len = s.len - i };
}

str8 str8_trim_right(str8 s)
{
    size len = s.len;
    while (len > 0 && mel__is_whitespace(s.data[len - 1])) len--;
    return (str8){ .data = s.data, .len = len };
}

str8 str8_trim(str8 s)
{
    return str8_trim_left(str8_trim_right(s));
}

u64 str8_hash(str8 s)
{
    return mel_xxh3_64(s.data, (usize)s.len);
}

str8 str8_dup(str8 s, const Mel_Alloc* alloc)
{
    if (s.len <= 0 || s.data == nullptr) return (str8){0};
    u8* copy = (u8*)mel_alloc(alloc, (usize)s.len);
    memcpy(copy, s.data, (usize)s.len);
    return (str8){ .data = copy, .len = s.len };
}

const char* str8_to_cstr(str8 s, const Mel_Alloc* alloc)
{
    char* buf = (char*)mel_alloc(alloc, (usize)(s.len + 1));
    if (s.len > 0) memcpy(buf, s.data, (usize)s.len);
    buf[s.len] = '\0';
    return buf;
}

size str8_levenshtein(str8 a, str8 b, const Mel_Alloc* alloc)
{
    if (a.len == 0) return b.len;
    if (b.len == 0) return a.len;

    if (a.len < b.len) { str8 tmp = a; a = b; b = tmp; }

    size cols = b.len + 1;
    size* prev = (size*)mel_alloc(alloc, (usize)(cols * (size)sizeof(size)));
    size* curr = (size*)mel_alloc(alloc, (usize)(cols * (size)sizeof(size)));

    for (size j = 0; j < cols; j++) prev[j] = j;

    for (size i = 1; i <= a.len; i++)
    {
        curr[0] = i;
        for (size j = 1; j <= b.len; j++)
        {
            size cost = (a.data[i - 1] == b.data[j - 1]) ? 0 : 1;
            size del = prev[j] + 1;
            size ins = curr[j - 1] + 1;
            size sub = prev[j - 1] + cost;

            size min = del;
            if (ins < min) min = ins;
            if (sub < min) min = sub;
            curr[j] = min;
        }
        size* tmp = prev;
        prev = curr;
        curr = tmp;
    }

    size result = prev[b.len];
    mel_dealloc(alloc, prev);
    mel_dealloc(alloc, curr);
    return result;
}

str8 str8_fmt(const Mel_Alloc* alloc, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list args_copy;
    va_copy(args_copy, args);

    i32 needed = vsnprintf(nullptr, 0, fmt, args);
    va_end(args);

    assert(needed >= 0);

    u8* buf = (u8*)mel_alloc(alloc, (usize)(needed + 1));
    vsnprintf((char*)buf, (usize)(needed + 1), fmt, args_copy);
    va_end(args_copy);

    return (str8){ .data = buf, .len = (size)needed };
}
