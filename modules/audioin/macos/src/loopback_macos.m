#include "audioin_macos_internal.h"

#include <allocator/allocator.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>

#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <Foundation/Foundation.h>

typedef struct
{
    const Mel_Alloc*                   alloc;
    AudioObjectID                      tap;
    AudioObjectID                      aggregate;
    AudioDeviceIOProcID                proc_id;
    u32                                channels;
    u32                                samplerate;
    u32                                capacity_frames;
    f32*                               scratch;
    _Atomic(Mel_AudioIn__Macos_Sinks*) sinks;
    _Atomic(Mel_AudioIn__Macos_Sinks*) garbage;
    _Atomic(u32)                       overruns;
    bool                               running;
} Loopback_State;

static Loopback_State g_lb;

bool mel_audioin__macos_loopback_available(void)
{
    if (__builtin_available(macOS 14.2, *))
        return true;
    return false;
}

static OSStatus lb_io_proc(AudioObjectID device, const AudioTimeStamp* now, const AudioBufferList* input, const AudioTimeStamp* input_time, AudioBufferList* output, const AudioTimeStamp* output_time, void* user)
{
    MEL_UNUSED(device);
    MEL_UNUSED(now);
    MEL_UNUSED(input_time);
    MEL_UNUSED(output);
    MEL_UNUSED(output_time);
    MEL_UNUSED(user);

    Mel_AudioIn__Macos_Sinks* sl = atomic_load_explicit(&g_lb.sinks, memory_order_acquire);
    if (!sl || sl->count == 0 || !input || input->mNumberBuffers == 0)
        return noErr;

    const f32* interleaved = NULL;
    u32        frames = 0;
    if (input->mNumberBuffers == 1 && input->mBuffers[0].mNumberChannels == g_lb.channels)
    {
        interleaved = input->mBuffers[0].mData;
        frames = input->mBuffers[0].mDataByteSize / ((u32)sizeof(f32) * g_lb.channels);
    }
    else
    {
        u32 src_ch = input->mBuffers[0].mNumberChannels;
        if (src_ch == 0)
            return noErr;
        frames = input->mBuffers[0].mDataByteSize / ((u32)sizeof(f32) * src_ch);
        if (frames > g_lb.capacity_frames)
        {
            atomic_fetch_add_explicit(&g_lb.overruns, 1u, memory_order_relaxed);
            return noErr;
        }
        u32 ch = 0;
        for (UInt32 b = 0; b < input->mNumberBuffers && ch < g_lb.channels; b++)
        {
            const f32* src = input->mBuffers[b].mData;
            u32        bch = input->mBuffers[b].mNumberChannels;
            for (u32 c = 0; c < bch && ch < g_lb.channels; c++, ch++)
                for (u32 f = 0; f < frames; f++)
                    g_lb.scratch[f * g_lb.channels + ch] = src[f * bch + c];
        }
        for (; ch < g_lb.channels; ch++)
            for (u32 f = 0; f < frames; f++)
                g_lb.scratch[f * g_lb.channels + ch] = 0.0f;
        interleaved = g_lb.scratch;
    }

    if (frames == 0 || !interleaved)
        return noErr;
    for (u32 i = 0; i < sl->count; i++)
        if (sl->sinks[i].on_frames)
            sl->sinks[i].on_frames(sl->sinks[i].token, interleaved, frames, g_lb.samplerate, g_lb.channels);
    return noErr;
}

static Mel_AudioIn_Status lb_sink_add(Mel_AudioIn_Sink sink)
{
    Mel_AudioIn__Macos_Sinks* cur = atomic_load_explicit(&g_lb.sinks, memory_order_acquire);
    Mel_AudioIn__Macos_Sinks* nl = mel_audioin__macos_sinks_with(g_lb.alloc, cur, sink);
    if (!nl)
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    Mel_AudioIn__Macos_Sinks* old = atomic_exchange_explicit(&g_lb.sinks, nl, memory_order_acq_rel);
    if (old)
        mel_audioin__macos_garbage_push(&g_lb.garbage, old);
    return MEL_AUDIOIN_OK;
}

static void lb_teardown(void)
{
    if (g_lb.proc_id)
    {
        AudioDeviceStop(g_lb.aggregate, g_lb.proc_id);
        AudioDeviceDestroyIOProcID(g_lb.aggregate, g_lb.proc_id);
    }
    if (g_lb.aggregate != kAudioObjectUnknown)
        AudioHardwareDestroyAggregateDevice(g_lb.aggregate);
    if (g_lb.tap != kAudioObjectUnknown)
    {
        if (__builtin_available(macOS 14.2, *))
            AudioHardwareDestroyProcessTap(g_lb.tap);
    }

    u32 overruns = atomic_load_explicit(&g_lb.overruns, memory_order_relaxed);
    if (overruns > 0)
        mel_log_warn("audioin", "coreaudio: %u oversized loopback slices dropped", overruns);

    Mel_AudioIn__Macos_Sinks* sl = atomic_exchange_explicit(&g_lb.sinks, NULL, memory_order_acq_rel);
    if (sl)
        mel_dealloc(g_lb.alloc, sl);
    mel_audioin__macos_garbage_drain(g_lb.alloc, &g_lb.garbage);
    if (g_lb.scratch)
        mel_dealloc(g_lb.alloc, g_lb.scratch);
    memset(&g_lb, 0, sizeof g_lb);
}

Mel_AudioIn_Status mel_audioin__macos_loopback_open(const Mel_Alloc* alloc, Mel_AudioIn_Sink sink)
{
    if (g_lb.running)
        return lb_sink_add(sink);

    if (__builtin_available(macOS 14.2, *))
    {
        @autoreleasepool
        {
            CATapDescription* desc = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
            desc.name = @"Melody System Audio Tap";
            desc.privateTap = YES;
            desc.muteBehavior = CATapUnmuted;

            AudioObjectID tap = kAudioObjectUnknown;
            OSStatus      st = AudioHardwareCreateProcessTap(desc, &tap);
            if (st != noErr || tap == kAudioObjectUnknown)
            {
                mel_log_error("audioin", "coreaudio: process tap creation failed (OSStatus %d); is audio-capture consent granted?", (i32)st);
                return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_DENIED;
            }

            AudioObjectPropertyAddress fmt_addr = {
                kAudioTapPropertyFormat,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain,
            };
            AudioStreamBasicDescription fmt;
            memset(&fmt, 0, sizeof fmt);
            UInt32 fmt_size = sizeof fmt;
            st = AudioObjectGetPropertyData(tap, &fmt_addr, 0, NULL, &fmt_size, &fmt);
            if (st != noErr || fmt.mSampleRate <= 0.0 || fmt.mChannelsPerFrame == 0)
            {
                mel_log_error("audioin", "coreaudio: tap format query failed (OSStatus %d)", (i32)st);
                AudioHardwareDestroyProcessTap(tap);
                return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
            }

            NSString*     tap_uid = desc.UUID.UUIDString;
            NSDictionary* composition = @{
                @kAudioAggregateDeviceNameKey : @"Melody System Loopback",
                @kAudioAggregateDeviceUIDKey : [NSString stringWithFormat:@"com.melody.audioin.loopback.%@", tap_uid],
                @kAudioAggregateDeviceIsPrivateKey : @YES,
                @kAudioAggregateDeviceTapAutoStartKey : @YES,
                @kAudioAggregateDeviceTapListKey : @[ @{
                    @kAudioSubTapUIDKey : tap_uid,
                    @kAudioSubTapDriftCompensationKey : @YES,
                } ],
            };

            AudioObjectID agg = kAudioObjectUnknown;
            st = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)composition, &agg);
            if (st != noErr || agg == kAudioObjectUnknown)
            {
                mel_log_error("audioin", "coreaudio: loopback aggregate device creation failed (OSStatus %d)", (i32)st);
                AudioHardwareDestroyProcessTap(tap);
                return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
            }

            AudioDeviceIOProcID proc_id = NULL;
            st = AudioDeviceCreateIOProcID(agg, lb_io_proc, NULL, &proc_id);
            if (st != noErr || proc_id == NULL)
            {
                mel_log_error("audioin", "coreaudio: loopback IOProc creation failed (OSStatus %d)", (i32)st);
                AudioHardwareDestroyAggregateDevice(agg);
                AudioHardwareDestroyProcessTap(tap);
                return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
            }

            AudioObjectPropertyAddress bufsize_addr = {
                kAudioDevicePropertyBufferFrameSize,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain,
            };
            UInt32 buffer_frames = 0;
            UInt32 buffer_size = sizeof buffer_frames;
            if (AudioObjectGetPropertyData(agg, &bufsize_addr, 0, NULL, &buffer_size, &buffer_frames) != noErr)
                buffer_frames = 0;
            u32 capacity = buffer_frames > 0 ? buffer_frames * 4u : 16384u;
            if (capacity < 4096u)
                capacity = 4096u;

            f32* scratch = mel_alloc(alloc, sizeof(f32) * (usize)capacity * fmt.mChannelsPerFrame);
            if (!scratch)
            {
                AudioDeviceDestroyIOProcID(agg, proc_id);
                AudioHardwareDestroyAggregateDevice(agg);
                AudioHardwareDestroyProcessTap(tap);
                return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
            }

            g_lb.alloc = alloc;
            g_lb.tap = tap;
            g_lb.aggregate = agg;
            g_lb.proc_id = proc_id;
            g_lb.channels = fmt.mChannelsPerFrame;
            g_lb.samplerate = (u32)(fmt.mSampleRate + 0.5);
            g_lb.capacity_frames = capacity;
            g_lb.scratch = scratch;
            atomic_store_explicit(&g_lb.sinks, NULL, memory_order_relaxed);
            atomic_store_explicit(&g_lb.garbage, NULL, memory_order_relaxed);
            atomic_store_explicit(&g_lb.overruns, 0u, memory_order_relaxed);
            g_lb.running = true;

            st = AudioDeviceStart(agg, proc_id);
            if (st != noErr)
            {
                mel_log_error("audioin", "coreaudio: loopback aggregate start failed (OSStatus %d)", (i32)st);
                lb_teardown();
                return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
            }

            mel_log_info("audioin", "coreaudio: system loopback running (%u ch @ %u Hz)", g_lb.channels, g_lb.samplerate);
            return lb_sink_add(sink);
        }
    }

    mel_log_error("audioin", "coreaudio: system loopback requires macOS 14.2+");
    return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
}

void mel_audioin__macos_loopback_close(void* token)
{
    if (!g_lb.running)
    {
        mel_log_warn("audioin", "coreaudio: loopback close while not open");
        return;
    }

    Mel_AudioIn__Macos_Sinks* cur = atomic_load_explicit(&g_lb.sinks, memory_order_acquire);
    u32                       remaining = 0;
    if (cur && cur->count > 0)
    {
        Mel_AudioIn__Macos_Sinks* nl = mel_audioin__macos_sinks_without(g_lb.alloc, cur, token);
        if (!nl)
        {
            mel_log_error("audioin", "coreaudio: loopback close: sink list allocation failed");
            return;
        }
        remaining = nl->count;
        Mel_AudioIn__Macos_Sinks* old;
        if (remaining == 0)
        {
            old = atomic_exchange_explicit(&g_lb.sinks, NULL, memory_order_acq_rel);
            mel_dealloc(g_lb.alloc, nl);
        }
        else
            old = atomic_exchange_explicit(&g_lb.sinks, nl, memory_order_acq_rel);
        if (old)
            mel_audioin__macos_garbage_push(&g_lb.garbage, old);
    }

    if (remaining == 0)
        lb_teardown();
}

void* mel_audioin__macos_loopback_native(void) { return g_lb.running ? (void*)(usize)g_lb.aggregate : NULL; }

void mel_audioin__macos_loopback_shutdown(void)
{
    if (g_lb.running)
        lb_teardown();
}
