#pragma once

#include <io/stream.h>
#include <io/status.h>

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <future/future.h>
#include <executor/executor.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    Mel_Future       future;
    Mel_IO_Result    result;
    const Mel_Alloc* alloc;
    bool             owned;
} Mel_IO_Op;

struct Mel_Stream
{
    const Mel_Stream_Iface* iface;
    void*                   user;
    Mel_Stream_Caps         caps;
    const Mel_Alloc*        alloc;
    Mel_Vat*                vat;
    Mel_Executor*           executor;
    i64                     position;
    Mel_IO_Op               scratch;
    bool                    scratch_busy;
};

Mel_IO_Op*  mel_io__op_new(const Mel_Alloc* alloc);
Mel_IO_Op*  mel_io__op_sync(Mel_Stream* s);
Mel_Future* mel_io__op_resolve(Mel_IO_Op* op, usize bytes, i64 position, i32 os_error, Mel_IO_Status status);

Mel_Future_Status mel_io__future_status_from(Mel_IO_Status status);

void mel_stream__set_position(Mel_Stream* s, i64 position);
i64  mel_stream__position(const Mel_Stream* s);

const Mel_Stream_Iface* mel_io__file_iface(void);
const Mel_Stream_Iface* mel_io__memory_rw_iface(void);
const Mel_Stream_Iface* mel_io__memory_const_iface(void);
const Mel_Stream_Iface* mel_io__memory_growable_iface(void);

bool mel_io__file_native_fd(const Mel_Stream* s, i32* out_fd);
bool mel_io__file_native_handle(const Mel_Stream* s, void** out_handle);
bool mel_io__memory_native(const Mel_Stream* s, void** out_base, usize* out_len);

#ifdef __cplusplus
}
#endif
