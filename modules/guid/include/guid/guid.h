#pragma once

#include <core/types.h>
#include <string/str8.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u8 bytes[16];
} Mel_Guid;

#define MEL_GUID_ZERO ((Mel_Guid){ 0 })

static inline bool mel_guid_equal(Mel_Guid a, Mel_Guid b)
{
    for (u32 i = 0; i < 16; i++)
        if (a.bytes[i] != b.bytes[i])
            return false;
    return true;
}

static inline bool mel_guid_is_zero(Mel_Guid g) { return mel_guid_equal(g, MEL_GUID_ZERO); }

u64 mel_guid_hash(Mel_Guid g);

Mel_Guid mel_guid_from_bytes(const u8 bytes[16]);

Mel_Guid mel_guid_from_hidapi(u16 bus, u16 vendor, u16 product, u16 version, const char* name, u8 driver_signature, u8 driver_data);

Mel_Guid mel_guid_from_vidpid(u16 vendor, u16 product, u16 version);

size mel_guid_to_string(Mel_Guid g, char* out, size cap);

bool mel_guid_from_string(str8 s, Mel_Guid* out);

bool mel_guid_vidpid(Mel_Guid g, u16* out_vendor, u16* out_product, u16* out_version);

#ifdef __cplusplus
}
#endif
