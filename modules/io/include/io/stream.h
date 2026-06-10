#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <future/future.h>

#include <io/status.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Vat      Mel_Vat;
typedef struct Mel_Executor Mel_Executor;

typedef struct Mel_Stream Mel_Stream;

typedef struct
{
    usize         bytes_transferred;
    i64           position;
    i32           os_error;
    Mel_IO_Status status;
} Mel_IO_Result;

typedef struct
{
    bool readable;
    bool writable;
    bool seekable;
    bool sized;
    bool growable;
    bool async;
    i64  size_bytes;
} Mel_Stream_Caps;

#define MEL_IO_SEEK_SET 0
#define MEL_IO_SEEK_CUR 1
#define MEL_IO_SEEK_END 2

typedef struct
{
    void*         buffer;
    usize         len;
    i64           offset;
    Mel_Executor* deliver;
} Mel_Stream_Read_Opt;

typedef struct
{
    const void*   buffer;
    usize         len;
    i64           offset;
    Mel_Executor* deliver;
} Mel_Stream_Write_Opt;

#define MEL_IO_NO_OFFSET ((i64) - 1)

typedef Mel_Future* (*Mel_Stream_Read_Fn)(Mel_Stream* s, void* user, Mel_Stream_Read_Opt opt);
typedef Mel_Future* (*Mel_Stream_Write_Fn)(Mel_Stream* s, void* user, Mel_Stream_Write_Opt opt);
typedef Mel_Future* (*Mel_Stream_Flush_Fn)(Mel_Stream* s, void* user, Mel_Executor* deliver);
typedef Mel_IO_Status (*Mel_Stream_Seek_Fn)(Mel_Stream* s, void* user, i64 offset, i32 whence, i64* out_pos);
typedef bool (*Mel_Stream_Size_Fn)(Mel_Stream* s, void* user, i64* out_size);
typedef void (*Mel_Stream_Close_Fn)(Mel_Stream* s, void* user);

typedef struct
{
    const char* name;

    Mel_Stream_Read_Fn  read;
    Mel_Stream_Write_Fn write;
    Mel_Stream_Flush_Fn flush;
    Mel_Stream_Seek_Fn  seek;
    Mel_Stream_Size_Fn  size;
    Mel_Stream_Close_Fn close;
} Mel_Stream_Iface;

typedef struct
{
    const Mel_Stream_Iface* iface;
    void*                   user;
    Mel_Stream_Caps         caps;
    const Mel_Alloc*        alloc;
    Mel_Vat*                vat;
    Mel_Executor*           executor;
} Mel_Stream_Opt;

Mel_Stream* mel_stream_create_opt(Mel_Stream_Opt opt);
#define mel_stream_create(...) mel_stream_create_opt((Mel_Stream_Opt){ __VA_ARGS__ })

void mel_stream_destroy(Mel_Stream* s);

Mel_Stream_Caps  mel_stream_caps(const Mel_Stream* s);
i64              mel_stream_position(const Mel_Stream* s);
const Mel_Alloc* mel_stream_alloc(const Mel_Stream* s);
Mel_Vat*         mel_stream_vat(const Mel_Stream* s);
Mel_Executor*    mel_stream_executor(const Mel_Stream* s);
void*            mel_stream_user(const Mel_Stream* s);
const char*      mel_stream_iface_name(const Mel_Stream* s);

Mel_Future* mel_stream_read_opt(Mel_Stream* s, Mel_Stream_Read_Opt opt);
#define mel_stream_read(s, ...) mel_stream_read_opt((s), (Mel_Stream_Read_Opt){ .offset = MEL_IO_NO_OFFSET, __VA_ARGS__ })

Mel_Future* mel_stream_write_opt(Mel_Stream* s, Mel_Stream_Write_Opt opt);
#define mel_stream_write(s, ...) mel_stream_write_opt((s), (Mel_Stream_Write_Opt){ .offset = MEL_IO_NO_OFFSET, __VA_ARGS__ })

Mel_Future* mel_stream_flush_ex(Mel_Stream* s, Mel_Executor* deliver);
#define mel_stream_flush(s) mel_stream_flush_ex((s), NULL)

const Mel_IO_Result* mel_stream_future_result(Mel_Future* f);
void                 mel_stream_future_release(Mel_Future* f);

Mel_IO_Status mel_stream_seek(Mel_Stream* s, i64 offset, i32 whence, i64* out_pos);
i64           mel_stream_tell(const Mel_Stream* s);
Mel_IO_Status mel_stream_size(Mel_Stream* s, i64* out_size);

Mel_IO_Result mel_stream_read_sync(Mel_Stream* s, void* buffer, usize len, i64 offset);
Mel_IO_Result mel_stream_write_sync(Mel_Stream* s, const void* buffer, usize len, i64 offset);

Mel_IO_Status mel_stream_read_exact(Mel_Stream* s, void* buffer, usize len);
Mel_IO_Status mel_stream_write_all(Mel_Stream* s, const void* buffer, usize len);

Mel_IO_Status mel_stream_read_u8(Mel_Stream* s, u8* out);
Mel_IO_Status mel_stream_write_u8(Mel_Stream* s, u8 v);

Mel_IO_Status mel_stream_read_u16_le(Mel_Stream* s, u16* out);
Mel_IO_Status mel_stream_read_u16_be(Mel_Stream* s, u16* out);
Mel_IO_Status mel_stream_read_u32_le(Mel_Stream* s, u32* out);
Mel_IO_Status mel_stream_read_u32_be(Mel_Stream* s, u32* out);
Mel_IO_Status mel_stream_read_u64_le(Mel_Stream* s, u64* out);
Mel_IO_Status mel_stream_read_u64_be(Mel_Stream* s, u64* out);

Mel_IO_Status mel_stream_write_u16_le(Mel_Stream* s, u16 v);
Mel_IO_Status mel_stream_write_u16_be(Mel_Stream* s, u16 v);
Mel_IO_Status mel_stream_write_u32_le(Mel_Stream* s, u32 v);
Mel_IO_Status mel_stream_write_u32_be(Mel_Stream* s, u32 v);
Mel_IO_Status mel_stream_write_u64_le(Mel_Stream* s, u64 v);
Mel_IO_Status mel_stream_write_u64_be(Mel_Stream* s, u64 v);

bool mel_stream_native_fd(const Mel_Stream* s, i32* out_fd);
bool mel_stream_native_file_handle(const Mel_Stream* s, void** out_handle);
bool mel_stream_native_memory(const Mel_Stream* s, void** out_base, usize* out_len);

#ifdef __cplusplus
}
#endif
