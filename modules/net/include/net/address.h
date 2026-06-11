#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <string/str8.fwd.h>

#include <net/net.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u8   bytes[16];
    u32  scope_id;
    u16  port;
    bool v6;
} Mel_Net_Address;

Mel_Net_Status mel_net_address_parse(str8 text, u16 port, Mel_Net_Address* out);
str8           mel_net_address_format(const Mel_Net_Address* addr, const Mel_Alloc* alloc);

bool mel_net_address_equals(const Mel_Net_Address* a, const Mel_Net_Address* b);
bool mel_net_address_is_loopback(const Mel_Net_Address* a);
bool mel_net_address_is_any(const Mel_Net_Address* a);
bool mel_net_address_is_v4_mapped(const Mel_Net_Address* a);

Mel_Net_Address mel_net_address_v4(u8 a, u8 b, u8 c, u8 d, u16 port);
Mel_Net_Address mel_net_address_v4_any(u16 port);
Mel_Net_Address mel_net_address_v4_loopback(u16 port);
Mel_Net_Address mel_net_address_v6_any(u16 port);
Mel_Net_Address mel_net_address_v6_loopback(u16 port);

#ifdef __cplusplus
}
#endif
