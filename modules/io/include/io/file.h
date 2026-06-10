#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <future/future.h>

#include <io/stream.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Vat      Mel_Vat;
typedef struct Mel_Executor Mel_Executor;

#define MEL_IO_FILE_READ     (1u << 0)
#define MEL_IO_FILE_WRITE    (1u << 1)
#define MEL_IO_FILE_CREATE   (1u << 2)
#define MEL_IO_FILE_TRUNCATE (1u << 3)
#define MEL_IO_FILE_APPEND   (1u << 4)
#define MEL_IO_FILE_EXCL     (1u << 5)

typedef struct
{
    const char*      path;
    u32              flags;
    u32              mode;
    Mel_Vat*         vat;
    const Mel_Alloc* alloc;
} Mel_IO_File_Open_Opt;

typedef struct
{
    Mel_Stream*   value;
    Mel_IO_Status status;
} Mel_IO_File_Open_Result;

Mel_IO_File_Open_Result mel_io_file_open_opt(Mel_IO_File_Open_Opt opt);
#define mel_io_file_open(...) mel_io_file_open_opt((Mel_IO_File_Open_Opt){ .mode = 0644, __VA_ARGS__ })

bool mel_io_file_available(void);

typedef struct
{
    u8*           data;
    usize         len;
    Mel_IO_Status status;
} Mel_IO_Blob;

typedef struct
{
    const char*      path;
    Mel_Vat*         vat;
    const Mel_Alloc* alloc;
} Mel_IO_Load_Opt;

Mel_Future* mel_io_load_file_opt(Mel_IO_Load_Opt opt);
#define mel_io_load_file(...) mel_io_load_file_opt((Mel_IO_Load_Opt){ __VA_ARGS__ })

typedef struct
{
    const char*      path;
    const void*      data;
    usize            len;
    u32              flags;
    u32              mode;
    Mel_Vat*         vat;
    const Mel_Alloc* alloc;
} Mel_IO_Save_Opt;

Mel_Future* mel_io_save_file_opt(Mel_IO_Save_Opt opt);
#define mel_io_save_file(...) mel_io_save_file_opt((Mel_IO_Save_Opt){ .mode = 0644, __VA_ARGS__ })

const Mel_IO_Blob* mel_io_load_future_result(Mel_Future* f);
void               mel_io_load_future_release(Mel_Future* f);

const Mel_IO_Result* mel_io_save_future_result(Mel_Future* f);
void                 mel_io_save_future_release(Mel_Future* f);

#ifdef __cplusplus
}
#endif
