#pragma once

#include <core/platform.h>
#include <core/types.h>
#include <audioin/audioin.h>
#include <string/str8.h>
#include <collection/array.h>
#include <allocator/allocator.fwd.h>

typedef struct
{
    i32               id;
    i32               type;
    str8              name;
    str8              address;
    Mel_AudioIn_Rates channel_counts;
    Mel_AudioIn_Rates sample_rates;
} Mel_AudioIn_Android_Device;

typedef Mel_Array(Mel_AudioIn_Android_Device) Mel_AudioIn_Android_Devices;

bool mel_audioin_android__permission_granted(void);
bool mel_audioin_android__request_permission(void);
bool mel_audioin_android__jni_enumerate(const Mel_Alloc* alloc, Mel_AudioIn_Android_Devices* out);
void mel_audioin_android__jni_devices_free(Mel_AudioIn_Android_Devices* devices, const Mel_Alloc* alloc);
bool mel_audioin_android__hotplug_start(void);
void mel_audioin_android__hotplug_stop(void);
void mel_audioin_android__on_devices_changed(void);
