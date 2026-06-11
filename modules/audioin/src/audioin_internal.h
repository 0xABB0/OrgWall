#pragma once

#include <audioin/audioin.h>
#include <audioin/events.h>
#include <audioin/permission.h>
#include <audioin/os.h>
#include <audioin/provider.h>

struct mel_audioin_kind
{
    const char* name;
};

struct mel_audioin_auth
{
    const char* name;
    bool        granted;
    u32         restrictiveness;
};

Mel_AudioIn_Status mel_audioin__open(Mel_AudioIn d, Mel_AudioIn_Sink sink);
void               mel_audioin__close(Mel_AudioIn d, void* token);

void mel_audioin__publish_register_provider(const Mel_Alloc* alloc);
