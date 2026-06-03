#pragma once

#include <core/types.h>

#include <io/status.h>
#include <io/file.h>
#include <io/stream.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    i32           fd;
    void*         handle;
    i64           initial_size;
    bool          seekable;
    Mel_IO_Status status;
} Mel_IO_File_Native;

bool mel_io__backend_available(void);

Mel_IO_File_Native mel_io__backend_open(const char* path, u32 flags, u32 mode);
void               mel_io__backend_close(Mel_IO_File_Native native);

Mel_IO_Status mel_io__backend_pio(Mel_IO_File_Native native, bool is_read, void* buffer, usize len, i64 offset, Mel_IO_Result* out);
Mel_IO_Status mel_io__backend_seek(Mel_IO_File_Native native, i64 offset, i32 whence, i64* out_pos);
bool          mel_io__backend_size(Mel_IO_File_Native native, i64* out_size);
Mel_IO_Status mel_io__backend_flush(Mel_IO_File_Native native);

#ifdef __cplusplus
}
#endif
