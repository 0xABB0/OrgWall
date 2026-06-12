#include <tts/provider.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <string.h>

#import <TargetConditionals.h>
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <os/lock.h>

typedef Mel_Array(str8) Apple_Strings;
typedef Mel_Array(f32) Apple_Frames;

@interface                                        Mel_Tts_Apple_Speak_Job: NSObject <AVSpeechSynthesizerDelegate>
@property(nonatomic, strong) AVSpeechSynthesizer* synth;
@property(nonatomic, strong) NSString*            text;
@property(nonatomic, assign) u64                  token;
@property(nonatomic, assign) Mel_Tts_Sink         sink;
@property(nonatomic, assign) BOOL                 wantRanges;
@end

@interface Mel_Tts_Apple_Render_Job: NSObject <AVSpeechSynthesizerDelegate>
{
@public
    Apple_Frames frames;
    u32          channels;
    u32          sample_rate;
    bool         format_bad;
}
@property(nonatomic, strong) AVSpeechSynthesizer* synth;
@property(nonatomic, assign) u64                  token;
@property(nonatomic, assign) Mel_Tts_Sink         sink;
@end

static struct
{
    const Mel_Alloc* alloc;
    Apple_Strings    strings;
} g_tts;

static os_unfair_lock                                             g_lock = OS_UNFAIR_LOCK_INIT;
static NSMutableDictionary<NSNumber*, Mel_Tts_Apple_Speak_Job*>*  g_speak_jobs;
static NSMutableDictionary<NSNumber*, Mel_Tts_Apple_Render_Job*>* g_render_jobs;

static Mel_Tts_Apple_Speak_Job* apple_speak_job_take(u64 token)
{
    Mel_Tts_Apple_Speak_Job* job = nil;
    os_unfair_lock_lock(&g_lock);
    if (g_speak_jobs != nil)
    {
        job = g_speak_jobs[@(token)];
        if (job)
            [g_speak_jobs removeObjectForKey:@(token)];
    }
    os_unfair_lock_unlock(&g_lock);
    return job;
}

static Mel_Tts_Apple_Speak_Job* apple_speak_job_peek(u64 token)
{
    os_unfair_lock_lock(&g_lock);
    Mel_Tts_Apple_Speak_Job* job = g_speak_jobs != nil ? g_speak_jobs[@(token)] : nil;
    os_unfair_lock_unlock(&g_lock);
    return job;
}

static void apple_speak_job_put(u64 token, Mel_Tts_Apple_Speak_Job* job)
{
    os_unfair_lock_lock(&g_lock);
    if (g_speak_jobs == nil)
        g_speak_jobs = [NSMutableDictionary dictionary];
    g_speak_jobs[@(token)] = job;
    os_unfair_lock_unlock(&g_lock);
}

static void apple_render_job_put(u64 token, Mel_Tts_Apple_Render_Job* job)
{
    os_unfair_lock_lock(&g_lock);
    if (g_render_jobs == nil)
        g_render_jobs = [NSMutableDictionary dictionary];
    g_render_jobs[@(token)] = job;
    os_unfair_lock_unlock(&g_lock);
}

static Mel_Tts_Apple_Render_Job* apple_render_job_peek(u64 token)
{
    os_unfair_lock_lock(&g_lock);
    Mel_Tts_Apple_Render_Job* job = g_render_jobs != nil ? g_render_jobs[@(token)] : nil;
    os_unfair_lock_unlock(&g_lock);
    return job;
}

@implementation Mel_Tts_Apple_Speak_Job

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer didFinishSpeechUtterance:(AVSpeechUtterance*)utterance
{
    MEL_UNUSED(synthesizer);
    MEL_UNUSED(utterance);
    Mel_Tts_Apple_Speak_Job* job = apple_speak_job_take(self.token);
    if (job == nil)
        return;
    Mel_Tts_Sink sink = job.sink;
    if (sink.on_done)
        sink.on_done(sink.token, MEL_TTS_OK);
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer didCancelSpeechUtterance:(AVSpeechUtterance*)utterance
{
    MEL_UNUSED(synthesizer);
    MEL_UNUSED(utterance);
    Mel_Tts_Apple_Speak_Job* job = apple_speak_job_take(self.token);
    if (job == nil)
        return;
    Mel_Tts_Sink sink = job.sink;
    if (sink.on_done)
        sink.on_done(sink.token, MEL_TTS_OK | MEL_TTS_RESULT_CANCELLED);
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer willSpeakRangeOfSpeechString:(NSRange)characterRange utterance:(AVSpeechUtterance*)utterance
{
    MEL_UNUSED(synthesizer);
    MEL_UNUSED(utterance);
    if (!self.wantRanges || self.sink.on_range == NULL)
        return;
    @autoreleasepool
    {
        NSUInteger end = characterRange.location + characterRange.length;
        if (end > self.text.length)
            return;
        usize offset = (usize)[[self.text substringToIndex:characterRange.location] lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
        usize length = (usize)[[self.text substringWithRange:characterRange] lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
        self.sink.on_range(self.sink.token, (Mel_Tts_Range){ .offset = offset, .length = length });
    }
}

@end

static void apple_render_finish(u64 token, Mel_Tts_Status status)
{
    Mel_Tts_Apple_Render_Job* job = nil;
    os_unfair_lock_lock(&g_lock);
    if (g_render_jobs != nil)
    {
        job = g_render_jobs[@(token)];
        if (job)
            [g_render_jobs removeObjectForKey:@(token)];
    }
    os_unfair_lock_unlock(&g_lock);
    if (job == nil)
        return;
    Mel_Tts_Sink sink = job.sink;
    if (sink.on_render)
    {
        bool ok = (status & MEL_TTS_SEVERITY_MASK) == MEL_TTS_OK && (status & (MEL_TTS_RESULT_ABORTED | MEL_TTS_RESULT_CANCELLED)) == 0;
        if (ok && job->format_bad)
        {
            mel_log_error("tts", "apple render: unsupported pcm format from synthesizer");
            sink.on_render(sink.token, NULL, MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO);
        }
        else if (ok && job->frames.count == 0)
        {
            mel_log_error("tts", "apple render: synthesizer produced no audio");
            sink.on_render(sink.token, NULL, MEL_TTS_ERROR | MEL_TTS_RESULT_AUDIO);
        }
        else if (ok)
        {
            Mel_Tts_Render pcm = {
                .frames = job->frames.items,
                .frame_count = (u32)(job->frames.count / job->channels),
                .sample_rate = job->sample_rate,
                .channels = job->channels,
            };
            sink.on_render(sink.token, &pcm, status);
        }
        else
            sink.on_render(sink.token, NULL, status);
    }
    mel_array_free(&job->frames);
}

@implementation Mel_Tts_Apple_Render_Job

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer didFinishSpeechUtterance:(AVSpeechUtterance*)utterance
{
    MEL_UNUSED(synthesizer);
    MEL_UNUSED(utterance);
    apple_render_finish(self.token, MEL_TTS_OK);
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer didCancelSpeechUtterance:(AVSpeechUtterance*)utterance
{
    MEL_UNUSED(synthesizer);
    MEL_UNUSED(utterance);
    apple_render_finish(self.token, MEL_TTS_OK | MEL_TTS_RESULT_ABORTED);
}

@end

static void apple_strings_clear(Apple_Strings* strings, const Mel_Alloc* alloc)
{
    for (usize i = 0; i < strings->count; i++)
        if (strings->items[i].data)
            mel_dealloc(alloc, strings->items[i].data);
    mel_array_clear(strings);
}

static str8 apple_intern(Apple_Strings* strings, const char* utf8)
{
    usize len = utf8 ? strlen(utf8) : 0;
    u8*   data = (u8*)mel_alloc(g_tts.alloc, len + 1);
    if (!data)
        return STR8_EMPTY;
    if (len)
        memcpy(data, utf8, len);
    data[len] = 0;
    str8 s = { data, (size)len };
    mel_array_push(strings, s);
    return s;
}

static u64 apple_voice_stable_id(AVSpeechSynthesisVoice* voice) { return (u64)[voice.identifier hash]; }

static AVSpeechSynthesisVoice* apple_voice_for(u64 stable_id)
{
    for (AVSpeechSynthesisVoice* voice in [AVSpeechSynthesisVoice speechVoices])
        if (apple_voice_stable_id(voice) == stable_id)
            return voice;
    return nil;
}

static bool apple_render_available(void)
{
    if (__builtin_available(macOS 10.15, iOS 13.0, *))
        return true;
    return false;
}

static u32 apple_enumerate_voices(void* user, const Mel_Alloc* alloc, Mel_Tts_Voice_Raw* out, u32 cap)
{
    MEL_UNUSED(user);
    g_tts.alloc = alloc;
    if (g_tts.strings.allocator == NULL)
        mel_array_init(&g_tts.strings, alloc);
    apple_strings_clear(&g_tts.strings, alloc);
    @autoreleasepool
    {
        NSArray<AVSpeechSynthesisVoice*>* voices = [AVSpeechSynthesisVoice speechVoices];
        u32                               total = (u32)voices.count;
        u32                               n = total < cap ? total : cap;
        bool                              render = apple_render_available();
        for (u32 i = 0; i < n; i++)
        {
            AVSpeechSynthesisVoice* voice = voices[i];
            out[i].stable_id = apple_voice_stable_id(voice);
            out[i].name = apple_intern(&g_tts.strings, voice.name.UTF8String);
            out[i].language = apple_intern(&g_tts.strings, voice.language.UTF8String);
            out[i].viseme_set = STR8_EMPTY;
            out[i].caps = (Mel_Tts_Voice_Caps){
                .rate = true,
                .rate_min = AVSpeechUtteranceMinimumSpeechRate / AVSpeechUtteranceDefaultSpeechRate,
                .rate_max = AVSpeechUtteranceMaximumSpeechRate / AVSpeechUtteranceDefaultSpeechRate,
                .pitch = true,
                .volume = true,
                .ranges = true,
                .can_pause = true,
                .render = render,
                .ssml = false,
                .visemes = false,
            };
        }
        return total;
    }
}

static AVSpeechUtterance* apple_utterance_build(AVSpeechSynthesisVoice* voice, NSString* text, const Mel_Tts_Lowered* lowered)
{
    AVSpeechUtterance* utterance = [AVSpeechUtterance speechUtteranceWithString:text];
    utterance.voice = voice;
    if (lowered->rate > 0.0f)
    {
        float rate = AVSpeechUtteranceDefaultSpeechRate * lowered->rate;
        if (rate < AVSpeechUtteranceMinimumSpeechRate)
            rate = AVSpeechUtteranceMinimumSpeechRate;
        if (rate > AVSpeechUtteranceMaximumSpeechRate)
            rate = AVSpeechUtteranceMaximumSpeechRate;
        utterance.rate = rate;
    }
    if (lowered->pitch > 0.0f)
    {
        float pitch = lowered->pitch;
        if (pitch < 0.5f || pitch > 2.0f)
        {
            mel_log_warn("tts", "apple: pitch %.2f clamped to [0.5, 2.0]", (double)pitch);
            pitch = pitch < 0.5f ? 0.5f : 2.0f;
        }
        utterance.pitchMultiplier = pitch;
    }
    if (lowered->volume > 0.0f)
    {
        float volume = lowered->volume;
        if (volume > 1.0f)
        {
            mel_log_warn("tts", "apple: volume %.2f clamped to 1.0", (double)volume);
            volume = 1.0f;
        }
        utterance.volume = volume;
    }
    return utterance;
}

static Mel_Tts_Status apple_speak(void* user, u64 stable_id, u64 token, const Mel_Tts_Lowered* lowered, Mel_Tts_Sink sink)
{
    MEL_UNUSED(user);
    @autoreleasepool
    {
        AVSpeechSynthesisVoice* voice = apple_voice_for(stable_id);
        if (!voice)
        {
            mel_log_error("tts", "apple speak: voice %llu not found", (unsigned long long)stable_id);
            return MEL_TTS_ERROR | MEL_TTS_RESULT_LOST;
        }
        NSString* text = [[NSString alloc] initWithBytes:lowered->text.data length:(NSUInteger)lowered->text.len encoding:NSUTF8StringEncoding];
        if (!text)
        {
            mel_log_error("tts", "apple speak: text is not valid utf-8");
            return MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
        }

        AVSpeechUtterance* utterance = apple_utterance_build(voice, text, lowered);

        Mel_Tts_Apple_Speak_Job* job = [[Mel_Tts_Apple_Speak_Job alloc] init];
        job.token = token;
        job.sink = sink;
        job.wantRanges = lowered->want_ranges ? YES : NO;
        job.text = text;
        job.synth = [[AVSpeechSynthesizer alloc] init];
        job.synth.delegate = job;
        apple_speak_job_put(token, job);
        [job.synth speakUtterance:utterance];
        return MEL_TTS_OK;
    }
}

static void apple_render_accumulate(Mel_Tts_Apple_Render_Job* job, AVAudioPCMBuffer* pcm)
{
    if (job->format_bad)
        return;
    AVAudioFormat* fmt = pcm.format;
    u32            ch = (u32)fmt.channelCount;
    u32            sr = (u32)fmt.sampleRate;
    u32            n = (u32)pcm.frameLength;
    if (ch == 0 || n == 0)
        return;
    if (job->channels == 0)
    {
        job->channels = ch;
        job->sample_rate = sr;
    }
    else if (job->channels != ch || job->sample_rate != sr)
    {
        mel_log_error("tts", "apple render: pcm format changed mid-stream (%u ch @ %u Hz -> %u ch @ %u Hz)", job->channels, job->sample_rate, ch, sr);
        job->format_bad = true;
        return;
    }
    mel_array_reserve(&job->frames, job->frames.count + (usize)n * ch);
    bool packed = fmt.interleaved || ch == 1;
    if (fmt.commonFormat == AVAudioPCMFormatFloat32 && pcm.floatChannelData != NULL)
    {
        float* const* data = pcm.floatChannelData;
        if (packed)
            for (u32 i = 0; i < n * ch; i++)
                mel_array_push(&job->frames, data[0][i]);
        else
            for (u32 f = 0; f < n; f++)
                for (u32 c = 0; c < ch; c++)
                    mel_array_push(&job->frames, data[c][f]);
    }
    else if (fmt.commonFormat == AVAudioPCMFormatInt16 && pcm.int16ChannelData != NULL)
    {
        int16_t* const* data = pcm.int16ChannelData;
        if (packed)
            for (u32 i = 0; i < n * ch; i++)
                mel_array_push(&job->frames, (f32)data[0][i] / 32768.0f);
        else
            for (u32 f = 0; f < n; f++)
                for (u32 c = 0; c < ch; c++)
                    mel_array_push(&job->frames, (f32)data[c][f] / 32768.0f);
    }
    else if (fmt.commonFormat == AVAudioPCMFormatInt32 && pcm.int32ChannelData != NULL)
    {
        int32_t* const* data = pcm.int32ChannelData;
        if (packed)
            for (u32 i = 0; i < n * ch; i++)
                mel_array_push(&job->frames, (f32)((f64)data[0][i] / 2147483648.0));
        else
            for (u32 f = 0; f < n; f++)
                for (u32 c = 0; c < ch; c++)
                    mel_array_push(&job->frames, (f32)((f64)data[c][f] / 2147483648.0));
    }
    else
    {
        mel_log_error("tts", "apple render: unsupported common format %ld", (long)fmt.commonFormat);
        job->format_bad = true;
    }
}

static Mel_Tts_Status apple_render(void* user, u64 stable_id, u64 token, const Mel_Tts_Lowered* lowered, Mel_Tts_Sink sink)
{
    MEL_UNUSED(user);
    if (g_tts.alloc == NULL)
    {
        mel_log_error("tts", "apple render: no allocator; render before enumerate");
        return MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
    }
    @autoreleasepool
    {
        AVSpeechSynthesisVoice* voice = apple_voice_for(stable_id);
        if (!voice)
        {
            mel_log_error("tts", "apple render: voice %llu not found", (unsigned long long)stable_id);
            return MEL_TTS_ERROR | MEL_TTS_RESULT_LOST;
        }
        NSString* text = [[NSString alloc] initWithBytes:lowered->text.data length:(NSUInteger)lowered->text.len encoding:NSUTF8StringEncoding];
        if (!text)
        {
            mel_log_error("tts", "apple render: text is not valid utf-8");
            return MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
        }

        if (__builtin_available(macOS 10.15, iOS 13.0, *))
        {
            AVSpeechUtterance* utterance = apple_utterance_build(voice, text, lowered);

            Mel_Tts_Apple_Render_Job* job = [[Mel_Tts_Apple_Render_Job alloc] init];
            job.token = token;
            job.sink = sink;
            mel_array_init(&job->frames, g_tts.alloc);
            job.synth = [[AVSpeechSynthesizer alloc] init];
            job.synth.delegate = job;
            apple_render_job_put(token, job);

            u64 jtoken = token;
            [job.synth writeUtterance:utterance
                     toBufferCallback:^(AVAudioBuffer* buffer) {
                         @autoreleasepool
                         {
                             AVAudioPCMBuffer* pcm = [buffer isKindOfClass:[AVAudioPCMBuffer class]] ? (AVAudioPCMBuffer*)buffer : nil;
                             if (pcm != nil && pcm.frameLength > 0)
                             {
                                 os_unfair_lock_lock(&g_lock);
                                 Mel_Tts_Apple_Render_Job* j = g_render_jobs != nil ? g_render_jobs[@(jtoken)] : nil;
                                 if (j)
                                     apple_render_accumulate(j, pcm);
                                 os_unfair_lock_unlock(&g_lock);
                                 return;
                             }
                             apple_render_finish(jtoken, MEL_TTS_OK);
                         }
                     }];
            return MEL_TTS_OK;
        }
        mel_log_error("tts", "apple render: write api unavailable on this os");
        return MEL_TTS_ERROR | MEL_TTS_RESULT_UNSUPPORTED;
    }
}

static void apple_pause(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    Mel_Tts_Apple_Speak_Job* job = apple_speak_job_peek(token);
    if (job)
        [job.synth pauseSpeakingAtBoundary:AVSpeechBoundaryImmediate];
}

static void apple_resume(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    Mel_Tts_Apple_Speak_Job* job = apple_speak_job_peek(token);
    if (job)
        [job.synth continueSpeaking];
}

static void apple_abort(void* user, u64 stable_id, u64 token)
{
    MEL_UNUSED(user);
    MEL_UNUSED(stable_id);
    Mel_Tts_Apple_Speak_Job* speak_job = apple_speak_job_peek(token);
    if (speak_job)
    {
        [speak_job.synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
        return;
    }
    Mel_Tts_Apple_Render_Job* render_job = apple_render_job_peek(token);
    if (render_job)
        [render_job.synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
}

static void* apple_voice_native(void* user, u64 stable_id)
{
    MEL_UNUSED(user);
    return (__bridge void*)apple_voice_for(stable_id);
}

static void apple_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    @autoreleasepool
    {
        os_unfair_lock_lock(&g_lock);
        NSMutableDictionary<NSNumber*, Mel_Tts_Apple_Speak_Job*>*  speak = g_speak_jobs;
        NSMutableDictionary<NSNumber*, Mel_Tts_Apple_Render_Job*>* render = g_render_jobs;
        g_speak_jobs = nil;
        g_render_jobs = nil;
        os_unfair_lock_unlock(&g_lock);
        for (NSNumber* key in speak)
            [speak[key].synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
        for (NSNumber* key in render)
        {
            Mel_Tts_Apple_Render_Job* job = render[key];
            [job.synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
            mel_array_free(&job->frames);
        }
    }
    if (g_tts.strings.allocator != NULL)
    {
        apple_strings_clear(&g_tts.strings, alloc);
        mel_array_free(&g_tts.strings);
        mel_array_init(&g_tts.strings, NULL);
    }
    g_tts.alloc = NULL;
}

void mel_tts__register_host_providers(void)
{
    static const Mel_Tts_Provider_Desc desc = {
        .name = "apple-avspeech",
        .enumerate_voices = apple_enumerate_voices,
        .speak = apple_speak,
        .pause = apple_pause,
        .resume = apple_resume,
        .abort = apple_abort,
        .render = apple_render,
        .voice_native = apple_voice_native,
        .shutdown = apple_shutdown,
    };
    mel_tts_provider_register(&desc);
}
