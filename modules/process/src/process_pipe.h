#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>

#include <io/stream.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Reactor Mel_Reactor;

Mel_Stream* mel_process__pipe_stream(i32 fd, bool readable, bool writable, Mel_Reactor* reactor, const Mel_Alloc* alloc);

bool mel_process__pipe_fd(const Mel_Stream* s, i32* out_fd);

#ifdef __cplusplus
}
#endif
