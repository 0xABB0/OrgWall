#include <audioout/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#define MEL_AUDIOOUT_IOS_QUANTUM_FRAMES 1024u

@interface MelAudioOutIosVolumeObserver: NSObject
@end

typedef struct
{
    void*                token;
    Mel_AudioOut_Pull_Fn pull;
    void (*on_lost)(void* token);
    str8 stable_id;
    bool started;
    bool lost;
} Ios_Open;

typedef struct
{
    u32      count;
    Ios_Open opens[];
} Open_List;

typedef struct
{
    const Mel_Alloc*      alloc;
    Mel_AudioOut_Provider provider;
    Mel_Array(str8) interned;
    str8           default_storage;
    _Atomic(void*) opens;
    Mel_Array(void*) garbage;
    f32* scratch;
    u32  scratch_frames;
    u32  channels;
    u32  samplerate;
    bool render_complained;
} Ios_State;

static Ios_State       g_ios;
static pthread_mutex_t g_ios_lock = PTHREAD_MUTEX_INITIALIZER;

static AVAudioEngine*                           g_ios_engine;
static AVAudioSourceNode*                       g_ios_source;
static NSArray<AVAudioSessionPortDescription*>* g_ios_ports;
static id                                       g_ios_route_observer;
static id                                       g_ios_config_observer;
static MelAudioOutIosVolumeObserver*            g_ios_volume_observer;

static const char* ios_nscstr(NSString* s) { return s != nil && s.UTF8String != NULL ? s.UTF8String : ""; }

static const char* ios_errstr(NSError* err) { return err != nil ? ios_nscstr(err.localizedDescription) : "unknown"; }

@implementation MelAudioOutIosVolumeObserver
- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object change:(NSDictionary<NSKeyValueChangeKey, id>*)change context:(void*)context
{
    MEL_UNUSED(keyPath);
    MEL_UNUSED(object);
    MEL_UNUSED(change);
    MEL_UNUSED(context);
    mel_audioout_provider_notify(g_ios.provider);
}
@end

static void ios_intern_clear(void)
{
    for (usize i = 0; i < g_ios.interned.count; i++)
        if (g_ios.interned.items[i].data != NULL)
            mel_dealloc(g_ios.alloc, g_ios.interned.items[i].data);
    mel_array_clear(&g_ios.interned);
}

static str8 ios_intern_cstr(const char* utf8)
{
    str8 s = str8_dup(str8_from_cstr(utf8 != NULL ? utf8 : ""), g_ios.alloc);
    mel_array_push(&g_ios.interned, s);
    return s;
}

static str8 ios_intern_id(NSString* uid)
{
    str8 s = str8_fmt(g_ios.alloc, "avsession:%s", ios_nscstr(uid));
    mel_array_push(&g_ios.interned, s);
    return s;
}

static const mel_audioout_kind* ios_kind_for(NSString* port_type)
{
    if ([port_type isEqualToString:AVAudioSessionPortBuiltInSpeaker] || [port_type isEqualToString:AVAudioSessionPortBuiltInReceiver] || [port_type isEqualToString:AVAudioSessionPortHeadphones])
        return &mel_audioout_builtin;
    if ([port_type isEqualToString:AVAudioSessionPortHDMI])
        return &mel_audioout_hdmi;
    if ([port_type isEqualToString:AVAudioSessionPortAirPlay])
        return &mel_audioout_virtual;
    if ([port_type isEqualToString:AVAudioSessionPortBluetoothA2DP] || [port_type isEqualToString:AVAudioSessionPortBluetoothHFP] || [port_type isEqualToString:AVAudioSessionPortBluetoothLE])
        return &mel_audioout_bluetooth;
    if ([port_type isEqualToString:AVAudioSessionPortUSBAudio])
        return &mel_audioout_usb;
    return &mel_audioout_unknown;
}

static bool ios_id_is_uid(str8 stable_id, NSString* uid)
{
    static const char prefix[] = "avsession:";
    const usize       plen = sizeof prefix - 1;
    if (uid == nil)
        return false;
    const char* u = ios_nscstr(uid);
    usize       ulen = strlen(u);
    return (usize)stable_id.len == plen + ulen && memcmp(stable_id.data, prefix, plen) == 0 && memcmp(stable_id.data + plen, u, ulen) == 0;
}

static AVAudioSessionPortDescription* ios_route_port_for(str8 stable_id)
{
    for (AVAudioSessionPortDescription* port in [AVAudioSession sharedInstance].currentRoute.outputs)
        if (ios_id_is_uid(stable_id, port.UID))
            return port;
    return nil;
}

static void ios_enumerate(void* user, Mel_AudioOut_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    assert(g_ios.alloc != NULL);
    @autoreleasepool
    {
        AVAudioSession*                          session = [AVAudioSession sharedInstance];
        NSArray<AVAudioSessionPortDescription*>* outputs = session.currentRoute.outputs;
        g_ios_ports = outputs;
        ios_intern_clear();
        if (outputs == nil || outputs.count == 0)
        {
            mel_log_warn("audioout", "ios: AVAudioSession current route has no outputs (category %s)", ios_nscstr(session.category));
            return;
        }
        u32 rate = (u32)(session.sampleRate + 0.5);
        if (rate == 0)
            mel_log_warn("audioout", "ios: AVAudioSession reports zero sample rate; descriptors will carry 0 Hz");
        f32 volume = (f32)session.outputVolume;
        for (AVAudioSessionPortDescription* port in outputs)
        {
            u32              channels = (u32)port.channels.count;
            Mel_AudioOut_Raw raw = {
                .stable_id = ios_intern_id(port.UID),
                .name = ios_intern_cstr(port.portName.UTF8String),
                .kind = ios_kind_for(port.portType),
                .channels = channels > 0 ? channels : 2u,
                .samplerate = rate,
                .samplerates = &rate,
                .samplerate_count = 1,
                .caps = { .volume = false, .mute = false },
                .volume = volume,
                .muted = false,
            };
            if (!fn(&raw, fn_user))
                return;
        }
    }
}

static str8 ios_default_id(void* user)
{
    MEL_UNUSED(user);
    @autoreleasepool
    {
        NSString* uid = [AVAudioSession sharedInstance].currentRoute.outputs.firstObject.UID;
        if (g_ios.default_storage.data != NULL)
        {
            mel_dealloc(g_ios.alloc, g_ios.default_storage.data);
            g_ios.default_storage = STR8_EMPTY;
        }
        if (uid == nil)
            return STR8_EMPTY;
        g_ios.default_storage = str8_fmt(g_ios.alloc, "avsession:%s", ios_nscstr(uid));
        return g_ios.default_storage;
    }
}

static void ios_opens_swap(Open_List* nl)
{
    void* old = atomic_exchange_explicit(&g_ios.opens, nl, memory_order_acq_rel);
    if (old != NULL)
        mel_array_push(&g_ios.garbage, old);
}

static Open_List* ios_opens_clone(u32 extra)
{
    Open_List* cur = atomic_load_explicit(&g_ios.opens, memory_order_acquire);
    u32        count = cur != NULL ? cur->count : 0;
    Open_List* nl = mel_alloc(g_ios.alloc, sizeof(Open_List) + sizeof(Ios_Open) * ((usize)count + extra));
    if (nl == NULL)
        return NULL;
    for (u32 i = 0; i < count; i++)
        nl->opens[i] = cur->opens[i];
    nl->count = count;
    return nl;
}

static u32 ios_started_count(void)
{
    Open_List* ol = atomic_load_explicit(&g_ios.opens, memory_order_acquire);
    u32        n = 0;
    if (ol != NULL)
        for (u32 i = 0; i < ol->count; i++)
            if (ol->opens[i].started)
                n++;
    return n;
}

static OSStatus ios_render(BOOL* is_silence, AVAudioFrameCount frame_count, AudioBufferList* abl)
{
    for (UInt32 b = 0; b < abl->mNumberBuffers; b++)
        memset(abl->mBuffers[b].mData, 0, abl->mBuffers[b].mDataByteSize);
    *is_silence = YES;
    Open_List* ol = atomic_load_explicit(&g_ios.opens, memory_order_acquire);
    if (ol == NULL || ol->count == 0 || g_ios.scratch == NULL)
        return noErr;
    u32  channels = g_ios.channels;
    bool interleaved = abl->mNumberBuffers == 1;
    if (!interleaved && abl->mNumberBuffers != channels)
    {
        if (!g_ios.render_complained)
        {
            g_ios.render_complained = true;
            mel_log_error("audioout", "ios: render buffer layout mismatch (%u buffers for %u channels); emitting silence", (u32)abl->mNumberBuffers, channels);
        }
        return noErr;
    }
    u32  frames = (u32)frame_count;
    bool wrote = false;
    for (u32 off = 0; off < frames; off += g_ios.scratch_frames)
    {
        u32 chunk = frames - off < g_ios.scratch_frames ? frames - off : g_ios.scratch_frames;
        for (u32 i = 0; i < ol->count; i++)
        {
            if (!ol->opens[i].started)
                continue;
            u32 got = ol->opens[i].pull(ol->opens[i].token, g_ios.scratch, chunk);
            if (got > chunk)
                got = chunk;
            if (got == 0)
                continue;
            wrote = true;
            if (interleaved)
            {
                f32* dst = (f32*)abl->mBuffers[0].mData + (usize)off * channels;
                for (usize s = 0; s < (usize)got * channels; s++)
                    dst[s] += g_ios.scratch[s];
            }
            else
                for (u32 ch = 0; ch < channels; ch++)
                {
                    f32* dst = (f32*)abl->mBuffers[ch].mData + off;
                    for (u32 f = 0; f < got; f++)
                        dst[f] += g_ios.scratch[(usize)f * channels + ch];
                }
        }
    }
    if (wrote)
        *is_silence = NO;
    return noErr;
}

static void ios_config_changed(void)
{
    @autoreleasepool
    {
        Mel_Array(Ios_Open) fired;
        mel_array_init(&fired, g_ios.alloc);
        pthread_mutex_lock(&g_ios_lock);
        if (g_ios_engine != nil && ios_started_count() > 0)
        {
            NSError* err = nil;
            if (![g_ios_engine startAndReturnError:&err])
            {
                mel_log_error("audioout", "ios: engine restart after configuration change failed: %s; signalling lost to all streams", ios_errstr(err));
                Open_List* nl = ios_opens_clone(0);
                if (nl == NULL)
                    mel_log_error("audioout", "ios: open list allocation failed after restart failure; loss signal not delivered");
                else
                {
                    for (u32 i = 0; i < nl->count; i++)
                    {
                        Ios_Open* o = &nl->opens[i];
                        if (o->lost)
                            continue;
                        o->lost = true;
                        if (o->on_lost != NULL)
                            mel_array_push(&fired, *o);
                    }
                    ios_opens_swap(nl);
                }
            }
            else
                mel_log_info("audioout", "ios: engine restarted after configuration change");
        }
        pthread_mutex_unlock(&g_ios_lock);
        for (usize i = 0; i < fired.count; i++)
            fired.items[i].on_lost(fired.items[i].token);
        mel_array_free(&fired);
    }
}

static void ios_route_changed(void)
{
    mel_audioout_provider_notify(g_ios.provider);
    @autoreleasepool
    {
        Mel_Array(Ios_Open) fired;
        mel_array_init(&fired, g_ios.alloc);
        pthread_mutex_lock(&g_ios_lock);
        Open_List* cur = atomic_load_explicit(&g_ios.opens, memory_order_acquire);
        bool       stale = false;
        if (cur != NULL)
            for (u32 i = 0; i < cur->count && !stale; i++)
                stale = !cur->opens[i].lost && ios_route_port_for(cur->opens[i].stable_id) == nil;
        if (stale)
        {
            Open_List* nl = ios_opens_clone(0);
            if (nl == NULL)
                mel_log_error("audioout", "ios: open list allocation failed on route change; loss signal not delivered");
            else
            {
                for (u32 i = 0; i < nl->count; i++)
                {
                    Ios_Open* o = &nl->opens[i];
                    if (o->lost || ios_route_port_for(o->stable_id) != nil)
                        continue;
                    o->lost = true;
                    mel_log_warn("audioout", "ios: output %.*s left the route; signalling lost to its stream", (int)o->stable_id.len, o->stable_id.data);
                    if (o->on_lost != NULL)
                        mel_array_push(&fired, *o);
                }
                ios_opens_swap(nl);
            }
        }
        pthread_mutex_unlock(&g_ios_lock);
        for (usize i = 0; i < fired.count; i++)
            fired.items[i].on_lost(fired.items[i].token);
        mel_array_free(&fired);
    }
}

static void ios_engine_teardown(void)
{
    if (g_ios_config_observer != nil)
    {
        [[NSNotificationCenter defaultCenter] removeObserver:g_ios_config_observer];
        g_ios_config_observer = nil;
    }
    if (g_ios_engine != nil)
    {
        [g_ios_engine stop];
        if (g_ios_source != nil)
            [g_ios_engine detachNode:g_ios_source];
        g_ios_source = nil;
        g_ios_engine = nil;
    }
    Open_List* ol = atomic_exchange_explicit(&g_ios.opens, NULL, memory_order_acq_rel);
    if (ol != NULL)
    {
        for (u32 i = 0; i < ol->count; i++)
            if (ol->opens[i].stable_id.data != NULL)
                mel_dealloc(g_ios.alloc, ol->opens[i].stable_id.data);
        mel_dealloc(g_ios.alloc, ol);
    }
    for (usize i = 0; i < g_ios.garbage.count; i++)
        mel_dealloc(g_ios.alloc, g_ios.garbage.items[i]);
    mel_array_clear(&g_ios.garbage);
    if (g_ios.scratch != NULL)
    {
        mel_dealloc(g_ios.alloc, g_ios.scratch);
        g_ios.scratch = NULL;
    }
    g_ios.scratch_frames = 0;
    g_ios.channels = 0;
    g_ios.samplerate = 0;
}

static Mel_AudioOut_Status ios_engine_create(void)
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError*        err = nil;
    NSString*       cat = session.category;
    bool            playback_capable = [cat isEqualToString:AVAudioSessionCategoryPlayback] || [cat isEqualToString:AVAudioSessionCategoryPlayAndRecord] || [cat isEqualToString:AVAudioSessionCategoryMultiRoute];
    if (!playback_capable)
    {
        NSString* want = [cat isEqualToString:AVAudioSessionCategoryRecord] ? AVAudioSessionCategoryPlayAndRecord : AVAudioSessionCategoryPlayback;
        if (![session setCategory:want error:&err])
        {
            mel_log_error("audioout", "ios: setCategory %s failed: %s", ios_nscstr(want), ios_errstr(err));
            return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
        }
        mel_log_info("audioout", "ios: session category was %s; set %s for playback; session policy belongs to a future audiopolicy module", ios_nscstr(cat), ios_nscstr(want));
    }
    if (![session setActive:YES error:&err])
    {
        mel_log_error("audioout", "ios: session activation failed: %s", ios_errstr(err));
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    u32 rate = (u32)(session.sampleRate + 0.5);
    if (rate == 0)
    {
        mel_log_error("audioout", "ios: AVAudioSession reports zero sample rate; cannot open output");
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }

    g_ios_engine = [[AVAudioEngine alloc] init];
    AVAudioFormat* out_fmt = [g_ios_engine.outputNode outputFormatForBus:0];
    u32            channels = (u32)out_fmt.channelCount;
    if (channels == 0)
        channels = 2;
    AVAudioFormat* src_fmt = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:(double)rate channels:(AVAudioChannelCount)channels interleaved:YES];
    if (src_fmt == nil)
    {
        mel_log_error("audioout", "ios: interleaved f32 AVAudioFormat (%u Hz %u ch) creation failed", rate, channels);
        g_ios_engine = nil;
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    g_ios.scratch = mel_alloc(g_ios.alloc, sizeof(f32) * MEL_AUDIOOUT_IOS_QUANTUM_FRAMES * channels);
    if (g_ios.scratch == NULL)
    {
        mel_log_error("audioout", "ios: render scratch allocation failed (%u frames x %u ch)", MEL_AUDIOOUT_IOS_QUANTUM_FRAMES, channels);
        g_ios_engine = nil;
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    g_ios.scratch_frames = MEL_AUDIOOUT_IOS_QUANTUM_FRAMES;
    g_ios.channels = channels;
    g_ios.samplerate = rate;
    g_ios.render_complained = false;

    g_ios_source = [[AVAudioSourceNode alloc] initWithFormat:src_fmt
                                                 renderBlock:^OSStatus(BOOL* isSilence, const AudioTimeStamp* timestamp, AVAudioFrameCount frameCount, AudioBufferList* outputData) {
                                                     MEL_UNUSED(timestamp);
                                                     return ios_render(isSilence, frameCount, outputData);
                                                 }];
    [g_ios_engine attachNode:g_ios_source];
    [g_ios_engine connect:g_ios_source to:g_ios_engine.mainMixerNode format:nil];

    g_ios_config_observer = [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioEngineConfigurationChangeNotification
                                                                              object:g_ios_engine
                                                                               queue:nil
                                                                          usingBlock:^(NSNotification* n) {
                                                                              MEL_UNUSED(n);
                                                                              ios_config_changed();
                                                                          }];
    mel_log_info("audioout", "ios: output engine created (%u ch @ %u Hz, quantum %u frames)", channels, rate, MEL_AUDIOOUT_IOS_QUANTUM_FRAMES);
    return MEL_AUDIOOUT_OK;
}

static void ios_engine_run_state(void)
{
    if (g_ios_engine == nil)
        return;
    u32 started = ios_started_count();
    if (started > 0 && !g_ios_engine.isRunning)
    {
        NSError* err = nil;
        [g_ios_engine prepare];
        if (![g_ios_engine startAndReturnError:&err])
            mel_log_error("audioout", "ios: AVAudioEngine start failed: %s; output is silent", ios_errstr(err));
        else
            mel_log_info("audioout", "ios: output engine started (%u stream(s))", started);
    }
    else if (started == 0 && g_ios_engine.isRunning)
    {
        [g_ios_engine stop];
        mel_log_info("audioout", "ios: output engine stopped (no started streams)");
    }
}

static Mel_AudioOut_Status ios_open(void* user, str8 stable_id, Mel_AudioOut_Format req, Mel_AudioOut_Open_Opt opt, Mel_AudioOut_Granted* granted, Mel_AudioOut_Source src)
{
    MEL_UNUSED(user);
    MEL_UNUSED(opt);
    assert(granted != NULL);
    assert(src.pull != NULL);
    @autoreleasepool
    {
        pthread_mutex_lock(&g_ios_lock);
        Mel_AudioOut_Status st = MEL_AUDIOOUT_OK;
        if (ios_route_port_for(stable_id) == nil)
        {
            mel_log_error("audioout", "ios: open %.*s refused: not in the current route; iOS sends all output to the active route and offers no per-device targeting", (int)stable_id.len, stable_id.data);
            st = MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
        }
        else
        {
            bool created = false;
            if (g_ios_engine == nil)
            {
                st = ios_engine_create();
                created = !mel_audioout_status_failed(st);
            }
            if (!mel_audioout_status_failed(st))
            {
                Open_List* nl = ios_opens_clone(1);
                if (nl == NULL)
                {
                    mel_log_error("audioout", "ios: open list allocation failed");
                    st = MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
                    if (created)
                        ios_engine_teardown();
                }
                else
                {
                    nl->opens[nl->count] = (Ios_Open){
                        .token = src.token,
                        .pull = src.pull,
                        .on_lost = src.on_lost,
                        .stable_id = str8_dup(stable_id, g_ios.alloc),
                        .started = false,
                        .lost = false,
                    };
                    nl->count++;
                    ios_opens_swap(nl);
                    granted->format.samplerate = g_ios.samplerate;
                    granted->format.channels = g_ios.channels;
                    granted->format.block_frames = g_ios.scratch_frames;
                    granted->exclusive = false;
                    AVAudioSession* session = [AVAudioSession sharedInstance];
                    f64             latency_sec = (f64)session.outputLatency + (f64)session.IOBufferDuration;
                    granted->os_timestamps = latency_sec > 0.0;
                    granted->latency_frames = latency_sec > 0.0 ? (u32)(latency_sec * (f64)g_ios.samplerate + 0.5) : 0;
                    if (req.samplerate != granted->format.samplerate || req.channels != granted->format.channels)
                        mel_log_info("audioout",
                                     "ios: open %.*s granted %u Hz %u ch block %u (requested %u Hz %u ch)",
                                     (int)stable_id.len,
                                     stable_id.data,
                                     granted->format.samplerate,
                                     granted->format.channels,
                                     granted->format.block_frames,
                                     req.samplerate,
                                     req.channels);
                }
            }
        }
        pthread_mutex_unlock(&g_ios_lock);
        return st;
    }
}

static void ios_set_started(str8 stable_id, void* token, bool started, const char* verb)
{
    pthread_mutex_lock(&g_ios_lock);
    Open_List* nl = ios_opens_clone(0);
    if (nl == NULL)
        mel_log_error("audioout", "ios: open list allocation failed on %s", verb);
    else
    {
        bool found = false;
        for (u32 i = 0; i < nl->count; i++)
            if (nl->opens[i].token == token)
            {
                nl->opens[i].started = started;
                found = true;
            }
        if (!found)
        {
            mel_dealloc(g_ios.alloc, nl);
            mel_log_warn("audioout", "ios: %s on %.*s with unknown token %p", verb, (int)stable_id.len, stable_id.data, token);
        }
        else
        {
            ios_opens_swap(nl);
            ios_engine_run_state();
        }
    }
    pthread_mutex_unlock(&g_ios_lock);
}

static void ios_start(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    @autoreleasepool
    {
        ios_set_started(stable_id, token, true, "start");
    }
}

static void ios_stop(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    @autoreleasepool
    {
        ios_set_started(stable_id, token, false, "stop");
    }
}

static void ios_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    @autoreleasepool
    {
        pthread_mutex_lock(&g_ios_lock);
        Open_List* cur = atomic_load_explicit(&g_ios.opens, memory_order_acquire);
        if (cur == NULL || cur->count == 0)
            mel_log_debug("audioout", "ios: close on %.*s with no open streams", (int)stable_id.len, stable_id.data);
        else
        {
            Open_List* nl = mel_alloc(g_ios.alloc, sizeof(Open_List) + sizeof(Ios_Open) * (usize)cur->count);
            if (nl == NULL)
                mel_log_error("audioout", "ios: open list allocation failed on close; stream stays attached");
            else
            {
                u32  kept = 0;
                str8 removed = STR8_EMPTY;
                for (u32 i = 0; i < cur->count; i++)
                {
                    if (cur->opens[i].token != token)
                        nl->opens[kept++] = cur->opens[i];
                    else
                        removed = cur->opens[i].stable_id;
                }
                if (kept == cur->count)
                {
                    mel_dealloc(g_ios.alloc, nl);
                    mel_log_warn("audioout", "ios: close on %.*s with unknown token %p", (int)stable_id.len, stable_id.data, token);
                }
                else
                {
                    nl->count = kept;
                    ios_opens_swap(nl);
                    if (removed.data != NULL)
                        mel_dealloc(g_ios.alloc, removed.data);
                    if (kept == 0)
                    {
                        ios_engine_teardown();
                        mel_log_info("audioout", "ios: output engine released (last stream closed)");
                    }
                    else
                        ios_engine_run_state();
                }
            }
        }
        pthread_mutex_unlock(&g_ios_lock);
    }
}

static void* ios_native(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    @autoreleasepool
    {
        for (AVAudioSessionPortDescription* port in g_ios_ports)
            if (ios_id_is_uid(stable_id, port.UID))
                return (__bridge void*)port;
        return NULL;
    }
}

static void ios_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    @autoreleasepool
    {
        pthread_mutex_lock(&g_ios_lock);
        Open_List* cur = atomic_load_explicit(&g_ios.opens, memory_order_acquire);
        if (cur != NULL && cur->count > 0)
            mel_log_warn("audioout", "ios: %u open stream(s) still live at shutdown; releasing", cur->count);
        ios_engine_teardown();
        pthread_mutex_unlock(&g_ios_lock);

        if (g_ios_route_observer != nil)
        {
            [[NSNotificationCenter defaultCenter] removeObserver:g_ios_route_observer];
            g_ios_route_observer = nil;
        }
        if (g_ios_volume_observer != nil)
        {
            [[AVAudioSession sharedInstance] removeObserver:g_ios_volume_observer forKeyPath:@"outputVolume"];
            g_ios_volume_observer = nil;
        }
        g_ios_ports = nil;
        ios_intern_clear();
        mel_array_free(&g_ios.interned);
        mel_array_free(&g_ios.garbage);
        if (g_ios.default_storage.data != NULL)
            mel_dealloc(g_ios.alloc, g_ios.default_storage.data);
        memset(&g_ios, 0, sizeof g_ios);
    }
}

void mel_audioout__register_host_providers(void)
{
    g_ios.alloc = mel_alloc_heap();
    mel_array_init(&g_ios.interned, g_ios.alloc);
    mel_array_init(&g_ios.garbage, g_ios.alloc);
    g_ios.default_storage = STR8_EMPTY;
    atomic_store_explicit(&g_ios.opens, NULL, memory_order_relaxed);

    static const Mel_AudioOut_Provider_Desc desc = {
        .name = "ios-avaudiosession",
        .enumerate = ios_enumerate,
        .default_id = ios_default_id,
        .open = ios_open,
        .start = ios_start,
        .stop = ios_stop,
        .close = ios_close,
        .native = ios_native,
        .shutdown = ios_shutdown,
    };
    g_ios.provider = mel_audioout_provider_register(&desc);

    @autoreleasepool
    {
        AVAudioSession* session = [AVAudioSession sharedInstance];
        g_ios_route_observer = [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification
                                                                                 object:session
                                                                                  queue:nil
                                                                             usingBlock:^(NSNotification* n) {
                                                                                 MEL_UNUSED(n);
                                                                                 ios_route_changed();
                                                                             }];
        g_ios_volume_observer = [[MelAudioOutIosVolumeObserver alloc] init];
        [session addObserver:g_ios_volume_observer forKeyPath:@"outputVolume" options:NSKeyValueObservingOptionNew context:NULL];
    }
}
