#pragma once

#include <core/platform.h>
#include <core/types.h>
#include <audioout/audioout.h>
#include <string/str8.h>
#include <collection/array.h>
#include <allocator/allocator.fwd.h>

typedef struct
{
    i32                id;
    i32                type;
    str8               name;
    str8               address;
    Mel_AudioOut_Rates channel_counts;
    Mel_AudioOut_Rates sample_rates;
} Mel_AudioOut_Android_Device;

typedef Mel_Array(Mel_AudioOut_Android_Device) Mel_AudioOut_Android_Devices;

bool mel_audioout_android__jni_enumerate(const Mel_Alloc* alloc, Mel_AudioOut_Android_Devices* out);
void mel_audioout_android__jni_devices_free(Mel_AudioOut_Android_Devices* devices, const Mel_Alloc* alloc);
bool mel_audioout_android__hotplug_start(void);
void mel_audioout_android__hotplug_stop(void);
void mel_audioout_android__on_devices_changed(void);
