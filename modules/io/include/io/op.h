#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <future/future.h>

#include <io/status.h>
#include <io/stream.h>

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

#ifdef __cplusplus
}
#endif
