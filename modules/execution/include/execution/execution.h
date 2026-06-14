#pragma once

#include <allocator/allocator.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Execution_Sender Mel_Execution_Sender;

typedef void* (*Mel_Execution_Work)(void* ctx);

Mel_Execution_Sender* mel_execution_sender_create(const Mel_Alloc* alloc, Mel_Execution_Work work, void* ctx);
void*                 mel_execution_sync_wait(Mel_Execution_Sender* sender);
void                  mel_execution_sender_destroy(const Mel_Alloc* alloc, Mel_Execution_Sender* sender);

#ifdef __cplusplus
}
#endif
