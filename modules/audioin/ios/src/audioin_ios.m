#include <audioin/provider.h>

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

#define MEL_AUDIOIN_IOS_TAP_FRAMES 1024u

typedef struct
{
    u32              count;
    Mel_AudioIn_Sink sinks[];
} Sink_List;

typedef struct
{
    const Mel_Alloc*     alloc;
    Mel_AudioIn_Provider provider;
    Mel_Array(str8) interned;
    str8           default_storage;
    str8           active_id;
    _Atomic(void*) sinks;
    Mel_Array(void*) garbage;
    f32* interleave;
    u32  interleave_cap;
    bool tap_complained;
} Ios_State;

static Ios_State       g_ios;
static pthread_mutex_t g_ios_lock = PTHREAD_MUTEX_INITIALIZER;

static AVAudioEngine*                           g_ios_engine;
static NSArray<AVAudioSessionPortDescription*>* g_ios_ports;
static id                                       g_ios_route_observer;
static id                                       g_ios_config_observer;

static const char* ios_nscstr(NSString* s) { return s != nil && s.UTF8String != NULL ? s.UTF8String : ""; }

static const char* ios_errstr(NSError* err) { return err != nil ? ios_nscstr(err.localizedDescription) : "unknown"; }

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

static const mel_audioin_kind* ios_kind_for(NSString* port_type)
{
    if ([port_type isEqualToString:AVAudioSessionPortBuiltInMic] || [port_type isEqualToString:AVAudioSessionPortHeadsetMic])
        return &mel_audioin_builtin;
    if ([port_type isEqualToString:AVAudioSessionPortUSBAudio])
        return &mel_audioin_usb;
    if ([port_type isEqualToString:AVAudioSessionPortBluetoothHFP] || [port_type isEqualToString:AVAudioSessionPortBluetoothLE])
        return &mel_audioin_bluetooth;
    return &mel_audioin_unknown;
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

static AVAudioSessionPortDescription* ios_port_for(str8 stable_id)
{
    for (AVAudioSessionPortDescription* port in [AVAudioSession sharedInstance].availableInputs)
        if (ios_id_is_uid(stable_id, port.UID))
            return port;
    return nil;
}

static void ios_enumerate(void* user, Mel_AudioIn_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    assert(g_ios.alloc != NULL);
    @autoreleasepool
    {
        AVAudioSession*                          session = [AVAudioSession sharedInstance];
        NSArray<AVAudioSessionPortDescription*>* inputs = session.availableInputs;
        g_ios_ports = inputs;
        ios_intern_clear();
        if (inputs == nil || inputs.count == 0)
        {
            mel_log_warn("audioin", "ios: AVAudioSession reports no available inputs (category %s)", ios_nscstr(session.category));
            return;
        }
        u32 rate = (u32)(session.sampleRate + 0.5);
        if (rate == 0)
            mel_log_warn("audioin", "ios: AVAudioSession reports zero sample rate; descriptors will carry 0 Hz");
        NSString* current_uid = session.currentRoute.inputs.firstObject.UID;
        bool      gain_settable = session.isInputGainSettable;
        for (AVAudioSessionPortDescription* port in inputs)
        {
            u32             channels = (u32)port.channels.count;
            bool            is_current = current_uid != nil && [port.UID isEqualToString:current_uid];
            Mel_AudioIn_Raw raw = {
                .stable_id = ios_intern_id(port.UID),
                .name = ios_intern_cstr(port.portName.UTF8String),
                .kind = ios_kind_for(port.portType),
                .channels = channels > 0 ? channels : 1u,
                .samplerate = rate,
                .samplerates = &rate,
                .samplerate_count = 1,
                .caps = { .gain = is_current && gain_settable },
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
        AVAudioSession* session = [AVAudioSession sharedInstance];
        NSString*       uid = session.currentRoute.inputs.firstObject.UID;
        if (uid == nil)
            uid = session.availableInputs.firstObject.UID;
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

static void ios_sinks_swap(Sink_List* nl)
{
    void* old = atomic_exchange_explicit(&g_ios.sinks, nl, memory_order_acq_rel);
    if (old != NULL)
        mel_array_push(&g_ios.garbage, old);
}

static Mel_AudioIn_Status ios_sink_add(Mel_AudioIn_Sink sink)
{
    Sink_List* cur = atomic_load_explicit(&g_ios.sinks, memory_order_acquire);
    u32        count = cur != NULL ? cur->count : 0;
    Sink_List* nl = mel_alloc(g_ios.alloc, sizeof(Sink_List) + sizeof(Mel_AudioIn_Sink) * ((usize)count + 1u));
    if (nl == NULL)
    {
        mel_log_error("audioin", "ios: sink list allocation failed");
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    for (u32 i = 0; i < count; i++)
        nl->sinks[i] = cur->sinks[i];
    nl->sinks[count] = sink;
    nl->count = count + 1u;
    ios_sinks_swap(nl);
    return MEL_AUDIOIN_OK;
}

static u32 ios_sink_remove(void* token)
{
    Sink_List* cur = atomic_load_explicit(&g_ios.sinks, memory_order_acquire);
    if (cur == NULL || cur->count == 0)
        return 0;
    Sink_List* nl = mel_alloc(g_ios.alloc, sizeof(Sink_List) + sizeof(Mel_AudioIn_Sink) * (usize)cur->count);
    if (nl == NULL)
    {
        mel_log_error("audioin", "ios: sink list allocation failed on close; sink stays attached");
        return cur->count;
    }
    u32 kept = 0;
    for (u32 i = 0; i < cur->count; i++)
        if (cur->sinks[i].token != token)
            nl->sinks[kept++] = cur->sinks[i];
    if (kept == cur->count)
    {
        mel_dealloc(g_ios.alloc, nl);
        mel_log_warn("audioin", "ios: close with unknown token %p", token);
        return kept;
    }
    nl->count = kept;
    ios_sinks_swap(nl);
    return kept;
}

static void ios_deliver(AVAudioPCMBuffer* buf)
{
    Sink_List* sl = atomic_load_explicit(&g_ios.sinks, memory_order_acquire);
    if (sl == NULL || sl->count == 0)
        return;
    u32 frames = (u32)buf.frameLength;
    if (frames == 0)
        return;
    AVAudioFormat* fmt = buf.format;
    u32            channels = (u32)fmt.channelCount;
    u32            samplerate = (u32)(fmt.sampleRate + 0.5);
    if (fmt.commonFormat != AVAudioPCMFormatFloat32 || buf.floatChannelData == NULL || channels == 0)
    {
        if (!g_ios.tap_complained)
        {
            g_ios.tap_complained = true;
            mel_log_error("audioin", "ios: tap delivered non-float32 buffer (common format %ld, %u ch); dropping capture frames", (long)fmt.commonFormat, channels);
        }
        return;
    }
    const f32* interleaved;
    if (fmt.isInterleaved || channels == 1)
        interleaved = buf.floatChannelData[0];
    else
    {
        u32 need = frames * channels;
        if (need > g_ios.interleave_cap)
        {
            f32* nb = g_ios.interleave != NULL ? mel_realloc(g_ios.alloc, g_ios.interleave, (usize)need * sizeof(f32)) : mel_alloc(g_ios.alloc, (usize)need * sizeof(f32));
            if (nb == NULL)
            {
                if (!g_ios.tap_complained)
                {
                    g_ios.tap_complained = true;
                    mel_log_error("audioin", "ios: interleave buffer growth to %u samples failed; dropping capture frames", need);
                }
                return;
            }
            g_ios.interleave = nb;
            g_ios.interleave_cap = need;
        }
        f32* const* src = buf.floatChannelData;
        for (u32 ch = 0; ch < channels; ch++)
            for (u32 f = 0; f < frames; f++)
                g_ios.interleave[(usize)f * channels + ch] = src[ch][f];
        interleaved = g_ios.interleave;
    }
    for (u32 i = 0; i < sl->count; i++)
        if (sl->sinks[i].on_frames != NULL)
            sl->sinks[i].on_frames(sl->sinks[i].token, interleaved, frames, samplerate, channels);
}

static void ios_install_tap(void)
{
    AVAudioInputNode* input = g_ios_engine.inputNode;
    AVAudioFormat*    fmt = [input outputFormatForBus:0];
    [input installTapOnBus:0
                bufferSize:MEL_AUDIOIN_IOS_TAP_FRAMES
                    format:fmt
                     block:^(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
                         MEL_UNUSED(when);
                         ios_deliver(buffer);
                     }];
}

static Sink_List* ios_engine_detach(void)
{
    Sink_List* sl = atomic_exchange_explicit(&g_ios.sinks, NULL, memory_order_acq_rel);
    if (g_ios_config_observer != nil)
    {
        [[NSNotificationCenter defaultCenter] removeObserver:g_ios_config_observer];
        g_ios_config_observer = nil;
    }
    if (g_ios_engine != nil)
    {
        [g_ios_engine stop];
        [g_ios_engine.inputNode removeTapOnBus:0];
        g_ios_engine = nil;
    }
    for (usize i = 0; i < g_ios.garbage.count; i++)
        mel_dealloc(g_ios.alloc, g_ios.garbage.items[i]);
    mel_array_clear(&g_ios.garbage);
    if (g_ios.interleave != NULL)
    {
        mel_dealloc(g_ios.alloc, g_ios.interleave);
        g_ios.interleave = NULL;
        g_ios.interleave_cap = 0;
    }
    if (g_ios.active_id.data != NULL)
    {
        mel_dealloc(g_ios.alloc, g_ios.active_id.data);
        g_ios.active_id = STR8_EMPTY;
    }
    return sl;
}

static void ios_fire_lost(Sink_List* sl)
{
    if (sl == NULL)
        return;
    for (u32 i = 0; i < sl->count; i++)
        if (sl->sinks[i].on_lost != NULL)
            sl->sinks[i].on_lost(sl->sinks[i].token);
    mel_dealloc(g_ios.alloc, sl);
}

static void ios_config_changed(void)
{
    @autoreleasepool
    {
        Sink_List* lost = NULL;
        pthread_mutex_lock(&g_ios_lock);
        if (g_ios_engine != nil)
        {
            if (ios_port_for(g_ios.active_id) == nil)
            {
                mel_log_warn("audioin", "ios: engine configuration changed and input %.*s is gone; signalling lost", (int)g_ios.active_id.len, g_ios.active_id.data);
                lost = ios_engine_detach();
            }
            else
            {
                AVAudioInputNode* input = g_ios_engine.inputNode;
                [input removeTapOnBus:0];
                ios_install_tap();
                NSError* err = nil;
                if (![g_ios_engine startAndReturnError:&err])
                {
                    mel_log_error("audioin", "ios: engine restart after configuration change failed: %s; signalling lost", ios_errstr(err));
                    lost = ios_engine_detach();
                }
                else
                    mel_log_info("audioin", "ios: engine restarted after configuration change on %.*s", (int)g_ios.active_id.len, g_ios.active_id.data);
            }
        }
        pthread_mutex_unlock(&g_ios_lock);
        ios_fire_lost(lost);
    }
}

static Mel_AudioIn_Status ios_engine_start(AVAudioSessionPortDescription* port, str8 stable_id)
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError*        err = nil;
    NSString*       cat = session.category;
    bool            record_capable = [cat isEqualToString:AVAudioSessionCategoryPlayAndRecord] || [cat isEqualToString:AVAudioSessionCategoryRecord] || [cat isEqualToString:AVAudioSessionCategoryMultiRoute];
    if (!record_capable)
    {
        if (![session setCategory:AVAudioSessionCategoryPlayAndRecord withOptions:AVAudioSessionCategoryOptionAllowBluetoothHFP error:&err])
        {
            mel_log_error("audioin", "ios: setCategory PlayAndRecord failed: %s", ios_errstr(err));
            return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
        }
        mel_log_info("audioin", "ios: session category was %s; set PlayAndRecord (+AllowBluetooth) for capture; session policy belongs to a future audiopolicy module", ios_nscstr(cat));
    }
    if (![session setActive:YES error:&err])
    {
        mel_log_error("audioin", "ios: session activation failed: %s", ios_errstr(err));
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    NSString* current_uid = session.currentRoute.inputs.firstObject.UID;
    if (current_uid == nil || ![current_uid isEqualToString:port.UID])
    {
        if (![session setPreferredInput:port error:&err])
        {
            mel_log_error("audioin", "ios: setPreferredInput %s failed: %s", ios_nscstr(port.UID), ios_errstr(err));
            return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
        }
    }

    g_ios_engine = [[AVAudioEngine alloc] init];
    AVAudioInputNode* input = g_ios_engine.inputNode;
    AVAudioFormat*    fmt = [input outputFormatForBus:0];
    u32               channels = (u32)fmt.channelCount;
    if (channels == 0)
    {
        mel_log_error("audioin", "ios: input node reports zero channels; cannot capture");
        g_ios_engine = nil;
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    u32 prealloc = MEL_AUDIOIN_IOS_TAP_FRAMES * channels * 2u;
    g_ios.interleave = mel_alloc(g_ios.alloc, (usize)prealloc * sizeof(f32));
    g_ios.interleave_cap = g_ios.interleave != NULL ? prealloc : 0;
    g_ios.tap_complained = false;

    ios_install_tap();
    [g_ios_engine prepare];
    if (![g_ios_engine startAndReturnError:&err])
    {
        mel_log_error("audioin", "ios: AVAudioEngine start failed: %s", ios_errstr(err));
        [input removeTapOnBus:0];
        g_ios_engine = nil;
        if (g_ios.interleave != NULL)
        {
            mel_dealloc(g_ios.alloc, g_ios.interleave);
            g_ios.interleave = NULL;
            g_ios.interleave_cap = 0;
        }
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }

    g_ios_config_observer = [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioEngineConfigurationChangeNotification
                                                                              object:g_ios_engine
                                                                               queue:nil
                                                                          usingBlock:^(NSNotification* n) {
                                                                              MEL_UNUSED(n);
                                                                              ios_config_changed();
                                                                          }];
    g_ios.active_id = str8_dup(stable_id, g_ios.alloc);
    mel_log_info("audioin", "ios: capture engine started on %.*s (%u ch @ %u Hz)", (int)stable_id.len, stable_id.data, channels, (u32)(fmt.sampleRate + 0.5));
    return MEL_AUDIOIN_OK;
}

static Mel_AudioIn_Status ios_open(void* user, str8 stable_id, Mel_AudioIn_Sink sink)
{
    MEL_UNUSED(user);
    @autoreleasepool
    {
        pthread_mutex_lock(&g_ios_lock);
        Mel_AudioIn_Status st = MEL_AUDIOIN_OK;
        if (g_ios_engine != nil && !str8_equals(g_ios.active_id, stable_id))
        {
            mel_log_error("audioin", "ios: capture already active on %.*s; iOS has a single input route, cannot also open %.*s", (int)g_ios.active_id.len, g_ios.active_id.data, (int)stable_id.len, stable_id.data);
            st = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
        }
        else
        {
            if (g_ios_engine == nil)
            {
                if (!mel_audioin_auth_is_granted(mel_audioin_authorization()))
                {
                    mel_log_error("audioin", "ios: open without record permission; call mel_audioin_authorize first");
                    st = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_DENIED;
                }
                else
                {
                    AVAudioSessionPortDescription* port = ios_port_for(stable_id);
                    if (port == nil)
                    {
                        mel_log_error("audioin", "ios: open: no available input matches id %.*s", (int)stable_id.len, stable_id.data);
                        st = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_NO_DEVICE;
                    }
                    else
                        st = ios_engine_start(port, stable_id);
                }
            }
            if (!mel_audioin_status_failed(st))
            {
                st = ios_sink_add(sink);
                Sink_List* cur = atomic_load_explicit(&g_ios.sinks, memory_order_acquire);
                if (mel_audioin_status_failed(st) && (cur == NULL || cur->count == 0))
                    mel_dealloc(g_ios.alloc, ios_engine_detach());
            }
        }
        pthread_mutex_unlock(&g_ios_lock);
        return st;
    }
}

static void ios_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    @autoreleasepool
    {
        pthread_mutex_lock(&g_ios_lock);
        if (g_ios_engine == nil)
            mel_log_debug("audioin", "ios: close with no active engine (already lost or never opened)");
        else if (!str8_equals(g_ios.active_id, stable_id))
            mel_log_warn("audioin", "ios: close for %.*s but active capture is %.*s", (int)stable_id.len, stable_id.data, (int)g_ios.active_id.len, g_ios.active_id.data);
        else if (ios_sink_remove(token) == 0)
        {
            mel_dealloc(g_ios.alloc, ios_engine_detach());
            mel_log_info("audioin", "ios: capture engine stopped on %.*s", (int)stable_id.len, stable_id.data);
        }
        pthread_mutex_unlock(&g_ios_lock);
    }
}

static f32 ios_gain(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    @autoreleasepool
    {
        AVAudioSession* session = [AVAudioSession sharedInstance];
        if (!ios_id_is_uid(stable_id, session.currentRoute.inputs.firstObject.UID))
        {
            mel_log_error("audioin", "ios: gain queried for %.*s but only the current route input exposes gain", (int)stable_id.len, stable_id.data);
            return 0.0f;
        }
        return (f32)session.inputGain;
    }
}

static Mel_AudioIn_Status ios_set_gain(void* user, str8 stable_id, f32 gain)
{
    MEL_UNUSED(user);
    @autoreleasepool
    {
        AVAudioSession* session = [AVAudioSession sharedInstance];
        if (!ios_id_is_uid(stable_id, session.currentRoute.inputs.firstObject.UID) || !session.isInputGainSettable)
        {
            mel_log_error("audioin", "ios: set_gain on %.*s unsupported (not the current route input, or gain not settable)", (int)stable_id.len, stable_id.data);
            return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
        }
        NSError* err = nil;
        if (![session setInputGain:gain error:&err])
        {
            mel_log_error("audioin", "ios: setInputGain %.2f failed: %s", (f64)gain, ios_errstr(err));
            return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
        }
        return MEL_AUDIOIN_OK;
    }
}

static const mel_audioin_auth* ios_authorization(void* user)
{
    MEL_UNUSED(user);
    @autoreleasepool
    {
        if (@available(iOS 17.0, *))
        {
            switch (AVAudioApplication.sharedInstance.recordPermission)
            {
            case AVAudioApplicationRecordPermissionGranted:
                return &mel_audioin_auth_granted;
            case AVAudioApplicationRecordPermissionDenied:
                return &mel_audioin_auth_denied;
            default:
                return &mel_audioin_auth_not_determined;
            }
        }
        switch ([AVAudioSession sharedInstance].recordPermission)
        {
        case AVAudioSessionRecordPermissionGranted:
            return &mel_audioin_auth_granted;
        case AVAudioSessionRecordPermissionDenied:
            return &mel_audioin_auth_denied;
        default:
            return &mel_audioin_auth_not_determined;
        }
    }
}

static void ios_authorize(void* user, Mel_AudioIn_Sink sink)
{
    MEL_UNUSED(user);
    if (sink.on_auth == NULL)
        return;
    @autoreleasepool
    {
        if (@available(iOS 17.0, *))
        {
            [AVAudioApplication requestRecordPermissionWithCompletionHandler:^(BOOL granted) {
                sink.on_auth(sink.token, granted ? &mel_audioin_auth_granted : &mel_audioin_auth_denied);
            }];
            return;
        }
        [[AVAudioSession sharedInstance] requestRecordPermission:^(BOOL granted) {
            sink.on_auth(sink.token, granted ? &mel_audioin_auth_granted : &mel_audioin_auth_denied);
        }];
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

static void ios_route_changed(void)
{
    mel_audioin_provider_notify(g_ios.provider);
    @autoreleasepool
    {
        Sink_List* lost = NULL;
        pthread_mutex_lock(&g_ios_lock);
        if (g_ios_engine != nil && ios_port_for(g_ios.active_id) == nil)
        {
            mel_log_warn("audioin", "ios: active capture input %.*s vanished on route change; signalling lost", (int)g_ios.active_id.len, g_ios.active_id.data);
            lost = ios_engine_detach();
        }
        pthread_mutex_unlock(&g_ios_lock);
        ios_fire_lost(lost);
    }
}

static void ios_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    @autoreleasepool
    {
        Sink_List* lost = NULL;
        pthread_mutex_lock(&g_ios_lock);
        if (g_ios_engine != nil)
        {
            mel_log_warn("audioin", "ios: capture still active at shutdown; releasing");
            lost = ios_engine_detach();
        }
        pthread_mutex_unlock(&g_ios_lock);
        ios_fire_lost(lost);

        if (g_ios_route_observer != nil)
        {
            [[NSNotificationCenter defaultCenter] removeObserver:g_ios_route_observer];
            g_ios_route_observer = nil;
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

void mel_audioin__register_host_providers(void)
{
    g_ios.alloc = mel_alloc_heap();
    mel_array_init(&g_ios.interned, g_ios.alloc);
    mel_array_init(&g_ios.garbage, g_ios.alloc);
    g_ios.default_storage = STR8_EMPTY;
    g_ios.active_id = STR8_EMPTY;
    atomic_store_explicit(&g_ios.sinks, NULL, memory_order_relaxed);

    static const Mel_AudioIn_Provider_Desc desc = {
        .name = "ios-avaudiosession",
        .enumerate = ios_enumerate,
        .default_id = ios_default_id,
        .open = ios_open,
        .close = ios_close,
        .gain = ios_gain,
        .set_gain = ios_set_gain,
        .authorization = ios_authorization,
        .authorize = ios_authorize,
        .native = ios_native,
        .shutdown = ios_shutdown,
    };
    g_ios.provider = mel_audioin_provider_register(&desc);

    @autoreleasepool
    {
        g_ios_route_observer = [[NSNotificationCenter defaultCenter] addObserverForName:AVAudioSessionRouteChangeNotification
                                                                                 object:[AVAudioSession sharedInstance]
                                                                                  queue:nil
                                                                             usingBlock:^(NSNotification* n) {
                                                                                 MEL_UNUSED(n);
                                                                                 ios_route_changed();
                                                                             }];
    }
}
