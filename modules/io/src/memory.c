#include "io_internal.h"

#include <io/memory.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <log/log.h>

#include <string.h>

typedef struct
{
    u8*   base;
    usize len;
    usize capacity;
    bool  owns;
    bool  growable;
} Mem_State;

static i64 resolve_offset(Mel_Stream* s, i64 offset) { return offset == MEL_IO_NO_OFFSET ? mel_stream__position(s) : offset; }

static Mel_Future* mem_read(Mel_Stream* s, void* user, Mel_Stream_Read_Opt opt)
{
    Mem_State* m = (Mem_State*)user;
    Mel_IO_Op* op = mel_io__op_new(s->alloc);
    if (!op)
        return NULL;

    i64 pos = resolve_offset(s, opt.offset);
    if (pos < 0 || (usize)pos > m->len)
        return mel_io__op_resolve(op, 0, mel_stream__position(s), 0, MEL_IO_ERROR | MEL_IO_BAD_HANDLE);

    usize         avail = m->len - (usize)pos;
    usize         n = opt.len < avail ? opt.len : avail;
    Mel_IO_Status st = MEL_IO_OK;
    if (n < opt.len)
        st |= MEL_IO_EOF | (n > 0 ? MEL_IO_PARTIAL : 0u);
    if (n > 0)
        memcpy(opt.buffer, m->base + pos, n);

    i64 new_pos = pos + (i64)n;
    if (opt.offset == MEL_IO_NO_OFFSET)
        mel_stream__set_position(s, new_pos);
    return mel_io__op_resolve(op, n, new_pos, 0, st);
}

static bool grow_to(Mem_State* m, Mel_Stream* s, usize need)
{
    if (need <= m->capacity)
        return true;
    usize cap = m->capacity ? m->capacity : 16;
    while (cap < need)
    {
        if (cap > SIZE_MAX / 2)
        {
            cap = need;
            break;
        }
        cap *= 2;
    }
    u8* nb = m->base ? mel_realloc(s->alloc, m->base, cap) : mel_alloc(s->alloc, cap);
    if (!nb)
        return false;
    m->base = nb;
    m->capacity = cap;
    return true;
}

static Mel_Future* mem_write(Mel_Stream* s, void* user, Mel_Stream_Write_Opt opt)
{
    Mem_State* m = (Mem_State*)user;
    Mel_IO_Op* op = mel_io__op_new(s->alloc);
    if (!op)
        return NULL;

    i64 pos = resolve_offset(s, opt.offset);
    if (pos < 0)
        return mel_io__op_resolve(op, 0, mel_stream__position(s), 0, MEL_IO_ERROR | MEL_IO_BAD_HANDLE);

    if (opt.len > SIZE_MAX - (usize)pos)
        return mel_io__op_resolve(op, 0, pos, 0, MEL_IO_ERROR | MEL_IO_NO_SPACE);
    usize end = (usize)pos + opt.len;

    if (m->growable)
    {
        if (!grow_to(m, s, end))
            return mel_io__op_resolve(op, 0, pos, 0, MEL_IO_ERROR | MEL_IO_NO_SPACE);
        if (end > m->len)
            m->len = end;
        memcpy(m->base + pos, opt.buffer, opt.len);
        i64 new_pos = (i64)end;
        if (opt.offset == MEL_IO_NO_OFFSET)
            mel_stream__set_position(s, new_pos);
        return mel_io__op_resolve(op, opt.len, new_pos, 0, MEL_IO_OK);
    }

    if ((usize)pos > m->capacity)
        return mel_io__op_resolve(op, 0, pos, 0, MEL_IO_ERROR | MEL_IO_NO_SPACE);
    usize         room = m->capacity - (usize)pos;
    usize         n = opt.len < room ? opt.len : room;
    Mel_IO_Status st = MEL_IO_OK;
    if (n < opt.len)
        st |= MEL_IO_NO_SPACE | (n > 0 ? MEL_IO_PARTIAL : 0u) | (n > 0 ? 0u : MEL_IO_ERROR);
    if (n > 0)
        memcpy(m->base + pos, opt.buffer, n);
    if ((usize)pos + n > m->len)
        m->len = (usize)pos + n;
    i64 new_pos = pos + (i64)n;
    if (opt.offset == MEL_IO_NO_OFFSET)
        mel_stream__set_position(s, new_pos);
    return mel_io__op_resolve(op, n, new_pos, 0, st);
}

static Mel_IO_Status mem_seek(Mel_Stream* s, void* user, i64 offset, i32 whence, i64* out_pos)
{
    Mem_State* m = (Mem_State*)user;
    i64        base;
    switch (whence)
    {
    case MEL_IO_SEEK_SET:
        base = 0;
        break;
    case MEL_IO_SEEK_CUR:
        base = mel_stream__position(s);
        break;
    case MEL_IO_SEEK_END:
        base = (i64)m->len;
        break;
    default:
        return MEL_IO_ERROR | MEL_IO_NOT_SEEKABLE;
    }
    i64 target = base + offset;
    if (target < 0)
        return MEL_IO_ERROR | MEL_IO_BAD_HANDLE;
    *out_pos = target;
    return MEL_IO_OK;
}

static bool mem_size(Mel_Stream* s, void* user, i64* out_size)
{
    (void)s;
    Mem_State* m = (Mem_State*)user;
    *out_size = (i64)m->len;
    return true;
}

static void mem_close(Mel_Stream* s, void* user)
{
    Mem_State* m = (Mem_State*)user;
    if (m->owns && m->base)
        mel_dealloc(s->alloc, m->base);
    mel_dealloc(s->alloc, m);
}

static const Mel_Stream_Iface MEM_RW_IFACE = {
    .name = "memory-fixed",
    .read = mem_read,
    .write = mem_write,
    .seek = mem_seek,
    .size = mem_size,
    .close = mem_close,
};

static const Mel_Stream_Iface MEM_CONST_IFACE = {
    .name = "memory-const",
    .read = mem_read,
    .write = NULL,
    .seek = mem_seek,
    .size = mem_size,
    .close = mem_close,
};

static const Mel_Stream_Iface MEM_GROW_IFACE = {
    .name = "memory-growable",
    .read = mem_read,
    .write = mem_write,
    .seek = mem_seek,
    .size = mem_size,
    .close = mem_close,
};

const Mel_Stream_Iface* mel_io__memory_rw_iface(void) { return &MEM_RW_IFACE; }
const Mel_Stream_Iface* mel_io__memory_const_iface(void) { return &MEM_CONST_IFACE; }
const Mel_Stream_Iface* mel_io__memory_growable_iface(void) { return &MEM_GROW_IFACE; }

bool mel_io__memory_native(const Mel_Stream* s, void** out_base, usize* out_len)
{
    Mem_State* m = (Mem_State*)mel_stream_user(s);
    if (!m)
        return false;
    if (out_base)
        *out_base = m->base;
    if (out_len)
        *out_len = m->len;
    return true;
}

Mel_Stream* mel_io_memory_fixed_opt(Mel_IO_Memory_Opt opt)
{
    if (!opt.buffer && opt.len > 0)
    {
        mel_log_error("io", "memory_fixed: null buffer with non-zero len");
        return NULL;
    }
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mem_State*       m = mel_alloc_type(alloc, Mem_State);
    if (!m)
        return NULL;
    *m = (Mem_State){ .base = (u8*)opt.buffer, .len = opt.len, .capacity = opt.len, .owns = false, .growable = false };
    return mel_stream_create(.iface = &MEM_RW_IFACE, .user = m, .alloc = alloc, .caps = { .readable = true, .writable = true, .seekable = true, .sized = true, .size_bytes = (i64)opt.len });
}

Mel_Stream* mel_io_memory_const_opt(Mel_IO_Const_Memory_Opt opt)
{
    if (!opt.buffer && opt.len > 0)
    {
        mel_log_error("io", "memory_const: null buffer with non-zero len");
        return NULL;
    }
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mem_State*       m = mel_alloc_type(alloc, Mem_State);
    if (!m)
        return NULL;
    *m = (Mem_State){ .base = (u8*)opt.buffer, .len = opt.len, .capacity = opt.len, .owns = false, .growable = false };
    return mel_stream_create(.iface = &MEM_CONST_IFACE, .user = m, .alloc = alloc, .caps = { .readable = true, .writable = false, .seekable = true, .sized = true, .size_bytes = (i64)opt.len });
}

Mel_Stream* mel_io_memory_growable_opt(Mel_IO_Growable_Opt opt)
{
    const Mel_Alloc* alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    Mem_State*       m = mel_alloc_type(alloc, Mem_State);
    if (!m)
        return NULL;
    *m = (Mem_State){ .base = NULL, .len = 0, .capacity = 0, .owns = true, .growable = true };
    if (opt.initial_capacity > 0)
    {
        m->base = mel_alloc(alloc, opt.initial_capacity);
        if (!m->base)
        {
            mel_dealloc(alloc, m);
            return NULL;
        }
        m->capacity = opt.initial_capacity;
    }
    return mel_stream_create(.iface = &MEM_GROW_IFACE, .user = m, .alloc = alloc, .caps = { .readable = true, .writable = true, .seekable = true, .sized = true, .growable = true });
}

usize mel_io_growable_len(const Mel_Stream* s)
{
    if (!s || s->iface != &MEM_GROW_IFACE)
        return 0;
    Mem_State* m = (Mem_State*)mel_stream_user(s);
    return m ? m->len : 0;
}

const void* mel_io_growable_data(const Mel_Stream* s)
{
    if (!s || s->iface != &MEM_GROW_IFACE)
        return NULL;
    Mem_State* m = (Mem_State*)mel_stream_user(s);
    return m ? m->base : NULL;
}

void* mel_io_growable_detach(Mel_Stream* s, usize* out_len)
{
    if (!s || s->iface != &MEM_GROW_IFACE)
        return NULL;
    Mem_State* m = (Mem_State*)mel_stream_user(s);
    if (!m)
        return NULL;
    void* base = m->base;
    if (out_len)
        *out_len = m->len;
    m->base = NULL;
    m->len = 0;
    m->capacity = 0;
    m->owns = false;
    return base;
}
