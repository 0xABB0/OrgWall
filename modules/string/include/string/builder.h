#pragma once

#include <string/str8.h>

#include <allocator/allocator.h>
#include <collection/llist.h>

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct Mel_String_Builder Mel_String_Builder;

struct Mel_String_Builder
{
    Mel_LList(str8) parts;
    size            total;
};

static inline void mel_sb_init(Mel_String_Builder* sb, const Mel_Alloc* alloc)
{
    mel_llist_init(&sb->parts, alloc);
    sb->total = 0;
}

static inline void mel_sb_reset(Mel_String_Builder* sb)
{
    mel_llist_foreach(&sb->parts, node, { mel_dealloc(sb->parts.allocator, node->value.data); });
    mel_llist_clear(&sb->parts);
    sb->total = 0;
}

static inline bool mel_sb_append_buf(Mel_String_Builder* sb, const void* buf, size len)
{
    if (len <= 0) return true;
    u8* data = (u8*)mel_alloc(sb->parts.allocator, (usize)len);
    if (!data) return false;
    memcpy(data, buf, (usize)len);
    str8 s = str8_from_parts(data, len);
    mel_llist_push_back(&sb->parts, s);
    sb->total += len;
    return true;
}

static inline bool mel_sb_append_str8(Mel_String_Builder* sb, str8 s)
{
    return mel_sb_append_buf(sb, s.data, s.len);
}

static inline bool mel_sb_append_cstr(Mel_String_Builder* sb, const char* s)
{
    return mel_sb_append_buf(sb, s, (size)strlen(s));
}

static inline bool mel_sb_append_null(Mel_String_Builder* sb)
{
    u8 nul = '\0';
    return mel_sb_append_buf(sb, &nul, 1);
}

static inline bool mel_sb_append_fmt(Mel_String_Builder* sb, const char* fmt, ...) MEL_PRINTF_FORMAT(2, 3);

static inline bool mel_sb_append_fmt(Mel_String_Builder* sb, const char* fmt, ...)
{
    va_list args, args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);
    i32 needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) { va_end(args_copy); return false; }
    u8* data = (u8*)mel_alloc(sb->parts.allocator, (usize)(needed + 1));
    if (!data) { va_end(args_copy); return false; }
    vsnprintf((char*)data, (usize)(needed + 1), fmt, args_copy);
    va_end(args_copy);
    str8 s = str8_from_parts(data, (size)needed);
    mel_llist_push_back(&sb->parts, s);
    sb->total += (size)needed;
    return true;
}

static inline str8 mel_sb_to_str8(Mel_String_Builder* sb, const Mel_Alloc* alloc)
{
    if (sb->total <= 0) return (str8){ 0 };
    u8* data = (u8*)mel_alloc(alloc, (usize)sb->total);
    if (!data) return (str8){ 0 };
    size offset = 0;
    mel_llist_foreach(&sb->parts, node, {
        str8 s = node->value;
        memcpy(data + offset, s.data, (usize)s.len);
        offset += s.len;
    });
    return str8_from_parts(data, sb->total);
}
