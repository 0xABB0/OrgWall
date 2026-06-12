#pragma once

#include <audioin/provider.h>

#include <allocator/allocator.fwd.h>

#include <stdatomic.h>

typedef struct Mel_AudioIn__Macos_Sinks Mel_AudioIn__Macos_Sinks;

struct Mel_AudioIn__Macos_Sinks
{
    Mel_AudioIn__Macos_Sinks* next;
    u32                       count;
    Mel_AudioIn_Sink          sinks[];
};

Mel_AudioIn__Macos_Sinks* mel_audioin__macos_sinks_with(const Mel_Alloc* alloc, const Mel_AudioIn__Macos_Sinks* cur, Mel_AudioIn_Sink sink);
Mel_AudioIn__Macos_Sinks* mel_audioin__macos_sinks_without(const Mel_Alloc* alloc, const Mel_AudioIn__Macos_Sinks* cur, void* token);
void                      mel_audioin__macos_garbage_push(_Atomic(Mel_AudioIn__Macos_Sinks*)* head, Mel_AudioIn__Macos_Sinks* sl);
void                      mel_audioin__macos_garbage_drain(const Mel_Alloc* alloc, _Atomic(Mel_AudioIn__Macos_Sinks*)* head);

u64 mel_audioin__macos_host_ns(u64 host_time);

const mel_audioin_auth* mel_audioin__macos_authorization(void);
void                    mel_audioin__macos_authorize(Mel_AudioIn_Sink sink);

bool               mel_audioin__macos_loopback_available(void);
Mel_AudioIn_Status mel_audioin__macos_loopback_open(const Mel_Alloc* alloc, Mel_AudioIn_Sink sink, Mel_AudioIn_Open_Opt opt, Mel_AudioIn_Granted* granted);
void               mel_audioin__macos_loopback_close(void* token);
void*              mel_audioin__macos_loopback_native(void);
void               mel_audioin__macos_loopback_shutdown(void);
