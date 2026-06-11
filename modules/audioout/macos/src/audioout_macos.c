#include <audioout/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#define MEL_AUDIOOUT_CA_MIN_CAPACITY_FRAMES 4096u

typedef struct
{
    void*                token;
    Mel_AudioOut_Pull_Fn pull;
    bool                 started;
} Out_Open;

typedef struct Open_List Open_List;

struct Open_List
{
    Open_List* next;
    u32        count;
    Out_Open   opens[];
};

typedef struct
{
    str8                stable_id;
    AudioDeviceID       device_id;
    AudioUnit           unit;
    u32                 channels;
    u32                 samplerate;
    u32                 capacity_frames;
    f32*                scratch;
    _Atomic(Open_List*) opens;
    _Atomic(Open_List*) garbage;
    _Atomic(u32)        oversized;
    bool                running;
} Open_Device;

typedef struct
{
    AudioDeviceID device_id;
    UInt32        volume_element;
    bool          volume_listener;
    UInt32        mute_element;
    bool          mute_listener;
    bool          seen;
} Watch_Entry;

typedef struct
{
    const Mel_Alloc*      alloc;
    Mel_AudioOut_Provider provider;
    bool                  registered;
    bool                  devices_listener;
    bool                  default_listener;
    Mel_Array(str8) strings;
    Mel_Array(u32) rates;
    str8 default_id;
    Mel_Array(Open_Device*) opens;
    Mel_Array(Watch_Entry) watches;
} Macos_State;

static Macos_State g_ca;

static const AudioObjectPropertyAddress g_ca_devices_addr = {
    kAudioHardwarePropertyDevices,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain,
};

static const AudioObjectPropertyAddress g_ca_default_output_addr = {
    kAudioHardwarePropertyDefaultOutputDevice,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain,
};

static AudioObjectPropertyAddress ca_volume_addr(UInt32 element) { return (AudioObjectPropertyAddress){ kAudioDevicePropertyVolumeScalar, kAudioObjectPropertyScopeOutput, element }; }

static AudioObjectPropertyAddress ca_mute_addr(UInt32 element) { return (AudioObjectPropertyAddress){ kAudioDevicePropertyMute, kAudioObjectPropertyScopeOutput, element }; }

static bool ca_probe_element(AudioDeviceID id, AudioObjectPropertyAddress (*make)(UInt32), UInt32* element, bool* settable)
{
    AudioObjectPropertyAddress addr = make(kAudioObjectPropertyElementMain);
    if (!AudioObjectHasProperty(id, &addr))
        addr.mElement = 1;
    if (!AudioObjectHasProperty(id, &addr))
        return false;
    *element = addr.mElement;
    Boolean s = false;
    *settable = AudioObjectIsPropertySettable(id, &addr, &s) == noErr && s;
    return true;
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

static u32 ca_output_channels(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyStreamConfiguration,
        kAudioObjectPropertyScopeOutput,
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

static const mel_audioout_kind* ca_kind(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyTransportType,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 transport = 0;
    UInt32 size = sizeof transport;
    if (AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, &transport) != noErr)
        return &mel_audioout_unknown;
    switch (transport)
    {
    case kAudioDeviceTransportTypeBuiltIn:
        return &mel_audioout_builtin;
    case kAudioDeviceTransportTypeUSB:
        return &mel_audioout_usb;
    case kAudioDeviceTransportTypeBluetooth:
    case kAudioDeviceTransportTypeBluetoothLE:
        return &mel_audioout_bluetooth;
    case kAudioDeviceTransportTypeDisplayPort:
    case kAudioDeviceTransportTypeHDMI:
        return &mel_audioout_hdmi;
    case kAudioDeviceTransportTypeVirtual:
    case kAudioDeviceTransportTypeAggregate:
        return &mel_audioout_virtual;
    default:
        return &mel_audioout_unknown;
    }
}

static AudioDeviceID ca_default_output(void)
{
    AudioDeviceID id = kAudioObjectUnknown;
    UInt32        size = sizeof id;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &g_ca_default_output_addr, 0, NULL, &size, &id) != noErr)
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

static OSStatus ca_prop_listener(AudioObjectID obj, UInt32 n, const AudioObjectPropertyAddress* addrs, void* user)
{
    MEL_UNUSED(obj);
    MEL_UNUSED(n);
    MEL_UNUSED(addrs);
    MEL_UNUSED(user);
    if (g_ca.registered)
        mel_audioout_provider_notify(g_ca.provider);
    return noErr;
}

static void watch_remove_listeners(Watch_Entry* w)
{
    if (w->volume_listener)
    {
        AudioObjectPropertyAddress addr = ca_volume_addr(w->volume_element);
        AudioObjectRemovePropertyListener(w->device_id, &addr, ca_prop_listener, NULL);
    }
    if (w->mute_listener)
    {
        AudioObjectPropertyAddress addr = ca_mute_addr(w->mute_element);
        AudioObjectRemovePropertyListener(w->device_id, &addr, ca_prop_listener, NULL);
    }
}

static void watch_ensure(AudioDeviceID id, UInt32 volume_element, bool has_mute, UInt32 mute_element)
{
    for (usize i = 0; i < g_ca.watches.count; i++)
        if (g_ca.watches.items[i].device_id == id)
        {
            g_ca.watches.items[i].seen = true;
            return;
        }

    Watch_Entry w = { .device_id = id, .volume_element = volume_element, .mute_element = mute_element, .seen = true };

    AudioObjectPropertyAddress vaddr = ca_volume_addr(volume_element);
    OSStatus                   st = AudioObjectAddPropertyListener(id, &vaddr, ca_prop_listener, NULL);
    if (st != noErr)
        mel_log_warn("audioout", "coreaudio: volume listener failed for device %u (OSStatus %d); external volume changes need manual refresh", (u32)id, (i32)st);
    else
        w.volume_listener = true;
    if (volume_element != kAudioObjectPropertyElementMain)
        mel_log_debug("audioout", "coreaudio: device %u exposes volume on channel 1, not the main element", (u32)id);

    if (has_mute)
    {
        AudioObjectPropertyAddress maddr = ca_mute_addr(mute_element);
        st = AudioObjectAddPropertyListener(id, &maddr, ca_prop_listener, NULL);
        if (st != noErr)
            mel_log_warn("audioout", "coreaudio: mute listener failed for device %u (OSStatus %d); external mute changes need manual refresh", (u32)id, (i32)st);
        else
            w.mute_listener = true;
    }

    mel_array_push(&g_ca.watches, w);
}

static void watch_sweep(void)
{
    for (usize i = 0; i < g_ca.watches.count;)
    {
        if (g_ca.watches.items[i].seen)
        {
            i++;
            continue;
        }
        watch_remove_listeners(&g_ca.watches.items[i]);
        mel_array_remove_unordered(&g_ca.watches, i);
    }
}

static void ca_enumerate(void* user, Mel_AudioOut_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    ca_strings_clear();
    for (usize i = 0; i < g_ca.watches.count; i++)
        g_ca.watches.items[i].seen = false;

    AudioDeviceID* ids = NULL;
    u32            n = ca_device_list(&ids);
    bool           stopped = false;
    for (u32 i = 0; i < n && !stopped; i++)
    {
        u32 channels = ca_output_channels(ids[i]);
        if (channels == 0)
            continue;

        CFStringRef uid = ca_copy_uid(ids[i]);
        if (!uid)
        {
            mel_log_warn("audioout", "coreaudio: output device %u has no UID; skipped", (u32)ids[i]);
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
            mel_log_warn("audioout", "coreaudio: device %.*s has no name; using its UID", (int)stable_id.len, stable_id.data);
            name = stable_id;
        }

        ca_collect_rates(ids[i]);

        UInt32 volume_element = kAudioObjectPropertyElementMain;
        bool   volume_settable = false;
        bool   has_volume = ca_probe_element(ids[i], ca_volume_addr, &volume_element, &volume_settable);
        bool   cap_volume = has_volume && volume_settable;

        f32  volume = 0.0f;
        bool muted = false;
        if (cap_volume)
        {
            AudioObjectPropertyAddress vaddr = ca_volume_addr(volume_element);
            Float32                    v = 0.0f;
            UInt32                     vsize = sizeof v;
            if (AudioObjectGetPropertyData(ids[i], &vaddr, 0, NULL, &vsize, &v) == noErr)
                volume = (f32)v;

            UInt32 mute_element = kAudioObjectPropertyElementMain;
            bool   mute_settable = false;
            bool   has_mute = ca_probe_element(ids[i], ca_mute_addr, &mute_element, &mute_settable);
            if (has_mute)
            {
                AudioObjectPropertyAddress maddr = ca_mute_addr(mute_element);
                UInt32                     m = 0;
                UInt32                     msize = sizeof m;
                if (AudioObjectGetPropertyData(ids[i], &maddr, 0, NULL, &msize, &m) == noErr)
                    muted = m != 0;
            }

            watch_ensure(ids[i], volume_element, has_mute, mute_element);
        }

        Mel_AudioOut_Raw raw = {
            .stable_id = stable_id,
            .name = name,
            .kind = ca_kind(ids[i]),
            .channels = channels,
            .samplerate = (u32)(ca_nominal_rate(ids[i]) + 0.5),
            .samplerates = g_ca.rates.items,
            .samplerate_count = (u32)g_ca.rates.count,
            .caps = { .volume = cap_volume },
            .volume = volume,
            .muted = muted,
        };
        if (!fn(&raw, fn_user))
            stopped = true;
    }
    if (ids)
        mel_dealloc(g_ca.alloc, ids);

    if (!stopped)
        watch_sweep();
}

static str8 ca_default_id(void* user)
{
    MEL_UNUSED(user);
    AudioDeviceID id = ca_default_output();
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

static void od_garbage_push(Open_Device* od, Open_List* ol)
{
    Open_List* cur = atomic_load_explicit(&od->garbage, memory_order_relaxed);
    do
        ol->next = cur;
    while (!atomic_compare_exchange_weak_explicit(&od->garbage, &cur, ol, memory_order_release, memory_order_relaxed));
}

static void od_garbage_drain(Open_Device* od)
{
    Open_List* ol = atomic_exchange_explicit(&od->garbage, NULL, memory_order_acq_rel);
    while (ol)
    {
        Open_List* next = ol->next;
        mel_dealloc(g_ca.alloc, ol);
        ol = next;
    }
}

static Open_List* od_opens_clone(Open_Device* od, u32 extra)
{
    Open_List* cur = atomic_load_explicit(&od->opens, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    Open_List* nl = mel_alloc(g_ca.alloc, sizeof *nl + sizeof(Out_Open) * ((usize)count + extra));
    if (!nl)
        return NULL;
    nl->next = NULL;
    for (u32 i = 0; i < count; i++)
        nl->opens[i] = cur->opens[i];
    nl->count = count;
    return nl;
}

static u32 od_swap(Open_Device* od, Open_List* nl)
{
    Open_List* old = atomic_exchange_explicit(&od->opens, nl, memory_order_acq_rel);
    if (old)
        od_garbage_push(od, old);
    u32 started = 0;
    if (nl)
        for (u32 i = 0; i < nl->count; i++)
            if (nl->opens[i].started)
                started++;
    return started;
}

static void od_apply_running(Open_Device* od, u32 started)
{
    if (started > 0 && !od->running)
    {
        OSStatus st = AudioOutputUnitStart(od->unit);
        if (st != noErr)
        {
            mel_log_error("audioout", "coreaudio: AudioOutputUnitStart failed for %.*s (OSStatus %d)", (int)od->stable_id.len, od->stable_id.data, (i32)st);
            return;
        }
        od->running = true;
    }
    else if (started == 0 && od->running)
    {
        OSStatus st = AudioOutputUnitStop(od->unit);
        if (st != noErr)
            mel_log_warn("audioout", "coreaudio: AudioOutputUnitStop failed for %.*s (OSStatus %d)", (int)od->stable_id.len, od->stable_id.data, (i32)st);
        od->running = false;
    }
}

static OSStatus ca_render(void* user, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, UInt32 bus, UInt32 frames, AudioBufferList* io)
{
    MEL_UNUSED(ts);
    MEL_UNUSED(bus);
    Open_Device* od = user;
    if (!io || io->mNumberBuffers == 0)
        return noErr;

    for (UInt32 b = 0; b < io->mNumberBuffers; b++)
        if (io->mBuffers[b].mData)
            memset(io->mBuffers[b].mData, 0, io->mBuffers[b].mDataByteSize);

    f32* dst = io->mBuffers[0].mData;
    if (!dst || frames == 0)
        return noErr;

    u32 dst_frames = io->mBuffers[0].mDataByteSize / ((u32)sizeof(f32) * od->channels);
    u32 n = frames < dst_frames ? frames : dst_frames;
    if (n > od->capacity_frames)
    {
        atomic_fetch_add_explicit(&od->oversized, 1u, memory_order_relaxed);
        n = od->capacity_frames;
    }

    bool       any = false;
    Open_List* ol = atomic_load_explicit(&od->opens, memory_order_acquire);
    if (ol)
        for (u32 i = 0; i < ol->count; i++)
        {
            if (!ol->opens[i].started)
                continue;
            any = true;
            u32 got = ol->opens[i].pull(ol->opens[i].token, od->scratch, n);
            if (got > n)
                got = n;
            for (usize s = 0; s < (usize)got * od->channels; s++)
                dst[s] += od->scratch[s];
        }

    if (!any && flags)
        *flags |= kAudioUnitRenderAction_OutputIsSilence;
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

    u32 oversized = atomic_load_explicit(&od->oversized, memory_order_relaxed);
    if (oversized > 0)
        mel_log_warn("audioout", "coreaudio: %u oversized render slices truncated on %.*s", oversized, (int)od->stable_id.len, od->stable_id.data);

    Open_List* ol = atomic_exchange_explicit(&od->opens, NULL, memory_order_acq_rel);
    if (ol)
        mel_dealloc(g_ca.alloc, ol);
    od_garbage_drain(od);
    if (od->scratch)
        mel_dealloc(g_ca.alloc, od->scratch);
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

static Mel_AudioOut_Status od_create(str8 stable_id, AudioDeviceID id, Open_Device** out)
{
    *out = NULL;
    u32 channels = ca_output_channels(id);
    f64 rate = ca_nominal_rate(id);
    if (channels == 0 || rate <= 0.0)
    {
        mel_log_error("audioout", "coreaudio: open %.*s: invalid output format (%u ch @ %g Hz)", (int)stable_id.len, stable_id.data, channels, rate);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_NO_DEVICE;
    }

    AudioComponentDescription comp_desc = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_HALOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent comp = AudioComponentFindNext(NULL, &comp_desc);
    if (!comp)
    {
        mel_log_error("audioout", "coreaudio: no HALOutput AudioComponent");
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }

    AudioUnit unit = NULL;
    OSStatus  st = AudioComponentInstanceNew(comp, &unit);
    if (st != noErr || !unit)
    {
        mel_log_error("audioout", "coreaudio: AudioComponentInstanceNew failed (OSStatus %d)", (i32)st);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }

    Open_Device* od = mel_alloc_type(g_ca.alloc, Open_Device);
    if (!od)
    {
        AudioComponentInstanceDispose(unit);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    memset(od, 0, sizeof *od);
    od->device_id = id;
    od->unit = unit;
    od->channels = channels;
    od->samplerate = (u32)(rate + 0.5);
    od->stable_id = str8_dup(stable_id, g_ca.alloc);
    atomic_store_explicit(&od->opens, NULL, memory_order_relaxed);
    atomic_store_explicit(&od->garbage, NULL, memory_order_relaxed);

    UInt32 enable = 1;
    UInt32 disable = 0;
    st = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enable, sizeof enable);
    if (st == noErr)
        st = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &disable, sizeof disable);
    if (st == noErr)
        st = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &id, sizeof id);
    if (st != noErr)
    {
        mel_log_error("audioout", "coreaudio: AUHAL output configuration failed for %.*s (OSStatus %d)", (int)stable_id.len, stable_id.data, (i32)st);
        od_teardown(od);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
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
    st = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt);
    if (st != noErr)
    {
        mel_log_error("audioout", "coreaudio: client format rejected for %.*s (OSStatus %d, %u ch @ %u Hz f32 interleaved)", (int)stable_id.len, stable_id.data, (i32)st, channels, od->samplerate);
        od_teardown(od);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }

    AURenderCallbackStruct cb = { .inputProc = ca_render, .inputProcRefCon = od };
    st = AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
    if (st == noErr)
        st = AudioUnitInitialize(unit);
    if (st != noErr)
    {
        mel_log_error("audioout", "coreaudio: AUHAL initialize failed for %.*s (OSStatus %d)", (int)stable_id.len, stable_id.data, (i32)st);
        od_teardown(od);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }

    UInt32 max_frames = 0;
    UInt32 max_size = sizeof max_frames;
    if (AudioUnitGetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max_frames, &max_size) != noErr || max_frames == 0)
        max_frames = MEL_AUDIOOUT_CA_MIN_CAPACITY_FRAMES;
    od->capacity_frames = max_frames < MEL_AUDIOOUT_CA_MIN_CAPACITY_FRAMES ? MEL_AUDIOOUT_CA_MIN_CAPACITY_FRAMES : max_frames;
    od->scratch = mel_alloc(g_ca.alloc, sizeof(f32) * (usize)od->capacity_frames * channels);
    if (!od->scratch)
    {
        od_teardown(od);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }

    mel_array_push(&g_ca.opens, od);
    mel_log_info("audioout", "coreaudio: opened %.*s (%u ch @ %u Hz, %u frame slices)", (int)stable_id.len, stable_id.data, channels, od->samplerate, od->capacity_frames);
    *out = od;
    return MEL_AUDIOOUT_OK;
}

static Mel_AudioOut_Status ca_open(void* user, str8 stable_id, Mel_AudioOut_Format req, Mel_AudioOut_Format* granted, Mel_AudioOut_Pull_Fn pull, void* token)
{
    MEL_UNUSED(user);
    assert(granted != NULL);
    assert(pull != NULL);

    Open_Device* od = ca_open_find(stable_id);
    if (!od)
    {
        AudioDeviceID id = ca_device_for(stable_id);
        if (id == kAudioObjectUnknown)
        {
            mel_log_error("audioout", "coreaudio: open %.*s: device not present", (int)stable_id.len, stable_id.data);
            return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_NO_DEVICE;
        }
        Mel_AudioOut_Status st = od_create(stable_id, id, &od);
        if (mel_audioout_status_failed(st))
            return st;
    }

    Open_List* nl = od_opens_clone(od, 1);
    if (!nl)
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    nl->opens[nl->count] = (Out_Open){ .token = token, .pull = pull, .started = false };
    nl->count++;
    od_swap(od, nl);

    granted->samplerate = od->samplerate;
    granted->channels = od->channels;
    granted->block_frames = od->capacity_frames;
    if (req.samplerate != granted->samplerate || req.channels != granted->channels)
        mel_log_debug("audioout", "coreaudio: %.*s granted %u ch @ %u Hz (requested %u ch @ %u Hz)", (int)stable_id.len, stable_id.data, granted->channels, granted->samplerate, req.channels, req.samplerate);
    return MEL_AUDIOOUT_OK;
}

static void ca_set_started(str8 stable_id, void* token, bool started, const char* what)
{
    Open_Device* od = ca_open_find(stable_id);
    if (!od)
    {
        mel_log_warn("audioout", "coreaudio: %s %.*s: not open", what, (int)stable_id.len, stable_id.data);
        return;
    }
    Open_List* nl = od_opens_clone(od, 0);
    if (!nl)
    {
        mel_log_error("audioout", "coreaudio: %s %.*s: open list allocation failed", what, (int)stable_id.len, stable_id.data);
        return;
    }
    bool found = false;
    for (u32 i = 0; i < nl->count; i++)
        if (nl->opens[i].token == token)
        {
            nl->opens[i].started = started;
            found = true;
        }
    if (!found)
        mel_log_warn("audioout", "coreaudio: %s %.*s: unknown token", what, (int)stable_id.len, stable_id.data);
    u32 running = od_swap(od, nl);
    od_apply_running(od, running);
}

static void ca_start(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    ca_set_started(stable_id, token, true, "start");
}

static void ca_stop(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    ca_set_started(stable_id, token, false, "stop");
}

static void ca_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    Open_Device* od = ca_open_find(stable_id);
    if (!od)
    {
        mel_log_warn("audioout", "coreaudio: close %.*s: not open", (int)stable_id.len, stable_id.data);
        return;
    }

    Open_List* cur = atomic_load_explicit(&od->opens, memory_order_acquire);
    u32        count = cur ? cur->count : 0;
    Open_List* nl = mel_alloc(g_ca.alloc, sizeof *nl + sizeof(Out_Open) * (usize)(count ? count : 1u));
    if (!nl)
    {
        mel_log_error("audioout", "coreaudio: close %.*s: open list allocation failed", (int)stable_id.len, stable_id.data);
        return;
    }
    nl->next = NULL;
    u32 kept = 0;
    for (u32 i = 0; i < count; i++)
        if (cur->opens[i].token != token)
            nl->opens[kept++] = cur->opens[i];
    nl->count = kept;

    if (kept == 0)
    {
        Open_List* old = atomic_exchange_explicit(&od->opens, NULL, memory_order_acq_rel);
        if (old)
            od_garbage_push(od, old);
        mel_dealloc(g_ca.alloc, nl);
        od_apply_running(od, 0);
        ca_opens_remove(od);
        mel_log_info("audioout", "coreaudio: closed %.*s", (int)stable_id.len, stable_id.data);
        od_teardown(od);
        return;
    }

    u32 running = od_swap(od, nl);
    od_apply_running(od, running);
}

static f32 ca_volume(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    AudioDeviceID id = ca_device_for(stable_id);
    if (id == kAudioObjectUnknown)
    {
        mel_log_error("audioout", "coreaudio: volume %.*s: device not present", (int)stable_id.len, stable_id.data);
        return 0.0f;
    }
    UInt32 element = kAudioObjectPropertyElementMain;
    bool   settable = false;
    if (!ca_probe_element(id, ca_volume_addr, &element, &settable))
    {
        mel_log_error("audioout", "coreaudio: volume %.*s: no output volume control", (int)stable_id.len, stable_id.data);
        return 0.0f;
    }
    AudioObjectPropertyAddress addr = ca_volume_addr(element);
    Float32                    v = 0.0f;
    UInt32                     size = sizeof v;
    OSStatus                   st = AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, &v);
    if (st != noErr)
    {
        mel_log_error("audioout", "coreaudio: volume %.*s: read failed (OSStatus %d)", (int)stable_id.len, stable_id.data, (i32)st);
        return 0.0f;
    }
    return (f32)v;
}

static Mel_AudioOut_Status ca_set_volume(void* user, str8 stable_id, f32 volume)
{
    MEL_UNUSED(user);
    AudioDeviceID id = ca_device_for(stable_id);
    if (id == kAudioObjectUnknown)
    {
        mel_log_error("audioout", "coreaudio: set_volume %.*s: device not present", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_NO_DEVICE;
    }
    UInt32 element = kAudioObjectPropertyElementMain;
    bool   settable = false;
    if (!ca_probe_element(id, ca_volume_addr, &element, &settable) || !settable)
    {
        mel_log_error("audioout", "coreaudio: set_volume %.*s: volume not settable", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    AudioObjectPropertyAddress addr = ca_volume_addr(element);
    Float32                    v = volume;
    OSStatus                   st = AudioObjectSetPropertyData(id, &addr, 0, NULL, sizeof v, &v);
    if (st != noErr)
    {
        mel_log_error("audioout", "coreaudio: set_volume %.*s: write failed (OSStatus %d)", (int)stable_id.len, stable_id.data, (i32)st);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    return MEL_AUDIOOUT_OK;
}

static bool ca_muted(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    AudioDeviceID id = ca_device_for(stable_id);
    if (id == kAudioObjectUnknown)
    {
        mel_log_error("audioout", "coreaudio: muted %.*s: device not present", (int)stable_id.len, stable_id.data);
        return false;
    }
    UInt32 element = kAudioObjectPropertyElementMain;
    bool   settable = false;
    if (!ca_probe_element(id, ca_mute_addr, &element, &settable))
    {
        mel_log_debug("audioout", "coreaudio: muted %.*s: no mute control; reporting unmuted", (int)stable_id.len, stable_id.data);
        return false;
    }
    AudioObjectPropertyAddress addr = ca_mute_addr(element);
    UInt32                     m = 0;
    UInt32                     size = sizeof m;
    OSStatus                   st = AudioObjectGetPropertyData(id, &addr, 0, NULL, &size, &m);
    if (st != noErr)
    {
        mel_log_error("audioout", "coreaudio: muted %.*s: read failed (OSStatus %d)", (int)stable_id.len, stable_id.data, (i32)st);
        return false;
    }
    return m != 0;
}

static Mel_AudioOut_Status ca_set_muted(void* user, str8 stable_id, bool muted)
{
    MEL_UNUSED(user);
    AudioDeviceID id = ca_device_for(stable_id);
    if (id == kAudioObjectUnknown)
    {
        mel_log_error("audioout", "coreaudio: set_muted %.*s: device not present", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_NO_DEVICE;
    }
    UInt32 element = kAudioObjectPropertyElementMain;
    bool   settable = false;
    if (!ca_probe_element(id, ca_mute_addr, &element, &settable) || !settable)
    {
        mel_log_error("audioout", "coreaudio: set_muted %.*s: mute not settable", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    AudioObjectPropertyAddress addr = ca_mute_addr(element);
    UInt32                     m = muted ? 1 : 0;
    OSStatus                   st = AudioObjectSetPropertyData(id, &addr, 0, NULL, sizeof m, &m);
    if (st != noErr)
    {
        mel_log_error("audioout", "coreaudio: set_muted %.*s: write failed (OSStatus %d)", (int)stable_id.len, stable_id.data, (i32)st);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    return MEL_AUDIOOUT_OK;
}

static void* ca_native(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    AudioDeviceID id = ca_device_for(stable_id);
    return id == kAudioObjectUnknown ? NULL : (void*)(usize)id;
}

static void ca_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    if (g_ca.devices_listener)
        AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &g_ca_devices_addr, ca_prop_listener, NULL);
    if (g_ca.default_listener)
        AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &g_ca_default_output_addr, ca_prop_listener, NULL);

    for (usize i = 0; i < g_ca.watches.count; i++)
        watch_remove_listeners(&g_ca.watches.items[i]);
    mel_array_free(&g_ca.watches);

    for (usize i = 0; i < g_ca.opens.count; i++)
        od_teardown(g_ca.opens.items[i]);
    mel_array_free(&g_ca.opens);

    ca_strings_clear();
    mel_array_free(&g_ca.strings);
    mel_array_free(&g_ca.rates);
    if (g_ca.default_id.data)
        mel_dealloc(g_ca.alloc, g_ca.default_id.data);
    memset(&g_ca, 0, sizeof g_ca);
}

void mel_audioout__register_host_providers(void)
{
    static const Mel_AudioOut_Provider_Desc desc = {
        .name = "coreaudio",
        .enumerate = ca_enumerate,
        .default_id = ca_default_id,
        .open = ca_open,
        .start = ca_start,
        .stop = ca_stop,
        .close = ca_close,
        .volume = ca_volume,
        .set_volume = ca_set_volume,
        .muted = ca_muted,
        .set_muted = ca_set_muted,
        .native = ca_native,
        .shutdown = ca_shutdown,
    };

    g_ca.alloc = mel_alloc_heap();
    mel_array_init(&g_ca.strings, g_ca.alloc);
    mel_array_init(&g_ca.rates, g_ca.alloc);
    mel_array_init(&g_ca.opens, g_ca.alloc);
    mel_array_init(&g_ca.watches, g_ca.alloc);
    g_ca.default_id = STR8_EMPTY;
    g_ca.provider = mel_audioout_provider_register(&desc);
    g_ca.registered = true;

    OSStatus st = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &g_ca_devices_addr, ca_prop_listener, NULL);
    if (st != noErr)
        mel_log_warn("audioout", "coreaudio: device-set listener failed (OSStatus %d); hotplug needs manual refresh", (i32)st);
    else
        g_ca.devices_listener = true;

    st = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &g_ca_default_output_addr, ca_prop_listener, NULL);
    if (st != noErr)
        mel_log_warn("audioout", "coreaudio: default-output listener failed (OSStatus %d); default changes need manual refresh", (i32)st);
    else
        g_ca.default_listener = true;
}
