#pragma once

#include <hid/hid.h>

#ifdef __cplusplus
extern "C"
{
#endif

// What a backend reports per device on enumeration. The descriptor is the by-value snapshot the
// registry diffs; stable_id keys identity across refreshes (handle survives, generation rolls on
// departure). open_token is the provider's private cookie for an opened channel (an fd, a pointer,
// an index), surfaced back to every I/O entry so the provider need not re-resolve the device.
typedef struct
{
    u64                stable_id;
    Mel_Hid_Descriptor desc;
} Mel_Hid_Raw;

typedef struct
{
    void*       value;
    i32         fd;
    Mel_Hid_Bus bus;
} Mel_Hid_Channel;

#define MEL_HID_NO_FD ((i32) - 1)

// The provider vtable. A platform backend fills it and registers once at init; a foreign source
// (virtual device, network HID bridge) registers the same shape. user is the backend's context.
typedef struct
{
    const char* name;
    void*       user;

    u32 (*enumerate)(void* user, Mel_Hid_Raw* out, u32 cap);

    Mel_Hid_Status (*open)(void* user, u64 stable_id, Mel_Hid_Channel* out_channel);
    void (*close)(void* user, u64 stable_id, Mel_Hid_Channel channel);

    Mel_Hid_Io_Result (*write)(void* user, Mel_Hid_Channel channel, const u8* data, usize len);
    Mel_Hid_Io_Result (*read)(void* user, Mel_Hid_Channel channel, u8* out, usize cap, i32 timeout_ms);
    Mel_Hid_Io_Result (*get_feature)(void* user, Mel_Hid_Channel channel, u8 report_id, u8* out, usize cap);
    Mel_Hid_Io_Result (*send_feature)(void* user, Mel_Hid_Channel channel, const u8* data, usize len);
    Mel_Hid_Io_Result (*get_report_descriptor)(void* user, Mel_Hid_Channel channel, u8* out, usize cap);
    Mel_Hid_Io_Result (*get_string)(void* user, Mel_Hid_Channel channel, u8 string_index, u8* out, usize cap);

    void* (*native)(void* user, Mel_Hid_Channel channel);
} Mel_Hid_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Hid_Provider;

Mel_Hid_Provider mel_hid_provider_register(const Mel_Hid_Provider_Desc* desc);
void             mel_hid_provider_unregister(Mel_Hid_Provider p);

// Each platform's backend implements this to register its native provider(s) at init. The portable
// core calls it once; a platform with no HID transport leaves it a no-op (honest absence).
void mel_hid__register_host_providers(const Mel_Alloc* alloc);

#ifdef __cplusplus
}
#endif
