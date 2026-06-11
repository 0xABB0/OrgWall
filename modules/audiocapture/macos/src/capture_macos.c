#include "../../src/audiocapture_internal.h"

#include <assert.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

#define MEL_AC_BUFFER_COUNT 3

typedef struct
{
    AudioQueueRef queue;
} Mel_AC_Backend;

static bool mel_ac__device_has_input(AudioDeviceID id)
{
    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyStreams,
        kAudioObjectPropertyScopeInput,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(id, &addr, 0, NULL, &size) != noErr)
        return false;
    return size > 0;
}

i32 mel_audiocapture_enumerate(u32* out_ids, i32 max_count)
{
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };

    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, NULL, &size) != noErr || size == 0)
        return 0;

    UInt32        device_count = size / sizeof(AudioDeviceID);
    AudioDeviceID ids[device_count];
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, ids) != noErr)
        return 0;

    i32 n = 0;
    for (UInt32 i = 0; i < device_count && n < max_count; i++)
        if (mel_ac__device_has_input(ids[i]))
            out_ids[n++] = (u32)ids[i];
    return n;
}

str8 mel_audiocapture_device_name(u32 id, const Mel_Alloc* alloc)
{
    AudioObjectPropertyAddress addr = {
        kAudioObjectPropertyName,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };

    CFStringRef name = NULL;
    UInt32      size = sizeof(name);
    if (AudioObjectGetPropertyData((AudioDeviceID)id, &addr, 0, NULL, &size, &name) != noErr || !name)
        return STR8_EMPTY;

    CFIndex utf8_max = CFStringGetMaximumSizeForEncoding(CFStringGetLength(name), kCFStringEncodingUTF8) + 1;
    char*   buf = mel_alloc(alloc, (usize)utf8_max);
    str8    result = STR8_EMPTY;
    if (CFStringGetCString(name, buf, utf8_max, kCFStringEncodingUTF8))
        result = str8_from_cstr(buf);
    else
        mel_dealloc(alloc, buf);

    CFRelease(name);
    return result;
}

bool mel_audiocapture_default_device(u32* out_id)
{
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };

    AudioDeviceID id = kAudioObjectUnknown;
    UInt32        size = sizeof(id);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, &id) != noErr || id == kAudioObjectUnknown)
        return false;

    *out_id = (u32)id;
    return true;
}

static void mel_ac__input_cb(void* user, AudioQueueRef q, AudioQueueBufferRef buf, const AudioTimeStamp* ts, UInt32 packet_count, const AudioStreamPacketDescription* packets)
{
    (void)ts;
    (void)packet_count;
    (void)packets;

    Mel_AudioCapture* c = user;
    u32               frames = buf->mAudioDataByteSize / sizeof(f32);
    mel_ac_ring_write(&c->ring, (const f32*)buf->mAudioData, frames);
    AudioQueueEnqueueBuffer(q, buf, 0, NULL);
}

static bool mel_ac__select_device(AudioQueueRef queue, u32 device_id)
{
    u32 default_id = 0;
    if (mel_audiocapture_default_device(&default_id) && default_id == device_id)
        return true;

    AudioObjectPropertyAddress addr = {
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };

    CFStringRef uid = NULL;
    UInt32      size = sizeof(uid);
    if (AudioObjectGetPropertyData((AudioDeviceID)device_id, &addr, 0, NULL, &size, &uid) != noErr || !uid)
        return false;

    OSStatus rc = AudioQueueSetProperty(queue, kAudioQueueProperty_CurrentDevice, &uid, sizeof(uid));
    CFRelease(uid);
    return rc == noErr;
}

Mel_AudioCapture* mel_audiocapture_open(const Mel_Alloc* alloc, u32 device_id, Mel_AudioCapture_Opt opt)
{
    assert(alloc);
    assert(opt.sample_rate > 0);
    assert(opt.ring_capacity_frames > 0);

    Mel_AudioCapture* c = mel_alloc_type(alloc, Mel_AudioCapture);
    c->alloc = alloc;
    c->opt = opt;
    c->backend = NULL;
    mel_ac_ring_init(&c->ring, alloc, opt.ring_capacity_frames);

    AudioStreamBasicDescription fmt = { 0 };
    fmt.mSampleRate = (Float64)opt.sample_rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mFramesPerPacket = 1;
    fmt.mChannelsPerFrame = 1;
    fmt.mBitsPerChannel = 32;
    fmt.mBytesPerFrame = sizeof(f32);
    fmt.mBytesPerPacket = sizeof(f32);

    AudioQueueRef queue = NULL;
    if (AudioQueueNewInput(&fmt, mel_ac__input_cb, c, NULL, NULL, 0, &queue) != noErr)
        goto fail;

    if (!mel_ac__select_device(queue, device_id))
        goto fail_queue;

    u32 buffer_frames = opt.sample_rate / 50;
    for (i32 i = 0; i < MEL_AC_BUFFER_COUNT; i++)
    {
        AudioQueueBufferRef buf = NULL;
        if (AudioQueueAllocateBuffer(queue, buffer_frames * sizeof(f32), &buf) != noErr)
            goto fail_queue;
        AudioQueueEnqueueBuffer(queue, buf, 0, NULL);
    }

    if (AudioQueueStart(queue, NULL) != noErr)
        goto fail_queue;

    Mel_AC_Backend* backend = mel_alloc_type(alloc, Mel_AC_Backend);
    backend->queue = queue;
    c->backend = backend;
    return c;

fail_queue:
    AudioQueueDispose(queue, true);
fail:
    mel_ac_ring_free(&c->ring, alloc);
    mel_dealloc(alloc, c);
    return NULL;
}

u32 mel_audiocapture_read(Mel_AudioCapture* c, f32* dst, u32 max_frames) { return mel_ac_ring_read(&c->ring, dst, max_frames); }

u32 mel_audiocapture_available(const Mel_AudioCapture* c) { return mel_ac_ring_available(&c->ring); }

void mel_audiocapture_close(Mel_AudioCapture* c)
{
    if (!c)
        return;

    Mel_AC_Backend* backend = c->backend;
    if (backend)
    {
        AudioQueueStop(backend->queue, true);
        AudioQueueDispose(backend->queue, true);
        mel_dealloc(c->alloc, backend);
    }

    mel_ac_ring_free(&c->ring, c->alloc);
    mel_dealloc(c->alloc, c);
}
