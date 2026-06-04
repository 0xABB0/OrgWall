#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>

#include <io/stream.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    void*            buffer;
    usize            len;
    const Mel_Alloc* alloc;
} Mel_IO_Memory_Opt;

Mel_Stream* mel_io_memory_fixed_opt(Mel_IO_Memory_Opt opt);
#define mel_io_memory_fixed(...) mel_io_memory_fixed_opt((Mel_IO_Memory_Opt){ __VA_ARGS__ })

typedef struct
{
    const void*      buffer;
    usize            len;
    const Mel_Alloc* alloc;
} Mel_IO_Const_Memory_Opt;

Mel_Stream* mel_io_memory_const_opt(Mel_IO_Const_Memory_Opt opt);
#define mel_io_memory_const(...) mel_io_memory_const_opt((Mel_IO_Const_Memory_Opt){ __VA_ARGS__ })

typedef struct
{
    usize            initial_capacity;
    const Mel_Alloc* alloc;
} Mel_IO_Growable_Opt;

Mel_Stream* mel_io_memory_growable_opt(Mel_IO_Growable_Opt opt);
#define mel_io_memory_growable(...) mel_io_memory_growable_opt((Mel_IO_Growable_Opt){ __VA_ARGS__ })

usize       mel_io_growable_len(const Mel_Stream* s);
const void* mel_io_growable_data(const Mel_Stream* s);
void*       mel_io_growable_detach(Mel_Stream* s, usize* out_len);

#ifdef __cplusplus
}
#endif
