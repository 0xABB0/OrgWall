#include <stt/provider.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <string.h>

#import <TargetConditionals.h>
#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>
#import <os/lock.h>

#if TARGET_OS_OSX
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>
#endif

@interface                                                          Mel_Stt_Apple_Job: NSObject
@property(nonatomic, strong) SFSpeechRecognizer*                    recognizer;
@property(nonatomic, strong) SFSpeechAudioBufferRecognitionRequest* request;
@property(nonatomic, strong) SFSpeechRecognitionTask*               task;
@property(nonatomic, strong) AVAudioEngine*                         engine;
@property(nonatomic, strong) AVAudioFormat*                         feedFormat;
@property(nonatomic, assign) u64                                    token;
@property(nonatomic, assign) Mel_Stt_Sink                           sink;
@end

@implementation Mel_Stt_Apple_Job
@end

typedef Mel_Array(str8) Apple_Strings;

static struct
{
    const Mel_Alloc* alloc;
    Apple_Strings    strings;
} g_apple_stt;

static os_unfair_lock                                       g_apple_stt_lock = OS_UNFAIR_LOCK_INIT;
static NSMutableDictionary<NSNumber*, Mel_Stt_Apple_Job*>*  g_apple_stt_jobs;
static NSMutableDictionary<NSNumber*, SFSpeechRecognizer*>* g_apple_stt_recognizers;

static void apple_job_put(u64 token, Mel_Stt_Apple_Job* job)
{
    os_unfair_lock_lock(&g_apple_stt_lock);
    if (g_apple_stt_jobs == nil)
        g_apple_stt_jobs = [NSMutableDictionary dictionary];
    g_apple_stt_jobs[@(token)] = job;
    os_unfair_lock_unlock(&g_apple_stt_lock);
}

static Mel_Stt_Apple_Job* apple_job_get(u64 token)
{
    os_unfair_lock_lock(&g_apple_stt_lock);
    Mel_Stt_Apple_Job* job = g_apple_stt_jobs[@(token)];
    os_unfair_lock_unlock(&g_apple_stt_lock);
    return job;
}

static Mel_Stt_Apple_Job* apple_job_take(u64 token)
{
    os_unfair_lock_lock(&g_apple_stt_lock);
    Mel_Stt_Apple_Job* job = g_apple_stt_jobs[@(token)];
    if (job)
        [g_apple_stt_jobs removeObjectForKey:@(token)];
    os_unfair_lock_unlock(&g_apple_stt_lock);
    return job;
}

static void apple_strings_clear(void)
{
    for (usize i = 0; i < g_apple_stt.strings.count; i++)
        if (g_apple_stt.strings.items[i].data)
            mel_dealloc(g_apple_stt.alloc, g_apple_stt.strings.items[i].data);
    mel_array_clear(&g_apple_stt.strings);
}

static void apple_strings_prepare(const Mel_Alloc* alloc)
{
    if (g_apple_stt.strings.allocator != NULL)
        apple_strings_clear();
    g_apple_stt.alloc = alloc;
    if (g_apple_stt.strings.allocator == NULL)
        mel_array_init(&g_apple_stt.strings, alloc);
}

static str8 apple_intern(const char* utf8)
{
    usize len = utf8 ? strlen(utf8) : 0;
    u8*   data = (u8*)mel_alloc(g_apple_stt.alloc, len + 1);
    if (!data)
        return (str8){ 0 };
    if (len)
        memcpy(data, utf8, len);
    data[len] = 0;
    str8 s = { data, (size)len };
    mel_array_push(&g_apple_stt.strings, s);
    return s;
}

static u64 apple_locale_stable_id(NSLocale* locale) { return (u64)[locale.localeIdentifier hash]; }

static bool apple_caps_punctuation(void)
{
    if (@available(macOS 13.0, iOS 16.0, *))
        return true;
    return false;
}

static u32 apple_enumerate_recognizers(void* user, const Mel_Alloc* alloc, Mel_Stt_Recognizer_Raw* out, u32 cap)
{
    (void)user;
    apple_strings_prepare(alloc);
    @autoreleasepool
    {
        NSSet<NSLocale*>*                                    locales = [SFSpeechRecognizer supportedLocales];
        NSMutableDictionary<NSNumber*, SFSpeechRecognizer*>* fresh = [NSMutableDictionary dictionaryWithCapacity:locales.count];
        u32                                                  total = (u32)locales.count;
        u32                                                  n = 0;
        for (NSLocale* locale in locales)
        {
            u64                 sid = apple_locale_stable_id(locale);
            SFSpeechRecognizer* recognizer = g_apple_stt_recognizers[@(sid)];
            if (!recognizer)
                recognizer = [[SFSpeechRecognizer alloc] initWithLocale:locale];
            if (!recognizer)
            {
                mel_log_warn("stt", "apple: locale %s has no recognizer; skipped", locale.localeIdentifier.UTF8String);
                total--;
                continue;
            }
            fresh[@(sid)] = recognizer;
            if (n >= cap)
                continue;
            bool on_device = false;
            if (@available(macOS 10.15, iOS 13.0, *))
                on_device = recognizer.supportsOnDeviceRecognition ? true : false;
            out[n].stable_id = sid;
            out[n].language = apple_intern(locale.localeIdentifier.UTF8String);
            out[n].caps = (Mel_Stt_Recognizer_Caps){
                .on_device = on_device,
                .require_on_device = on_device,
                .partials = true,
                .can_stop = true,
                .feed = true,
#if TARGET_OS_OSX
                .device_select = true,
#else
                .device_select = false,
#endif
                .vocabulary = true,
                .punctuation = apple_caps_punctuation(),
                .profanity_filter = false,
            };
            n++;
        }
        g_apple_stt_recognizers = fresh;
        return total;
    }
}

static const mel_stt_auth* apple_auth_map(SFSpeechRecognizerAuthorizationStatus status)
{
    switch (status)
    {
    case SFSpeechRecognizerAuthorizationStatusAuthorized:
        return &mel_stt_auth_granted;
    case SFSpeechRecognizerAuthorizationStatusDenied:
        return &mel_stt_auth_denied;
    case SFSpeechRecognizerAuthorizationStatusRestricted:
        return &mel_stt_auth_restricted;
    default:
        return &mel_stt_auth_not_determined;
    }
}

static const mel_stt_auth* apple_authorization(void* user)
{
    (void)user;
    return apple_auth_map([SFSpeechRecognizer authorizationStatus]);
}

static void apple_authorize(void* user, Mel_Stt_Sink sink)
{
    (void)user;
    if (sink.on_auth == NULL)
        return;
    [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
        sink.on_auth(sink.token, apple_auth_map(status));
    }];
}

static void apple_job_teardown(Mel_Stt_Apple_Job* job)
{
    if (job.engine)
    {
        [job.engine.inputNode removeTapOnBus:0];
        [job.engine stop];
    }
}

static void apple_job_finish(u64 token, Mel_Stt_Status status)
{
    Mel_Stt_Apple_Job* job = apple_job_take(token);
    if (!job)
        return;
    Mel_Stt_Sink sink = job.sink;
    apple_job_teardown(job);
    if (sink.on_done)
        sink.on_done(sink.token, status);
}

#if TARGET_OS_OSX
static AudioDeviceID apple_device_for(str8 stable_id)
{
    str8 prefix = S8("coreaudio:");
    if (!str8_starts_with(stable_id, prefix) || g_apple_stt.alloc == NULL)
        return kAudioObjectUnknown;
    str8        uid = str8_suffix(stable_id, stable_id.len - prefix.len);
    CFStringRef want = CFStringCreateWithBytes(kCFAllocatorDefault, uid.data, (CFIndex)uid.len, kCFStringEncodingUTF8, false);
    if (!want)
        return kAudioObjectUnknown;
    AudioObjectPropertyAddress devices_addr = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioObjectPropertyAddress uid_addr = { kAudioDevicePropertyDeviceUID, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioDeviceID              found = kAudioObjectUnknown;
    UInt32                     bytes = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devices_addr, 0, NULL, &bytes) == noErr && bytes > 0)
    {
        AudioDeviceID* ids = (AudioDeviceID*)mel_alloc(g_apple_stt.alloc, bytes);
        if (ids && AudioObjectGetPropertyData(kAudioObjectSystemObject, &devices_addr, 0, NULL, &bytes, ids) == noErr)
        {
            u32 n = bytes / (u32)sizeof(AudioDeviceID);
            for (u32 i = 0; i < n && found == kAudioObjectUnknown; i++)
            {
                CFStringRef u = NULL;
                UInt32      usz = sizeof u;
                if (AudioObjectGetPropertyData(ids[i], &uid_addr, 0, NULL, &usz, &u) != noErr || u == NULL)
                    continue;
                if (CFEqual(u, want))
                    found = ids[i];
                CFRelease(u);
            }
        }
        if (ids)
            mel_dealloc(g_apple_stt.alloc, ids);
    }
    CFRelease(want);
    return found;
}
#endif

static Mel_Stt_Status apple_engine_begin(Mel_Stt_Apple_Job* job, const Mel_Stt_Listen_Lowered* lowered)
{
#if !TARGET_OS_OSX
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSString*       cat = session.category;
    bool            record_capable = [cat isEqualToString:AVAudioSessionCategoryPlayAndRecord] || [cat isEqualToString:AVAudioSessionCategoryRecord] || [cat isEqualToString:AVAudioSessionCategoryMultiRoute];
    NSError*        serr = nil;
    if (!record_capable && ![session setCategory:AVAudioSessionCategoryPlayAndRecord withOptions:AVAudioSessionCategoryOptionAllowBluetoothHFP error:&serr])
    {
        mel_log_error("stt", "apple listen: AVAudioSession category change failed: %s", serr ? serr.localizedDescription.UTF8String : "unknown");
        return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
    }
    if (![session setActive:YES error:&serr])
    {
        mel_log_error("stt", "apple listen: AVAudioSession activation failed: %s", serr ? serr.localizedDescription.UTF8String : "unknown");
        return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
    }
#endif

    job.engine = [[AVAudioEngine alloc] init];
    AVAudioInputNode* input = job.engine.inputNode;

    if (lowered->device_stable_id.len > 0)
    {
#if TARGET_OS_OSX
        AudioDeviceID dev = apple_device_for(lowered->device_stable_id);
        if (dev == kAudioObjectUnknown)
        {
            mel_log_error("stt", "apple listen: no CoreAudio device for %.*s; the device door binds CoreAudio-backed inputs only", (int)lowered->device_stable_id.len, lowered->device_stable_id.data);
            return MEL_STT_ERROR | MEL_STT_RESULT_NO_DEVICE;
        }
        AudioUnit au = input.audioUnit;
        if (au == NULL)
        {
            mel_log_error("stt", "apple listen: input node exposes no audio unit; cannot bind device");
            return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
        }
        OSStatus st = AudioUnitSetProperty(au, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev, sizeof dev);
        if (st != noErr)
        {
            mel_log_error("stt", "apple listen: binding device %.*s failed (OSStatus %d)", (int)lowered->device_stable_id.len, lowered->device_stable_id.data, (i32)st);
            return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
        }
#else
        mel_log_error("stt", "apple listen: device door lowered on a platform whose caps deny it; provider bug");
        return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
#endif
    }

    AVAudioFormat* format = [input outputFormatForBus:0];
    if (format.channelCount == 0)
    {
        mel_log_error("stt", "apple listen: no audio input route");
        return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
    }

    SFSpeechAudioBufferRecognitionRequest* request = job.request;
    [input installTapOnBus:0
                bufferSize:1024
                    format:format
                     block:^(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
                         (void)when;
                         [request appendAudioPCMBuffer:buffer];
                     }];
    [job.engine prepare];
    NSError* err = nil;
    if (![job.engine startAndReturnError:&err])
    {
        [input removeTapOnBus:0];
        mel_log_error("stt", "apple listen: audio engine start failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
        return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
    }
    return MEL_STT_OK;
}

static Mel_Stt_Status apple_listen(void* user, u64 stable_id, u64 token, const Mel_Stt_Listen_Lowered* lowered, Mel_Stt_Sink sink)
{
    (void)user;
    @autoreleasepool
    {
        if ([SFSpeechRecognizer authorizationStatus] != SFSpeechRecognizerAuthorizationStatusAuthorized)
        {
            mel_log_error("stt", "apple listen: speech recognition consent is %s; SFSpeechRecognizer demands it for every door", mel_stt_auth_name(apple_authorization(NULL)));
            return MEL_STT_ERROR | MEL_STT_RESULT_DENIED;
        }
        SFSpeechRecognizer* recognizer = g_apple_stt_recognizers[@(stable_id)];
        if (!recognizer)
        {
            mel_log_error("stt", "apple listen: recognizer %llu not found", (unsigned long long)stable_id);
            return MEL_STT_ERROR | MEL_STT_RESULT_NO_DEVICE;
        }
        if (!recognizer.isAvailable)
        {
            mel_log_error("stt", "apple listen: recognizer %llu unavailable", (unsigned long long)stable_id);
            return MEL_STT_ERROR | MEL_STT_RESULT_NETWORK;
        }

        Mel_Stt_Apple_Job* job = [[Mel_Stt_Apple_Job alloc] init];
        job.token = token;
        job.sink = sink;
        job.recognizer = recognizer;
        job.request = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
        job.request.shouldReportPartialResults = lowered->partials ? YES : NO;

        if (lowered->require_on_device)
        {
            if (@available(macOS 10.15, iOS 13.0, *))
                job.request.requiresOnDeviceRecognition = YES;
            else
            {
                mel_log_error("stt", "apple listen: require_on_device lowered onto an OS without it; caps over-claim");
                return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
            }
        }
        if (lowered->punctuation)
        {
            if (@available(macOS 13.0, iOS 16.0, *))
                job.request.addsPunctuation = YES;
            else
            {
                mel_log_error("stt", "apple listen: punctuation lowered onto an OS without addsPunctuation; caps over-claim");
                return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
            }
        }
        if (lowered->vocabulary_count > 0)
        {
            NSMutableArray<NSString*>* phrases = [NSMutableArray arrayWithCapacity:lowered->vocabulary_count];
            for (u32 i = 0; i < lowered->vocabulary_count; i++)
            {
                str8      v = lowered->vocabulary[i];
                NSString* phrase = [[NSString alloc] initWithBytes:v.data length:(NSUInteger)v.len encoding:NSUTF8StringEncoding];
                if (!phrase)
                {
                    mel_log_error("stt", "apple listen: vocabulary entry %u is not valid utf-8", i);
                    return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
                }
                [phrases addObject:phrase];
            }
            job.request.contextualStrings = phrases;
        }

        if (lowered->feed)
        {
            job.feedFormat = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:(double)lowered->feed_sample_rate channels:1];
            if (!job.feedFormat)
            {
                mel_log_error("stt", "apple listen: feed format rejected (%u Hz mono f32)", lowered->feed_sample_rate);
                return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
            }
        }
        else
        {
            Mel_Stt_Status st = apple_engine_begin(job, lowered);
            if (mel_stt_failed(st))
                return st;
        }

        apple_job_put(token, job);

        u64 jtoken = token;
        job.task = [recognizer recognitionTaskWithRequest:job.request
                                            resultHandler:^(SFSpeechRecognitionResult* result, NSError* error) {
                                                if (result)
                                                {
                                                    @autoreleasepool
                                                    {
                                                        NSString*   str = result.bestTranscription.formattedString;
                                                        const char* utf8 = str.UTF8String;
                                                        f32         confidence = 0.0f;
                                                        if (result.isFinal)
                                                        {
                                                            NSArray<SFTranscriptionSegment*>* segments = result.bestTranscription.segments;
                                                            if (segments.count > 0)
                                                            {
                                                                f32 sum = 0.0f;
                                                                for (SFTranscriptionSegment* seg in segments)
                                                                    sum += (f32)seg.confidence;
                                                                confidence = sum / (f32)segments.count;
                                                            }
                                                        }
                                                        Mel_Stt_Result res = {
                                                            .text = (str8){ (u8*)utf8, utf8 ? (size)strlen(utf8) : 0 },
                                                            .final = result.isFinal ? true : false,
                                                            .confidence = confidence,
                                                        };
                                                        Mel_Stt_Apple_Job* live = apple_job_get(jtoken);
                                                        if (live && live.sink.on_result)
                                                            live.sink.on_result(live.sink.token, &res);
                                                    }
                                                    if (result.isFinal)
                                                    {
                                                        apple_job_finish(jtoken, MEL_STT_OK);
                                                        return;
                                                    }
                                                }
                                                if (error)
                                                    apple_job_finish(jtoken, MEL_STT_ERROR | MEL_STT_RESULT_NETWORK);
                                            }];
        return MEL_STT_OK;
    }
}

static void apple_stop(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    @autoreleasepool
    {
        Mel_Stt_Apple_Job* job = apple_job_get(token);
        if (!job)
            return;
        if (job.engine)
        {
            [job.engine.inputNode removeTapOnBus:0];
            [job.engine stop];
        }
        [job.request endAudio];
    }
}

static void apple_abort(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    @autoreleasepool
    {
        Mel_Stt_Apple_Job* job = apple_job_take(token);
        if (!job)
            return;
        [job.task cancel];
        apple_job_teardown(job);
    }
}

static Mel_Stt_Status apple_feed(void* user, u64 stable_id, u64 token, const f32* frames, u32 frame_count)
{
    (void)user;
    (void)stable_id;
    @autoreleasepool
    {
        Mel_Stt_Apple_Job* job = apple_job_get(token);
        if (!job)
        {
            mel_log_error("stt", "apple feed: no live job for token %llu", (unsigned long long)token);
            return MEL_STT_ERROR | MEL_STT_RESULT_LOST;
        }
        if (!job.feedFormat)
        {
            mel_log_error("stt", "apple feed: session was not opened through the fed door; provider bug");
            return MEL_STT_ERROR | MEL_STT_RESULT_UNSUPPORTED;
        }
        if (frame_count == 0)
            return MEL_STT_OK;
        AVAudioPCMBuffer* buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:job.feedFormat frameCapacity:frame_count];
        if (!buffer || buffer.floatChannelData == NULL)
        {
            mel_log_error("stt", "apple feed: PCM buffer allocation failed for %u frames", frame_count);
            return MEL_STT_ERROR | MEL_STT_RESULT_AUDIO;
        }
        memcpy(buffer.floatChannelData[0], frames, sizeof(f32) * frame_count);
        buffer.frameLength = frame_count;
        [job.request appendAudioPCMBuffer:buffer];
        return MEL_STT_OK;
    }
}

static void* apple_recognizer_native(void* user, u64 stable_id)
{
    (void)user;
    return (__bridge void*)g_apple_stt_recognizers[@(stable_id)];
}

static void apple_shutdown(void* user, const Mel_Alloc* alloc)
{
    (void)user;
    (void)alloc;
    @autoreleasepool
    {
        os_unfair_lock_lock(&g_apple_stt_lock);
        NSArray<Mel_Stt_Apple_Job*>* jobs = g_apple_stt_jobs.allValues;
        [g_apple_stt_jobs removeAllObjects];
        g_apple_stt_jobs = nil;
        os_unfair_lock_unlock(&g_apple_stt_lock);
        for (Mel_Stt_Apple_Job* job in jobs)
        {
            [job.task cancel];
            apple_job_teardown(job);
        }
        g_apple_stt_recognizers = nil;
    }
    if (g_apple_stt.strings.allocator != NULL)
    {
        apple_strings_clear();
        mel_array_free(&g_apple_stt.strings);
        memset(&g_apple_stt.strings, 0, sizeof g_apple_stt.strings);
    }
    g_apple_stt.alloc = NULL;
}

void mel_stt__register_host_providers(void)
{
    static const Mel_Stt_Provider_Desc desc = {
        .name = "apple-sfspeech",
        .enumerate_recognizers = apple_enumerate_recognizers,
        .authorization = apple_authorization,
        .authorize = apple_authorize,
        .listen = apple_listen,
        .stop = apple_stop,
        .abort = apple_abort,
        .feed = apple_feed,
        .recognizer_native = apple_recognizer_native,
        .shutdown = apple_shutdown,
    };
    mel_stt_provider_register(&desc);
}
