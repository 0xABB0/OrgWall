#include "audioin_macos_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#define MEL_AUDIOIN_CA_LOOPBACK_ID S8("coreaudio:system-loopback")

typedef struct
{
    str8                               stable_id;
    AudioDeviceID                      device_id;
    AudioUnit                          unit;
    u32                                channels;
    u32                                samplerate;
    u32                                capacity_frames;
    f32*                               render_buf;
    AudioBufferList                    abl;
    _Atomic(Mel_AudioIn__Macos_Sinks*) sinks;
    _Atomic(Mel_AudioIn__Macos_Sinks*) garbage;
    _Atomic(bool)                      lost;
    _Atomic(u32)                       overruns;
    bool                               alive_listener;
} Open_Device;

typedef struct
{
    const Mel_Alloc*     alloc;
    Mel_AudioIn_Provider provider;
    bool                 registered;
    bool                 devices_listener;
    bool                 default_listener;
    Mel_Array(str8) strings;
    Mel_Array(u32) rates;
    str8 default_id;
    Mel_Array(Open_Device*) opens;
} Macos_State;

static Macos_State g_ca;

static const AudioObjectPropertyAddress g_ca_devices_addr = {
    kAudioHardwarePropertyDevices,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain,
};

static const AudioObjectPropertyAddress g_ca_default_input_addr = {
    kAudioHardwarePropertyDefaultInputDevice,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain,
};

static const AudioObjectPropertyAddress g_ca_alive_addr = {
    kAudioDevicePropertyDeviceIsAlive,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain,
};

static const AudioObjectPropertyAddress g_ca_volume_addr = {
    kAudioDevicePropertyVolumeScalar,
    kAudioObjectPropertyScopeInput,
    kAudioObjectPropertyElementMain,
};

Mel_AudioIn__Macos_Sinks* mel_audioin__macos_sinks_with(const Mel_Alloc* alloc, const Mel_AudioIn__Macos_Sinks* cur, Mel_AudioIn_Sink sink)
{
    u32                       count = cur ? cur->count : 0;
    Mel_AudioIn__Macos_Sinks* nl = mel_alloc(alloc, sizeof *nl + sizeof(Mel_AudioIn_Sink) * ((usize)count + 1u));
    if (!nl)
        return NULL;
    nl->next = NULL;
    for (u32 i = 0; i < count; i++)
        nl->sinks[i] = cur->sinks[i];
    nl->sinks[count] = sink;
    nl->count = count + 1u;
    return nl;
}

Mel_AudioIn__Macos_Sinks* mel_audioin__macos_sinks_without(const Mel_Alloc* alloc, const Mel_AudioIn__Macos_Sinks* cur, void* token)
{
    u32                       count = cur ? cur->count : 0;
    Mel_AudioIn__Macos_Sinks* nl = mel_alloc(alloc, sizeof *nl + sizeof(Mel_AudioIn_Sink) * (usize)(count ? count : 1u));
    if (!nl)
        return NULL;
    nl->next = NULL;
    u32 kept = 0;
    for (u32 i = 0; i < count; i++)
        if (cur->sinks[i].token != token)
            nl->sinks[kept++] = cur->sinks[i];
    nl->count = kept;
    return nl;
}

void mel_audioin__macos_garbage_push(_Atomic(Mel_AudioIn__Macos_Sinks*)* head, Mel_AudioIn__Macos_Sinks* sl)
{
    Mel_AudioIn__Macos_Sinks* cur = atomic_load_explicit(head, memory_order_relaxed);
    do
        sl->next = cur;
    while (!atomic_compare_exchange_weak_explicit(head, &cur, sl, memory_order_release, memory_order_relaxed));
}

void mel_audioin__macos_garbage_drain(const Mel_Alloc* alloc, _Atomic(Mel_AudioIn__Macos_Sinks*)* head)
{
    Mel_AudioIn__Macos_Sinks* sl = atomic_exchange_explicit(head, NULL, memory_order_acq_rel);
    while (sl)
    {
        Mel_AudioIn__Macos_Sinks* next = sl->next;
        mel_dealloc(alloc, sl);
        sl = next;
    }
}

static str8 ca_cf_to_str8(CFStringRef s, const char* prefix)
{
    usize   plen = prefix ? strlen(prefix) : 0;
    CFIndex max = CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
    char*   buf = mel_alloc(g_ca.alloc, plen + (usize)max);
    if (!buf)
        return STR8_EMPTY;
    if (plen)
        memcpy(buf, prefix, plen);
    if (!CFStringGetCString(s, buf + plen, max, kCFStringEncodingUTF8))
    {
        mel_dealloc(g_ca.alloc, buf);
        return STR8_EMPTY;
    }
    return str8_from_cstr(buf);
}

static void ca_strings_clear(void)
{
    for (usize i = 0; i < g_ca.strings.count; i++)
        if (g_ca.strings.items[i].data)
            mel_dealloc(g_ca.alloc, g_ca.strings.items[i].data);
    mel_array_clear(&g_ca.strings);
}

static str8 ca_intern_cf(CFStringRef s, const char* prefix)
{
    str8 owned = ca_cf_to_str8(s, prefix);
    if (owned.len > 0)
        mel_array_push(&g_ca.strings, owned);
    return owned;
}

static u32 ca_device_list(AudioDeviceID** out)
{
    *out = NULL;
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &g_ca_devices_addr, 0, NULL, &size) != noErr || size == 0)
        return 0;
    AudioDeviceID* ids = mel_alloc(g_ca.alloc, size);
    if (!ids)
        return 0;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &g_ca_devices_addr, 0, NULL, &size, ids) != noErr)
    {
        mel_dealloc(g_ca.alloc, ids);
        return 0;
    }
    *out = ids;
    return size / (u32)sizeof(AudioDeviceID);
}

static u32 ca_input_channels(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyStreamConfiguration,
        kAudioObjectPropertyScopeInput,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(id, &addr, 0, NULL, &size) != noErr || size == 0)
        return 0;
    AudioBufferList* abl = mel_alloc(g_ca.alloc, size);
    if (!abl)
        return 0;
    u32 channels = 0;
    if (AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, abl) == noErr)
        for (UInt32 b = 0; b < abl->mNumberBuffers; b++)
            channels += abl->mBuffers[b].mNumberChannels;
    mel_dealloc(g_ca.alloc, abl);
    return channels;
}

static CFStringRef ca_copy_uid(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    CFStringRef uid = NULL;
    UInt32      size = sizeof uid;
    if (AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, &uid) != noErr)
        return NULL;
    return uid;
}

static CFStringRef ca_copy_name(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr = {
        kAudioObjectPropertyName,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    CFStringRef name = NULL;
    UInt32      size = sizeof name;
    if (AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, &name) != noErr)
        return NULL;
    return name;
}

static f64 ca_nominal_rate(AudioObjectID id)
{
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyNominalSampleRate,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    Float64 rate = 0.0;
    UInt32  size = sizeof rate;
    if (AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, &rate) != noErr)
        return 0.0;
    return rate;
}

static void ca_rates_push_unique(u32 rate)
{
    if (rate == 0)
        return;
    for (usize i = 0; i < g_ca.rates.count; i++)
        if (g_ca.rates.items[i] == rate)
            return;
    mel_array_push(&g_ca.rates, rate);
}

static void ca_collect_rates(AudioDeviceID id)
{
    mel_array_clear(&g_ca.rates);
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyAvailableNominalSampleRates,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(id, &addr, 0, NULL, &size) != noErr || size == 0)
        return;
    AudioValueRange* ranges = mel_alloc(g_ca.alloc, size);
    if (!ranges)
        return;
    if (AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, ranges) == noErr)
    {
        u32 n = size / (u32)sizeof(AudioValueRange);
        for (u32 i = 0; i < n; i++)
        {
            ca_rates_push_unique((u32)(ranges[i].mMinimum + 0.5));
            ca_rates_push_unique((u32)(ranges[i].mMaximum + 0.5));
        }
    }
    mel_dealloc(g_ca.alloc, ranges);
}

static const mel_audioin_kind* ca_kind(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyTransportType,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 transport = 0;
    UInt32 size = sizeof transport;
    if (AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, &transport) != noErr)
        return &mel_audioin_unknown;
    switch (transport)
    {
    case kAudioDeviceTransportTypeBuiltIn:
        return &mel_audioin_builtin;
    case kAudioDeviceTransportTypeUSB:
        return &mel_audioin_usb;
    case kAudioDeviceTransportTypeBluetooth:
    case kAudioDeviceTransportTypeBluetoothLE:
        return &mel_audioin_bluetooth;
    case kAudioDeviceTransportTypeVirtual:
    case kAudioDeviceTransportTypeAggregate:
        return &mel_audioin_virtual;
    default:
        return &mel_audioin_unknown;
    }
}

static bool ca_gain_settable(AudioDeviceID id)
{
    if (!AudioObjectHasProperty(id, &g_ca_volume_addr))
        return false;
    Boolean settable = false;
    return AudioObjectIsPropertySettable(id, &g_ca_volume_addr, &settable) == noErr && settable;
}

static AudioDeviceID ca_default_input(void)
{
    AudioDeviceID id = kAudioObjectUnknown;
    UInt32        size = sizeof id;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &g_ca_default_input_addr, 0, NULL, &size, &id) != noErr)
        return kAudioObjectUnknown;
    return id;
}

static AudioDeviceID ca_device_for(str8 stable_id)
{
    str8 prefix = S8("coreaudio:");
    if (!str8_starts_with(stable_id, prefix))
        return kAudioObjectUnknown;
    str8        uid = str8_suffix(stable_id, stable_id.len - prefix.len);
    CFStringRef want = CFStringCreateWithBytes(kCFAllocatorDefault, uid.data, (CFIndex)uid.len, kCFStringEncodingUTF8, false);
    if (!want)
        return kAudioObjectUnknown;

    AudioDeviceID* ids = NULL;
    u32            n = ca_device_list(&ids);
    AudioDeviceID  found = kAudioObjectUnknown;
    for (u32 i = 0; i < n && found == kAudioObjectUnknown; i++)
    {
        CFStringRef u = ca_copy_uid(ids[i]);
        if (!u)
            continue;
        if (CFEqual(u, want))
            found = ids[i];
        CFRelease(u);
    }
    if (ids)
        mel_dealloc(g_ca.alloc, ids);
    CFRelease(want);
    return found;
}

static void ca_enumerate(void* user, Mel_AudioIn_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    ca_strings_clear();

    AudioDeviceID* ids = NULL;
    u32            n = ca_device_list(&ids);
    bool           stopped = false;
    for (u32 i = 0; i < n && !stopped; i++)
    {
        u32 channels = ca_input_channels(ids[i]);
        if (channels == 0)
            continue;

        CFStringRef uid = ca_copy_uid(ids[i]);
        if (!uid)
        {
            mel_log_warn("audioin", "coreaudio: input device %u has no UID; skipped", (u32)ids[i]);
            continue;
        }
        str8 stable_id = ca_intern_cf(uid, "coreaudio:");
        CFRelease(uid);
        if (stable_id.len == 0)
            continue;

        str8        name = STR8_EMPTY;
        CFStringRef cf_name = ca_copy_name(ids[i]);
        if (cf_name)
        {
            name = ca_intern_cf(cf_name, NULL);
            CFRelease(cf_name);
        }
        if (name.len == 0)
        {
            mel_log_warn("audioin", "coreaudio: device %.*s has no name; using its UID", (int)stable_id.len, stable_id.data);
            name = stable_id;
        }

        ca_collect_rates(ids[i]);

        Mel_AudioIn_Raw raw = {
            .stable_id = stable_id,
            .name = name,
            .kind = ca_kind(ids[i]),
            .channels = channels,
            .samplerate = (u32)(ca_nominal_rate(ids[i]) + 0.5),
            .samplerates = g_ca.rates.items,
            .samplerate_count = (u32)g_ca.rates.count,
            .caps = { .gain = ca_gain_settable(ids[i]) },
        };
        if (!fn(&raw, fn_user))
            stopped = true;
    }
    if (ids)
        mel_dealloc(g_ca.alloc, ids);

    if (!stopped && mel_audioin__macos_loopback_available())
    {
        u32                        rate = 0;
        AudioObjectPropertyAddress out_addr = {
            kAudioHardwarePropertyDefaultOutputDevice,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain,
        };
        AudioDeviceID out_id = kAudioObjectUnknown;
        UInt32        out_size = sizeof out_id;
        if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &out_addr, 0, NULL, &out_size, &out_id) == noErr && out_id != kAudioObjectUnknown)
            rate = (u32)(ca_nominal_rate(out_id) + 0.5);
        else
            mel_log_warn("audioin", "coreaudio: no default output device; system loopback rate reported as 0");

        Mel_AudioIn_Raw raw = {
            .stable_id = MEL_AUDIOIN_CA_LOOPBACK_ID,
            .name = S8("System Audio"),
            .kind = &mel_audioin_loopback,
            .channels = 2,
            .samplerate = rate,
            .samplerates = &rate,
            .samplerate_count = 1,
            .caps = { .gain = false },
        };
        fn(&raw, fn_user);
    }
}

static str8 ca_default_id(void* user)
{
    MEL_UNUSED(user);
    AudioDeviceID id = ca_default_input();
    if (id == kAudioObjectUnknown)
        return STR8_EMPTY;
    CFStringRef uid = ca_copy_uid(id);
    if (!uid)
        return STR8_EMPTY;
    if (g_ca.default_id.data)
    {
        mel_dealloc(g_ca.alloc, g_ca.default_id.data);
        g_ca.default_id = STR8_EMPTY;
    }
    g_ca.default_id = ca_cf_to_str8(uid, "coreaudio:");
    CFRelease(uid);
    return g_ca.default_id;
}

static Open_Device* ca_open_find(str8 stable_id)
{
    for (usize i = 0; i < g_ca.opens.count; i++)
        if (str8_equals(g_ca.opens.items[i]->stable_id, stable_id))
            return g_ca.opens.items[i];
    return NULL;
}

static Mel_AudioIn_Status od_sink_add(Open_Device* od, Mel_AudioIn_Sink sink)
{
    Mel_AudioIn__Macos_Sinks* cur = atomic_load_explicit(&od->sinks, memory_order_acquire);
    Mel_AudioIn__Macos_Sinks* nl = mel_audioin__macos_sinks_with(g_ca.alloc, cur, sink);
    if (!nl)
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    Mel_AudioIn__Macos_Sinks* old = atomic_exchange_explicit(&od->sinks, nl, memory_order_acq_rel);
    if (old)
        mel_audioin__macos_garbage_push(&od->garbage, old);
    return MEL_AUDIOIN_OK;
}

static OSStatus ca_alive_listener(AudioObjectID obj, UInt32 n, const AudioObjectPropertyAddress* addrs, void* user)
{
    MEL_UNUSED(n);
    MEL_UNUSED(addrs);
    Open_Device* od = user;

    UInt32 alive = 1;
    UInt32 size = sizeof alive;
    if (AudioObjectGetPropertyData(obj, &g_ca_alive_addr, 0, NULL, &size, &alive) == noErr && alive != 0)
        return noErr;

    if (atomic_exchange_explicit(&od->lost, true, memory_order_acq_rel))
        return noErr;

    mel_log_warn("audioin", "coreaudio: device %.*s lost", (int)od->stable_id.len, od->stable_id.data);
    Mel_AudioIn__Macos_Sinks* old = atomic_exchange_explicit(&od->sinks, NULL, memory_order_acq_rel);
    if (old)
    {
        for (u32 i = 0; i < old->count; i++)
            if (old->sinks[i].on_lost)
                old->sinks[i].on_lost(old->sinks[i].token);
        mel_audioin__macos_garbage_push(&od->garbage, old);
    }
    if (g_ca.registered)
        mel_audioin_provider_notify(g_ca.provider);
    return noErr;
}

static OSStatus ca_input_proc(void* user, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32 bus, UInt32 frames, AudioBufferList* io)
{
    MEL_UNUSED(io);
    Open_Device* od = user;
    if (frames == 0)
        return noErr;
    if (frames > od->capacity_frames)
    {
        atomic_fetch_add_explicit(&od->overruns, 1u, memory_order_relaxed);
        return noErr;
    }

    od->abl.mNumberBuffers = 1;
    od->abl.mBuffers[0].mNumberChannels = od->channels;
    od->abl.mBuffers[0].mDataByteSize = frames * od->channels * (u32)sizeof(f32);
    od->abl.mBuffers[0].mData = od->render_buf;

    OSStatus st = AudioUnitRender(od->unit, flags, ts, bus, frames, &od->abl);
    if (st != noErr)
        return st;

    Mel_AudioIn__Macos_Sinks* sl = atomic_load_explicit(&od->sinks, memory_order_acquire);
    if (sl)
        for (u32 i = 0; i < sl->count; i++)
            if (sl->sinks[i].on_frames)
                sl->sinks[i].on_frames(sl->sinks[i].token, od->render_buf, frames, od->samplerate, od->channels);
    return noErr;
}

static void od_teardown(Open_Device* od)
{
    if (od->unit)
    {
        AudioOutputUnitStop(od->unit);
        AudioUnitUninitialize(od->unit);
        AudioComponentInstanceDispose(od->unit);
    }
    if (od->alive_listener)
        AudioObjectRemovePropertyListener(od->device_id, &g_ca_alive_addr, ca_alive_listener, od);

    u32 overruns = atomic_load_explicit(&od->overruns, memory_order_relaxed);
    if (overruns > 0)
        mel_log_warn("audioin", "coreaudio: %u oversized IO slices dropped on %.*s", overruns, (int)od->stable_id.len, od->stable_id.data);

    Mel_AudioIn__Macos_Sinks* sl = atomic_exchange_explicit(&od->sinks, NULL, memory_order_acq_rel);
    if (sl)
        mel_dealloc(g_ca.alloc, sl);
    mel_audioin__macos_garbage_drain(g_ca.alloc, &od->garbage);
    if (od->render_buf)
        mel_dealloc(g_ca.alloc, od->render_buf);
    if (od->stable_id.data)
        mel_dealloc(g_ca.alloc, od->stable_id.data);
    mel_dealloc(g_ca.alloc, od);
}

static void ca_opens_remove(Open_Device* od)
{
    for (usize i = 0; i < g_ca.opens.count; i++)
        if (g_ca.opens.items[i] == od)
        {
            mel_array_remove_unordered(&g_ca.opens, i);
            return;
        }
}

static Mel_AudioIn_Status od_create(str8 stable_id, AudioDeviceID id, Mel_AudioIn_Sink sink)
{
    u32 channels = ca_input_channels(id);
    f64 rate = ca_nominal_rate(id);
    if (channels == 0 || rate <= 0.0)
    {
        mel_log_error("audioin", "coreaudio: open %.*s: invalid input format (%u ch @ %g Hz)", (int)stable_id.len, stable_id.data, channels, rate);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_NO_DEVICE;
    }

    AudioComponentDescription comp_desc = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_HALOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent comp = AudioComponentFindNext(NULL, &comp_desc);
    if (!comp)
    {
        mel_log_error("audioin", "coreaudio: no HALOutput AudioComponent");
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }

    AudioUnit unit = NULL;
    OSStatus  st = AudioComponentInstanceNew(comp, &unit);
    if (st != noErr || !unit)
    {
        mel_log_error("audioin", "coreaudio: AudioComponentInstanceNew failed (OSStatus %d)", (i32)st);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }

    Open_Device* od = mel_alloc_type(g_ca.alloc, Open_Device);
    if (!od)
    {
        AudioComponentInstanceDispose(unit);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    memset(od, 0, sizeof *od);
    od->device_id = id;
    od->unit = unit;
    od->channels = channels;
    od->samplerate = (u32)(rate + 0.5);
    od->stable_id = str8_dup(stable_id, g_ca.alloc);
    atomic_store_explicit(&od->sinks, NULL, memory_order_relaxed);
    atomic_store_explicit(&od->garbage, NULL, memory_order_relaxed);

    UInt32 enable = 1;
    UInt32 disable = 0;
    st = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enable, sizeof enable);
    if (st == noErr)
        st = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &disable, sizeof disable);
    if (st == noErr)
        st = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &id, sizeof id);
    if (st != noErr)
    {
        mel_log_error("audioin", "coreaudio: AUHAL input configuration failed for %.*s (OSStatus %d)", (int)stable_id.len, stable_id.data, (i32)st);
        od_teardown(od);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }

    AudioStreamBasicDescription fmt = {
        .mSampleRate = rate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = (UInt32)(sizeof(f32) * channels),
        .mFramesPerPacket = 1,
        .mBytesPerFrame = (UInt32)(sizeof(f32) * channels),
        .mChannelsPerFrame = channels,
        .mBitsPerChannel = 32,
    };
    st = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &fmt, sizeof fmt);
    if (st != noErr)
    {
        mel_log_error("audioin", "coreaudio: client format rejected for %.*s (OSStatus %d, %u ch @ %u Hz f32 interleaved)", (int)stable_id.len, stable_id.data, (i32)st, channels, od->samplerate);
        od_teardown(od);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }

    AURenderCallbackStruct cb = { .inputProc = ca_input_proc, .inputProcRefCon = od };
    st = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &cb, sizeof cb);
    if (st == noErr)
        st = AudioUnitInitialize(unit);
    if (st != noErr)
    {
        mel_log_error("audioin", "coreaudio: AUHAL initialize failed for %.*s (OSStatus %d)", (int)stable_id.len, stable_id.data, (i32)st);
        od_teardown(od);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }

    UInt32 max_frames = 0;
    UInt32 max_size = sizeof max_frames;
    if (AudioUnitGetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max_frames, &max_size) != noErr || max_frames == 0)
        max_frames = 4096;
    od->capacity_frames = max_frames < 4096 ? 4096 : max_frames;
    od->render_buf = mel_alloc(g_ca.alloc, sizeof(f32) * (usize)od->capacity_frames * channels);
    if (!od->render_buf)
    {
        od_teardown(od);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }

    st = AudioObjectAddPropertyListener(id, &g_ca_alive_addr, ca_alive_listener, od);
    if (st != noErr)
        mel_log_warn("audioin", "coreaudio: device-alive listener failed for %.*s (OSStatus %d); loss will surface on refresh only", (int)stable_id.len, stable_id.data, (i32)st);
    else
        od->alive_listener = true;

    Mel_AudioIn_Status add = od_sink_add(od, sink);
    if (mel_audioin_status_failed(add))
    {
        od_teardown(od);
        return add;
    }

    st = AudioOutputUnitStart(unit);
    if (st != noErr)
    {
        mel_log_error("audioin", "coreaudio: AudioOutputUnitStart failed for %.*s (OSStatus %d)", (int)stable_id.len, stable_id.data, (i32)st);
        od_teardown(od);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }

    mel_array_push(&g_ca.opens, od);
    mel_log_info("audioin", "coreaudio: capturing %.*s (%u ch @ %u Hz)", (int)stable_id.len, stable_id.data, channels, od->samplerate);
    return MEL_AUDIOIN_OK;
}

static Mel_AudioIn_Status ca_open(void* user, str8 stable_id, Mel_AudioIn_Sink sink)
{
    MEL_UNUSED(user);
    if (str8_equals(stable_id, MEL_AUDIOIN_CA_LOOPBACK_ID))
        return mel_audioin__macos_loopback_open(g_ca.alloc, sink);

    Open_Device* od = ca_open_find(stable_id);
    if (od)
    {
        if (atomic_load_explicit(&od->lost, memory_order_acquire))
        {
            mel_log_error("audioin", "coreaudio: open %.*s: device lost", (int)stable_id.len, stable_id.data);
            return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_LOST;
        }
        return od_sink_add(od, sink);
    }

    AudioDeviceID id = ca_device_for(stable_id);
    if (id == kAudioObjectUnknown)
    {
        mel_log_error("audioin", "coreaudio: open %.*s: device not present", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_NO_DEVICE;
    }
    return od_create(stable_id, id, sink);
}

static void ca_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    if (str8_equals(stable_id, MEL_AUDIOIN_CA_LOOPBACK_ID))
    {
        mel_audioin__macos_loopback_close(token);
        return;
    }

    Open_Device* od = ca_open_find(stable_id);
    if (!od)
    {
        mel_log_warn("audioin", "coreaudio: close %.*s: not open", (int)stable_id.len, stable_id.data);
        return;
    }

    Mel_AudioIn__Macos_Sinks* cur = atomic_load_explicit(&od->sinks, memory_order_acquire);
    u32                       remaining = 0;
    if (cur && cur->count > 0)
    {
        Mel_AudioIn__Macos_Sinks* nl = mel_audioin__macos_sinks_without(g_ca.alloc, cur, token);
        if (!nl)
        {
            mel_log_error("audioin", "coreaudio: close %.*s: sink list allocation failed", (int)stable_id.len, stable_id.data);
            return;
        }
        remaining = nl->count;
        Mel_AudioIn__Macos_Sinks* old;
        if (remaining == 0)
        {
            old = atomic_exchange_explicit(&od->sinks, NULL, memory_order_acq_rel);
            mel_dealloc(g_ca.alloc, nl);
        }
        else
            old = atomic_exchange_explicit(&od->sinks, nl, memory_order_acq_rel);
        if (old)
            mel_audioin__macos_garbage_push(&od->garbage, old);
    }

    if (remaining == 0)
    {
        ca_opens_remove(od);
        od_teardown(od);
    }
}

static f32 ca_gain(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    AudioDeviceID id = ca_device_for(stable_id);
    if (id == kAudioObjectUnknown)
    {
        mel_log_error("audioin", "coreaudio: gain %.*s: device not present", (int)stable_id.len, stable_id.data);
        return 0.0f;
    }
    Float32  v = 0.0f;
    UInt32   size = sizeof v;
    OSStatus st = AudioObjectGetPropertyData(id, &g_ca_volume_addr, 0, NULL, &size, &v);
    if (st != noErr)
    {
        mel_log_error("audioin", "coreaudio: gain %.*s: read failed (OSStatus %d)", (int)stable_id.len, stable_id.data, (i32)st);
        return 0.0f;
    }
    return (f32)v;
}

static Mel_AudioIn_Status ca_set_gain(void* user, str8 stable_id, f32 gain)
{
    MEL_UNUSED(user);
    AudioDeviceID id = ca_device_for(stable_id);
    if (id == kAudioObjectUnknown)
    {
        mel_log_error("audioin", "coreaudio: set_gain %.*s: device not present", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    if (!ca_gain_settable(id))
    {
        mel_log_error("audioin", "coreaudio: set_gain %.*s: gain not settable", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    Float32  v = gain;
    OSStatus st = AudioObjectSetPropertyData(id, &g_ca_volume_addr, 0, NULL, sizeof v, &v);
    if (st != noErr)
    {
        mel_log_error("audioin", "coreaudio: set_gain %.*s: write failed (OSStatus %d)", (int)stable_id.len, stable_id.data, (i32)st);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    return MEL_AUDIOIN_OK;
}

static const mel_audioin_auth* ca_authorization(void* user)
{
    MEL_UNUSED(user);
    return mel_audioin__macos_authorization();
}

static void ca_authorize(void* user, Mel_AudioIn_Sink sink)
{
    MEL_UNUSED(user);
    mel_audioin__macos_authorize(sink);
}

static void* ca_native(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    if (str8_equals(stable_id, MEL_AUDIOIN_CA_LOOPBACK_ID))
        return mel_audioin__macos_loopback_native();
    AudioDeviceID id = ca_device_for(stable_id);
    return id == kAudioObjectUnknown ? NULL : (void*)(usize)id;
}

static OSStatus ca_system_listener(AudioObjectID obj, UInt32 n, const AudioObjectPropertyAddress* addrs, void* user)
{
    MEL_UNUSED(obj);
    MEL_UNUSED(n);
    MEL_UNUSED(addrs);
    MEL_UNUSED(user);
    if (g_ca.registered)
        mel_audioin_provider_notify(g_ca.provider);
    return noErr;
}

static void ca_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (g_ca.devices_listener)
        AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &g_ca_devices_addr, ca_system_listener, NULL);
    if (g_ca.default_listener)
        AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &g_ca_default_input_addr, ca_system_listener, NULL);

    for (usize i = 0; i < g_ca.opens.count; i++)
        od_teardown(g_ca.opens.items[i]);
    mel_array_free(&g_ca.opens);

    mel_audioin__macos_loopback_shutdown();

    ca_strings_clear();
    mel_array_free(&g_ca.strings);
    mel_array_free(&g_ca.rates);
    if (g_ca.default_id.data)
        mel_dealloc(g_ca.alloc, g_ca.default_id.data);
    memset(&g_ca, 0, sizeof g_ca);
}

void mel_audioin__register_host_providers(void)
{
    static const Mel_AudioIn_Provider_Desc desc = {
        .name = "coreaudio",
        .enumerate = ca_enumerate,
        .default_id = ca_default_id,
        .open = ca_open,
        .close = ca_close,
        .gain = ca_gain,
        .set_gain = ca_set_gain,
        .authorization = ca_authorization,
        .authorize = ca_authorize,
        .native = ca_native,
        .shutdown = ca_shutdown,
    };

    g_ca.alloc = mel_alloc_heap();
    mel_array_init(&g_ca.strings, g_ca.alloc);
    mel_array_init(&g_ca.rates, g_ca.alloc);
    mel_array_init(&g_ca.opens, g_ca.alloc);
    g_ca.default_id = STR8_EMPTY;
    g_ca.provider = mel_audioin_provider_register(&desc);
    g_ca.registered = true;

    OSStatus st = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &g_ca_devices_addr, ca_system_listener, NULL);
    if (st != noErr)
        mel_log_warn("audioin", "coreaudio: device-set listener failed (OSStatus %d); hotplug needs manual refresh", (i32)st);
    else
        g_ca.devices_listener = true;

    st = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &g_ca_default_input_addr, ca_system_listener, NULL);
    if (st != noErr)
        mel_log_warn("audioin", "coreaudio: default-input listener failed (OSStatus %d); default changes need manual refresh", (i32)st);
    else
        g_ca.default_listener = true;
}
