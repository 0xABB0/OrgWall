#include <speech/provider.h>

#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <string.h>

#import <TargetConditionals.h>
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <Speech/SFSpeechRecognizer.h>
#import <Speech/SFSpeechRecognitionRequest.h>
#import <Speech/SFSpeechRecognitionTask.h>
#import <Speech/SFSpeechRecognitionResult.h>
#import <Speech/SFTranscription.h>
#import <Speech/SFTranscriptionSegment.h>

static void apple_stt_finish(u64 token, Mel_Speech_Status status);

@interface                                        Mel_Speech_TTS_Job: NSObject <AVSpeechSynthesizerDelegate>
@property(nonatomic, strong) AVSpeechSynthesizer* synth;
@property(nonatomic, strong) NSString*            text;
@property(nonatomic, assign) u64                  token;
@property(nonatomic, assign) Mel_Speech_Sink      sink;
@property(nonatomic, assign) BOOL                 wantRanges;
@end

@interface                                                          Mel_Speech_STT_Job: NSObject
@property(nonatomic, strong) SFSpeechRecognizer*                    recognizer;
@property(nonatomic, strong) SFSpeechAudioBufferRecognitionRequest* request;
@property(nonatomic, strong) SFSpeechRecognitionTask*               task;
@property(nonatomic, strong) AVAudioEngine*                         engine;
@property(nonatomic, assign) u64                                    token;
@property(nonatomic, assign) Mel_Speech_Sink                        sink;
@end

static NSMutableDictionary<NSNumber*, Mel_Speech_TTS_Job*>* g_tts_jobs;
static NSMutableDictionary<NSNumber*, Mel_Speech_STT_Job*>* g_stt_jobs;

static NSMutableDictionary<NSNumber*, Mel_Speech_TTS_Job*>* apple_tts_jobs(void)
{
    if (g_tts_jobs == nil)
        g_tts_jobs = [NSMutableDictionary dictionary];
    return g_tts_jobs;
}

static NSMutableDictionary<NSNumber*, Mel_Speech_STT_Job*>* apple_stt_jobs(void)
{
    if (g_stt_jobs == nil)
        g_stt_jobs = [NSMutableDictionary dictionary];
    return g_stt_jobs;
}

static void apple_tts_job_remove(u64 token) { [apple_tts_jobs() removeObjectForKey:@(token)]; }

@implementation Mel_Speech_TTS_Job

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer didFinishSpeechUtterance:(AVSpeechUtterance*)utterance
{
    (void)synthesizer;
    (void)utterance;
    Mel_Speech_Sink sink = self.sink;
    apple_tts_job_remove(self.token);
    if (sink.on_speak_done)
        sink.on_speak_done(sink.token, MEL_SPEECH_OK);
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer didCancelSpeechUtterance:(AVSpeechUtterance*)utterance
{
    (void)synthesizer;
    (void)utterance;
    Mel_Speech_Sink sink = self.sink;
    apple_tts_job_remove(self.token);
    if (sink.on_speak_done)
        sink.on_speak_done(sink.token, MEL_SPEECH_OK | MEL_SPEECH_RESULT_ABORTED);
}

- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer willSpeakRangeOfSpeechString:(NSRange)characterRange utterance:(AVSpeechUtterance*)utterance
{
    (void)synthesizer;
    (void)utterance;
    if (!self.wantRanges || self.sink.on_range == NULL)
        return;
    NSUInteger end = characterRange.location + characterRange.length;
    if (end > self.text.length)
        return;
    usize offset = (usize)[[self.text substringToIndex:characterRange.location] lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    usize length = (usize)[[self.text substringWithRange:characterRange] lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    self.sink.on_range(self.sink.token, (Mel_Speech_Range){ .offset = offset, .length = length });
}

@end

typedef Mel_Array(str8) Apple_Strings;

static struct
{
    const Mel_Alloc* alloc;
    Apple_Strings    voice_strings;
    Apple_Strings    rec_strings;
} g_apple;

static void apple_strings_clear(Apple_Strings* strings)
{
    for (usize i = 0; i < strings->count; i++)
        if (strings->items[i].data)
            mel_dealloc(g_apple.alloc, strings->items[i].data);
    mel_array_clear(strings);
}

static str8 apple_intern(Apple_Strings* strings, const char* utf8)
{
    usize len = utf8 ? strlen(utf8) : 0;
    u8*   data = (u8*)mel_alloc(g_apple.alloc, len + 1);
    if (!data)
        return (str8){ 0 };
    if (len)
        memcpy(data, utf8, len);
    data[len] = 0;
    str8 s = { data, (size)len };
    mel_array_push(strings, s);
    return s;
}

static void apple_strings_prepare(Apple_Strings* strings, const Mel_Alloc* alloc)
{
    g_apple.alloc = alloc;
    if (strings->allocator == NULL)
        mel_array_init(strings, alloc);
    apple_strings_clear(strings);
}

static u64 apple_voice_stable_id(AVSpeechSynthesisVoice* voice) { return (u64)[voice.identifier hash]; }

static AVSpeechSynthesisVoice* apple_voice_for(u64 stable_id)
{
    for (AVSpeechSynthesisVoice* voice in [AVSpeechSynthesisVoice speechVoices])
        if (apple_voice_stable_id(voice) == stable_id)
            return voice;
    return nil;
}

static u64 apple_locale_stable_id(NSLocale* locale) { return (u64)[locale.localeIdentifier hash]; }

static NSLocale* apple_locale_for(u64 stable_id)
{
    for (NSLocale* locale in [SFSpeechRecognizer supportedLocales])
        if (apple_locale_stable_id(locale) == stable_id)
            return locale;
    return nil;
}

static u32 apple_enumerate_voices(void* user, const Mel_Alloc* alloc, Mel_Speech_Voice_Raw* out, u32 cap)
{
    (void)user;
    apple_strings_prepare(&g_apple.voice_strings, alloc);
    @autoreleasepool
    {
        NSArray<AVSpeechSynthesisVoice*>* voices = [AVSpeechSynthesisVoice speechVoices];
        u32                               total = (u32)voices.count;
        u32                               n = total < cap ? total : cap;
        for (u32 i = 0; i < n; i++)
        {
            AVSpeechSynthesisVoice* voice = voices[i];
            out[i].stable_id = apple_voice_stable_id(voice);
            out[i].name = apple_intern(&g_apple.voice_strings, voice.name.UTF8String);
            out[i].language = apple_intern(&g_apple.voice_strings, voice.language.UTF8String);
            out[i].caps = (Mel_Speech_Voice_Caps){
                .rate = true,
                .rate_min = AVSpeechUtteranceMinimumSpeechRate / AVSpeechUtteranceDefaultSpeechRate,
                .rate_max = AVSpeechUtteranceMaximumSpeechRate / AVSpeechUtteranceDefaultSpeechRate,
                .pitch = true,
                .volume = true,
                .ranges = true,
                .can_pause = true,
            };
        }
        return total;
    }
}

static u32 apple_enumerate_recognizers(void* user, const Mel_Alloc* alloc, Mel_Speech_Recognizer_Raw* out, u32 cap)
{
    (void)user;
    apple_strings_prepare(&g_apple.rec_strings, alloc);
    @autoreleasepool
    {
        NSSet<NSLocale*>* locales = [SFSpeechRecognizer supportedLocales];
        u32               total = (u32)locales.count;
        u32               n = 0;
        for (NSLocale* locale in locales)
        {
            if (n >= cap)
                break;
            out[n].stable_id = apple_locale_stable_id(locale);
            out[n].language = apple_intern(&g_apple.rec_strings, locale.localeIdentifier.UTF8String);
            out[n].caps = (Mel_Speech_Recognizer_Caps){
                .on_device = false,
                .partials = true,
                .can_stop = true,
            };
            n++;
        }
        return total;
    }
}

static Mel_Speech_Status apple_speak(void* user, u64 stable_id, u64 token, const Mel_Speech_Speak_Lowered* lowered, Mel_Speech_Sink sink)
{
    (void)user;
    @autoreleasepool
    {
        AVSpeechSynthesisVoice* voice = apple_voice_for(stable_id);
        if (!voice)
        {
            mel_log_error("speech", "apple speak: voice %llu not found", (unsigned long long)stable_id);
            return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NO_DEVICE;
        }
        NSString* text = [[NSString alloc] initWithBytes:lowered->text.data length:(NSUInteger)lowered->text.len encoding:NSUTF8StringEncoding];
        if (!text)
        {
            mel_log_error("speech", "apple speak: text is not valid utf-8");
            return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_UNSUPPORTED;
        }

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
            utterance.pitchMultiplier = lowered->pitch;
        if (lowered->volume > 0.0f)
            utterance.volume = lowered->volume;

        Mel_Speech_TTS_Job* job = [[Mel_Speech_TTS_Job alloc] init];
        job.token = token;
        job.sink = sink;
        job.wantRanges = lowered->want_ranges ? YES : NO;
        job.text = text;
        job.synth = [[AVSpeechSynthesizer alloc] init];
        job.synth.delegate = job;
        apple_tts_jobs()[@(token)] = job;
        [job.synth speakUtterance:utterance];
        return MEL_SPEECH_OK;
    }
}

static void apple_speak_pause(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    Mel_Speech_TTS_Job* job = apple_tts_jobs()[@(token)];
    if (job)
        [job.synth pauseSpeakingAtBoundary:AVSpeechBoundaryImmediate];
}

static void apple_speak_resume(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    Mel_Speech_TTS_Job* job = apple_tts_jobs()[@(token)];
    if (job)
        [job.synth continueSpeaking];
}

static void apple_speak_abort(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    Mel_Speech_TTS_Job* job = apple_tts_jobs()[@(token)];
    if (job)
    {
        [job.synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
        apple_tts_job_remove(token);
    }
}

static const mel_speech_auth* apple_auth_combine(void)
{
    SFSpeechRecognizerAuthorizationStatus speech = [SFSpeechRecognizer authorizationStatus];
    AVAuthorizationStatus                 mic = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (speech == SFSpeechRecognizerAuthorizationStatusDenied || mic == AVAuthorizationStatusDenied)
        return &mel_speech_auth_denied;
    if (speech == SFSpeechRecognizerAuthorizationStatusRestricted || mic == AVAuthorizationStatusRestricted)
        return &mel_speech_auth_restricted;
    if (speech == SFSpeechRecognizerAuthorizationStatusAuthorized && mic == AVAuthorizationStatusAuthorized)
        return &mel_speech_auth_granted;
    return &mel_speech_auth_not_determined;
}

static const mel_speech_auth* apple_authorization(void* user)
{
    (void)user;
    return apple_auth_combine();
}

static void apple_authorize(void* user, Mel_Speech_Sink sink)
{
    (void)user;
    if (sink.on_auth == NULL)
        return;
    [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
        (void)status;
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL granted) {
                                     (void)granted;
                                     sink.on_auth(sink.token, apple_auth_combine());
                                 }];
    }];
}

static void apple_stt_teardown(Mel_Speech_STT_Job* job)
{
    [job.engine.inputNode removeTapOnBus:0];
    [job.engine stop];
}

static void apple_stt_finish(u64 token, Mel_Speech_Status status)
{
    Mel_Speech_STT_Job* job = apple_stt_jobs()[@(token)];
    if (!job)
        return;
    Mel_Speech_Sink sink = job.sink;
    apple_stt_teardown(job);
    [apple_stt_jobs() removeObjectForKey:@(token)];
    if (sink.on_listen_done)
        sink.on_listen_done(sink.token, status);
}

static Mel_Speech_Status apple_listen(void* user, u64 stable_id, u64 token, const Mel_Speech_Listen_Lowered* lowered, Mel_Speech_Sink sink)
{
    (void)user;
    @autoreleasepool
    {
        if (!mel_speech_auth_is_granted(apple_auth_combine()))
        {
            mel_log_error("speech", "apple listen: not authorized; call mel_speech_authorize first");
            return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_DENIED;
        }
        NSLocale* locale = apple_locale_for(stable_id);
        if (!locale)
        {
            mel_log_error("speech", "apple listen: recognizer %llu not found", (unsigned long long)stable_id);
            return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NO_DEVICE;
        }
        SFSpeechRecognizer* recognizer = [[SFSpeechRecognizer alloc] initWithLocale:locale];
        if (!recognizer || !recognizer.isAvailable)
        {
            mel_log_error("speech", "apple listen: recognizer for %s unavailable", locale.localeIdentifier.UTF8String);
            return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NETWORK;
        }

        Mel_Speech_STT_Job* job = [[Mel_Speech_STT_Job alloc] init];
        job.token = token;
        job.sink = sink;
        job.recognizer = recognizer;
        job.request = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
        job.request.shouldReportPartialResults = lowered->partials ? YES : NO;
        job.engine = [[AVAudioEngine alloc] init];

        AVAudioInputNode* input = job.engine.inputNode;
        AVAudioFormat*    format = [input outputFormatForBus:0];
        if (format.channelCount == 0)
        {
            mel_log_error("speech", "apple listen: no audio input route");
            return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_AUDIO;
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
            mel_log_error("speech", "apple listen: audio engine start failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
            return MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_AUDIO;
        }

        Mel_Speech_Sink jsink = sink;
        u64             jtoken = token;
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
                                                        Mel_Speech_Result res = {
                                                            .text = (str8){ (u8*)utf8, utf8 ? (size)strlen(utf8) : 0 },
                                                            .final = result.isFinal ? true : false,
                                                            .confidence = confidence,
                                                        };
                                                        if (jsink.on_result)
                                                            jsink.on_result(jsink.token, &res);
                                                    }
                                                    if (result.isFinal)
                                                    {
                                                        apple_stt_finish(jtoken, MEL_SPEECH_OK);
                                                        return;
                                                    }
                                                }
                                                if (error)
                                                    apple_stt_finish(jtoken, MEL_SPEECH_ERROR | MEL_SPEECH_RESULT_NETWORK);
                                            }];
        apple_stt_jobs()[@(token)] = job;
        return MEL_SPEECH_OK;
    }
}

static void apple_listen_stop(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    Mel_Speech_STT_Job* job = apple_stt_jobs()[@(token)];
    if (!job)
        return;
    [job.engine.inputNode removeTapOnBus:0];
    [job.engine stop];
    [job.request endAudio];
}

static void apple_listen_abort(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    Mel_Speech_STT_Job* job = apple_stt_jobs()[@(token)];
    if (!job)
        return;
    [job.task cancel];
    apple_stt_teardown(job);
    [apple_stt_jobs() removeObjectForKey:@(token)];
}

static void* apple_voice_native(void* user, u64 stable_id)
{
    (void)user;
    return (__bridge void*)apple_voice_for(stable_id);
}

static void apple_shutdown(void* user, const Mel_Alloc* alloc)
{
    (void)user;
    (void)alloc;
    @autoreleasepool
    {
        if (g_tts_jobs != nil)
        {
            for (NSNumber* key in [g_tts_jobs allKeys])
                [g_tts_jobs[key].synth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
            [g_tts_jobs removeAllObjects];
            g_tts_jobs = nil;
        }
        if (g_stt_jobs != nil)
        {
            for (NSNumber* key in [g_stt_jobs allKeys])
            {
                Mel_Speech_STT_Job* job = g_stt_jobs[key];
                [job.task cancel];
                apple_stt_teardown(job);
            }
            [g_stt_jobs removeAllObjects];
            g_stt_jobs = nil;
        }
    }
    if (g_apple.voice_strings.allocator != NULL)
    {
        apple_strings_clear(&g_apple.voice_strings);
        mel_array_free(&g_apple.voice_strings);
        mel_array_init(&g_apple.voice_strings, NULL);
    }
    if (g_apple.rec_strings.allocator != NULL)
    {
        apple_strings_clear(&g_apple.rec_strings);
        mel_array_free(&g_apple.rec_strings);
        mel_array_init(&g_apple.rec_strings, NULL);
    }
    g_apple.alloc = NULL;
}

void mel_speech__register_host_providers(void)
{
    static const Mel_Speech_Provider_Desc desc = {
        .name = "apple-speech",
        .enumerate_voices = apple_enumerate_voices,
        .enumerate_recognizers = apple_enumerate_recognizers,
        .speak = apple_speak,
        .speak_pause = apple_speak_pause,
        .speak_resume = apple_speak_resume,
        .speak_abort = apple_speak_abort,
        .authorization = apple_authorization,
        .authorize = apple_authorize,
        .listen = apple_listen,
        .listen_stop = apple_listen_stop,
        .listen_abort = apple_listen_abort,
        .voice_native = apple_voice_native,
        .shutdown = apple_shutdown,
    };
    mel_speech_provider_register(&desc);
}
