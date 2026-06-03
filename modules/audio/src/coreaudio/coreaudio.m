#include <audio/backend.h>

#include "../audio_internal.h"

#include <core/types.h>
#include <allocator/allocator.h>
#include <event/event.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>

#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AudioUnit.h>
#import <CoreAudio/CoreAudio.h>

typedef struct
{
    AudioUnit       unit;
    u32             opened;
    u32             started;
    u32             channels;
    u32             block_frames;
    f64             samplerate;
    _Atomic(Mel_Audio_Ring*) ring;
    _Atomic(u64)    underruns;
    _Atomic(u32)    underrun_signalled;
    _Atomic(u32)    primed;
    _Atomic(Mel_Event*) device_events;
    u32             listener_installed;
} Mel_Audio__CoreAudio;

static Mel_Audio__CoreAudio g_ca;

#define MEL_AUDIO__DEVICE_EVENT_DEFAULT_CHANGED 1u

static const AudioObjectPropertyAddress g_ca_default_device_addr = {
    .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
    .mScope = kAudioObjectPropertyScopeGlobal,
    .mElement = kAudioObjectPropertyElementMain,
};

static OSStatus mel_audio__ca_device_listener(AudioObjectID obj, UInt32 n, const AudioObjectPropertyAddress* addrs, void* user)
{
    MEL_UNUSED(obj);
    MEL_UNUSED(n);
    MEL_UNUSED(addrs);
    MEL_UNUSED(user);

    Mel_Event* ev = atomic_load_explicit(&g_ca.device_events, memory_order_acquire);
    if (ev != NULL)
    {
        u32 code = MEL_AUDIO__DEVICE_EVENT_DEFAULT_CHANGED;
        mel_event_fire(ev, &code);
    }
    return noErr;
}

static OSStatus mel_audio__ca_render(void* user, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* ts, u32 bus, u32 frames, AudioBufferList* io)
{
    MEL_UNUSED(user);
    MEL_UNUSED(ts);
    MEL_UNUSED(bus);

    Mel_Audio_Ring* ring = atomic_load_explicit(&g_ca.ring, memory_order_acquire);

    for (u32 b = 0; b < io->mNumberBuffers; b++)
    {
        AudioBuffer* buf = &io->mBuffers[b];
        f32*         dst = (f32*)buf->mData;
        u32          dst_samples = buf->mDataByteSize / (u32)sizeof(f32);

        if (ring == NULL)
        {
            memset(dst, 0, buf->mDataByteSize);
            if (flags != NULL)
                *flags |= kAudioUnitRenderAction_OutputIsSilence;
            continue;
        }

        u32 want = frames * g_ca.channels;
        if (want > dst_samples)
            want = dst_samples;

        u32 got = mel_audio_ring_read(ring, dst, want);

        if (got > 0u)
            atomic_store_explicit(&g_ca.primed, 1u, memory_order_relaxed);

        if (got < want)
        {
            memset(dst + got, 0, (usize)(want - got) * sizeof(f32));
            if (atomic_load_explicit(&g_ca.primed, memory_order_relaxed) != 0u)
            {
                u64 prev = atomic_fetch_add_explicit(&g_ca.underruns, 1u, memory_order_relaxed);
                if (prev == 0u && atomic_exchange_explicit(&g_ca.underrun_signalled, 1u, memory_order_relaxed) == 0u)
                    assert(!"mel audio coreaudio: ring underrun (mix thread did not keep ahead)");
            }
        }

        if (dst_samples > want)
            memset(dst + want, 0, (usize)(dst_samples - want) * sizeof(f32));
    }

    return noErr;
}

static AudioComponent mel_audio__ca_find_component(void)
{
    AudioComponentDescription desc = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_HALOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
        .componentFlags = 0,
        .componentFlagsMask = 0,
    };
    return AudioComponentFindNext(NULL, &desc);
}

bool mel_audio_backend_open(Mel_Audio_Opt req, Mel_Audio_Caps* granted, const Mel_Alloc* a)
{
    MEL_UNUSED(a);
    assert(granted != NULL);
    assert(req.samplerate > 0u);
    assert(req.channels >= 1u);

    if (g_ca.opened)
    {
        mel_log_error("audio", "coreaudio: backend already open");
        return false;
    }

    AudioComponent comp = mel_audio__ca_find_component();
    if (comp == NULL)
    {
        mel_log_error("audio", "coreaudio: no HALOutput AudioComponent (requested %uHz %uch)", req.samplerate, req.channels);
        return false;
    }

    AudioUnit unit = NULL;
    OSStatus  st = AudioComponentInstanceNew(comp, &unit);
    if (st != noErr || unit == NULL)
    {
        mel_log_error("audio", "coreaudio: AudioComponentInstanceNew failed (OSStatus %d)", (i32)st);
        return false;
    }

    UInt32 enable_output = 1u;
    st = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enable_output, sizeof(enable_output));
    if (st != noErr)
    {
        mel_log_error("audio", "coreaudio: enable output IO failed (OSStatus %d)", (i32)st);
        AudioComponentInstanceDispose(unit);
        return false;
    }

    f64 want_rate = (f64)req.samplerate;
    u32 want_ch = req.channels;

    AudioStreamBasicDescription fmt = {
        .mSampleRate = want_rate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = (UInt32)(sizeof(f32) * want_ch),
        .mFramesPerPacket = 1u,
        .mBytesPerFrame = (UInt32)(sizeof(f32) * want_ch),
        .mChannelsPerFrame = want_ch,
        .mBitsPerChannel = 32u,
        .mReserved = 0u,
    };

    st = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));
    if (st != noErr)
    {
        mel_log_error("audio", "coreaudio: set input stream format failed (OSStatus %d, requested %uHz %uch interleaved f32)", (i32)st, req.samplerate, req.channels);
        AudioComponentInstanceDispose(unit);
        return false;
    }

    AudioStreamBasicDescription got_fmt = { 0 };
    UInt32                      got_size = sizeof(got_fmt);
    st = AudioUnitGetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &got_fmt, &got_size);
    if (st != noErr)
    {
        mel_log_error("audio", "coreaudio: get input stream format failed (OSStatus %d)", (i32)st);
        AudioComponentInstanceDispose(unit);
        return false;
    }

    u32 granted_ch = got_fmt.mChannelsPerFrame;
    f64 granted_rate = got_fmt.mSampleRate;
    if (granted_ch == 0u || granted_rate <= 0.0)
    {
        mel_log_error("audio", "coreaudio: device granted invalid format (%gHz %uch)", granted_rate, granted_ch);
        AudioComponentInstanceDispose(unit);
        return false;
    }
    if ((got_fmt.mFormatFlags & kAudioFormatFlagIsFloat) == 0u || (got_fmt.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0u)
    {
        mel_log_error("audio", "coreaudio: device refused interleaved float (flags 0x%x)", (u32)got_fmt.mFormatFlags);
        AudioComponentInstanceDispose(unit);
        return false;
    }

    u32    block = req.block_frames > 0u ? req.block_frames : 512u;
    UInt32 max_fps = block;
    st = AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max_fps, sizeof(max_fps));
    if (st != noErr)
    {
        mel_log_error("audio", "coreaudio: set MaximumFramesPerSlice=%u failed (OSStatus %d)", block, (i32)st);
        AudioComponentInstanceDispose(unit);
        return false;
    }

    UInt32 got_max_fps = 0u;
    UInt32 got_max_size = sizeof(got_max_fps);
    st = AudioUnitGetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &got_max_fps, &got_max_size);
    if (st == noErr && got_max_fps > 0u)
        block = got_max_fps;

    AURenderCallbackStruct cb = {
        .inputProc = mel_audio__ca_render,
        .inputProcRefCon = &g_ca,
    };
    st = AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof(cb));
    if (st != noErr)
    {
        mel_log_error("audio", "coreaudio: set render callback failed (OSStatus %d)", (i32)st);
        AudioComponentInstanceDispose(unit);
        return false;
    }

    st = AudioUnitInitialize(unit);
    if (st != noErr)
    {
        mel_log_error("audio", "coreaudio: AudioUnitInitialize failed (OSStatus %d)", (i32)st);
        AudioComponentInstanceDispose(unit);
        return false;
    }

    assert(req.ring_blocks > 0u);
    u32 ring_blocks = req.ring_blocks;

    f64    device_latency = 0.0;
    UInt32 lat_size = sizeof(device_latency);
    OSStatus lat_st = AudioUnitGetProperty(unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &device_latency, &lat_size);
    u32 device_latency_frames = (lat_st == noErr && device_latency > 0.0) ? (u32)(device_latency * granted_rate) : 0u;

    g_ca.unit = unit;
    g_ca.opened = 1u;
    g_ca.started = 0u;
    g_ca.channels = granted_ch;
    g_ca.block_frames = block;
    g_ca.samplerate = granted_rate;
    atomic_store_explicit(&g_ca.ring, NULL, memory_order_relaxed);
    atomic_store_explicit(&g_ca.underruns, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_ca.underrun_signalled, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_ca.primed, 0u, memory_order_relaxed);

    *granted = (Mel_Audio_Caps){
        .samplerate = (u32)(granted_rate + 0.5),
        .channels = granted_ch,
        .block_frames = block,
        .ring_blocks = ring_blocks,
        .latency_frames = ring_blocks * block + device_latency_frames,
    };

    if ((u32)(granted_rate + 0.5) != req.samplerate || granted_ch != req.channels)
        mel_log_warn("audio", "coreaudio: granted %uHz %uch (requested %uHz %uch)", (u32)(granted_rate + 0.5), granted_ch, req.samplerate, req.channels);

    mel_log_info("audio", "coreaudio: opened %uHz %uch block %u ring %u blocks latency %u frames", (u32)(granted_rate + 0.5), granted_ch, block, ring_blocks, ring_blocks * block + device_latency_frames);
    return true;
}

void mel_audio_backend_start(Mel_Audio_Ring* ring)
{
    assert(ring != NULL);
    assert(g_ca.opened);

    if (g_ca.started)
    {
        mel_log_warn("audio", "coreaudio: backend already started");
        return;
    }

    atomic_store_explicit(&g_ca.ring, ring, memory_order_release);

    OSStatus st = AudioOutputUnitStart(g_ca.unit);
    if (st != noErr)
    {
        atomic_store_explicit(&g_ca.ring, NULL, memory_order_release);
        mel_log_error("audio", "coreaudio: AudioOutputUnitStart failed (OSStatus %d)", (i32)st);
        assert(!"mel audio coreaudio: AudioOutputUnitStart failed");
        return;
    }

    g_ca.started = 1u;
}

void mel_audio_backend_stop(void)
{
    if (!g_ca.opened || !g_ca.started)
        return;

    OSStatus st = AudioOutputUnitStop(g_ca.unit);
    if (st != noErr)
        mel_log_error("audio", "coreaudio: AudioOutputUnitStop failed (OSStatus %d)", (i32)st);

    atomic_store_explicit(&g_ca.ring, NULL, memory_order_release);
    g_ca.started = 0u;

    u64 underruns = atomic_load_explicit(&g_ca.underruns, memory_order_relaxed);
    if (underruns > 0u)
        mel_log_warn("audio", "coreaudio: %llu underruns over session", (unsigned long long)underruns);
}

void mel_audio_backend_close(const Mel_Alloc* a)
{
    MEL_UNUSED(a);
    if (!g_ca.opened)
        return;

    if (g_ca.started)
        mel_audio_backend_stop();

    if (g_ca.unit != NULL)
    {
        OSStatus st = AudioUnitUninitialize(g_ca.unit);
        if (st != noErr)
            mel_log_error("audio", "coreaudio: AudioUnitUninitialize failed (OSStatus %d)", (i32)st);
        st = AudioComponentInstanceDispose(g_ca.unit);
        if (st != noErr)
            mel_log_error("audio", "coreaudio: AudioComponentInstanceDispose failed (OSStatus %d)", (i32)st);
    }

    g_ca.unit = NULL;
    g_ca.opened = 0u;
    g_ca.started = 0u;
    atomic_store_explicit(&g_ca.ring, NULL, memory_order_relaxed);
}

void mel_audio_backend_set_device_event(Mel_Event* ev)
{
    if (ev != NULL)
    {
        atomic_store_explicit(&g_ca.device_events, ev, memory_order_release);
        if (!g_ca.listener_installed)
        {
            OSStatus st = AudioObjectAddPropertyListener(kAudioObjectSystemObject, &g_ca_default_device_addr, mel_audio__ca_device_listener, NULL);
            if (st != noErr)
                mel_log_warn("audio", "coreaudio: install default-device listener failed (OSStatus %d); hotplug events unfired", (i32)st);
            else
                g_ca.listener_installed = 1u;
        }
        return;
    }

    if (g_ca.listener_installed)
    {
        OSStatus st = AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &g_ca_default_device_addr, mel_audio__ca_device_listener, NULL);
        if (st != noErr)
            mel_log_warn("audio", "coreaudio: remove default-device listener failed (OSStatus %d)", (i32)st);
        g_ca.listener_installed = 0u;
    }
    atomic_store_explicit(&g_ca.device_events, NULL, memory_order_release);
}
