#include <audioin/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>
#include <string/str8.h>
#include <thread/thread.h>
#include <log/log.h>

#include <alsa/asoundlib.h>

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    u32              count;
    Mel_AudioIn_Sink sinks[];
} Alsa_Sink_List;

typedef struct
{
    str8                    stable_id;
    str8                    name;
    str8                    ctl_name;
    const mel_audioin_kind* kind;
    u32                     channels;
    u32                     samplerate;
    Mel_AudioIn_Rates       rates;
    Mel_AudioIn_Caps        caps;
    bool                    probed;
    bool                    probe_warned;
    bool                    present;
} Alsa_Device;

typedef struct
{
    str8              stable_id;
    snd_pcm_t*        pcm;
    snd_pcm_format_t  format;
    u32               channels;
    u32               samplerate;
    snd_pcm_uframes_t period_frames;
    void*             raw;
    f32*              samples;
    Mel_Thread        thread;
    bool              spawned;
    bool              overrun_warned;
    u64               xruns;
    _Atomic(u32)      running;
    _Atomic(u32)      lost;
    _Atomic(void*)    sinks;
    Mel_Array(void*) garbage;
} Alsa_Engine;

static struct
{
    const Mel_Alloc*     alloc;
    Mel_AudioIn_Provider provider;
    Mel_Array(Alsa_Device*) devices;
    Mel_Array(Alsa_Engine*) engines;
} g_alsa;

static const char* alsa_open_name(str8 stable_id) { return (const char*)stable_id.data + S8("alsa:").len; }

static Alsa_Device* alsa_device_find(str8 stable_id)
{
    for (usize i = 0; i < g_alsa.devices.count; i++)
        if (str8_equals(g_alsa.devices.items[i]->stable_id, stable_id))
            return g_alsa.devices.items[i];
    return NULL;
}

static Alsa_Engine* alsa_engine_find(str8 stable_id)
{
    for (usize i = 0; i < g_alsa.engines.count; i++)
        if (str8_equals(g_alsa.engines.items[i]->stable_id, stable_id))
            return g_alsa.engines.items[i];
    return NULL;
}

static const mel_audioin_kind* alsa_classify(const char* driver, const char* card_id)
{
    if (strcmp(driver, "USB-Audio") == 0 || strstr(card_id, "USB") != NULL)
        return &mel_audioin_usb;
    if (strcmp(driver, "Loopback") == 0)
        return &mel_audioin_loopback;
    if (strncmp(driver, "HDA", 3) == 0)
        return &mel_audioin_builtin;
    return &mel_audioin_unknown;
}

static snd_mixer_t* alsa_mixer_open(const Alsa_Device* d, snd_mixer_elem_t** out_elem)
{
    if (d->ctl_name.len == 0)
        return NULL;
    snd_mixer_t* m = NULL;
    if (snd_mixer_open(&m, 0) < 0)
        return NULL;
    if (snd_mixer_attach(m, (const char*)d->ctl_name.data) < 0 || snd_mixer_selem_register(m, NULL, NULL) < 0 || snd_mixer_load(m) < 0)
    {
        snd_mixer_close(m);
        return NULL;
    }
    for (snd_mixer_elem_t* e = snd_mixer_first_elem(m); e != NULL; e = snd_mixer_elem_next(e))
        if (snd_mixer_selem_has_capture_volume(e))
        {
            *out_elem = e;
            return m;
        }
    snd_mixer_close(m);
    return NULL;
}

static u32 alsa_nominal_rate(const Mel_AudioIn_Rates* rates)
{
    u32 best = 0;
    for (usize i = 0; i < rates->count; i++)
    {
        u32 r = rates->items[i];
        if (r == 48000u)
            return r;
        u32 dr = r > 48000u ? r - 48000u : 48000u - r;
        u32 db = best > 48000u ? best - 48000u : 48000u - best;
        if (best == 0u || dr < db)
            best = r;
    }
    return best;
}

static bool alsa_probe_pcm(Alsa_Device* d)
{
    const char* open_name = alsa_open_name(d->stable_id);
    snd_pcm_t*  pcm = NULL;
    int         rc = snd_pcm_open(&pcm, open_name, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
    if (rc < 0)
    {
        if (!d->probe_warned)
        {
            d->probe_warned = true;
            mel_log_warn("audioin", "alsa: probe open(%s) failed: %s; listing without format info until a refresh succeeds", open_name, snd_strerror(rc));
        }
        return false;
    }

    snd_pcm_hw_params_t* hw = NULL;
    snd_pcm_hw_params_alloca(&hw);
    rc = snd_pcm_hw_params_any(pcm, hw);
    if (rc < 0)
    {
        mel_log_warn("audioin", "alsa: probe hw_params_any(%s) failed: %s", open_name, snd_strerror(rc));
        snd_pcm_close(pcm);
        return false;
    }

    unsigned int ch_min = 0;
    unsigned int ch_max = 0;
    snd_pcm_hw_params_get_channels_min(hw, &ch_min);
    snd_pcm_hw_params_get_channels_max(hw, &ch_max);
    u32 channels = ch_max;
    if (ch_max > 64u)
    {
        unsigned int narrowed = ch_min > 2u ? ch_min : 2u;
        if (snd_pcm_hw_params_set_channels_near(pcm, hw, &narrowed) == 0)
            channels = narrowed;
        else
            channels = ch_min;
    }

    static const u32 candidates[] = { 8000u, 11025u, 16000u, 22050u, 32000u, 44100u, 48000u, 88200u, 96000u, 176400u, 192000u };
    mel_array_clear(&d->rates);
    for (usize i = 0; i < sizeof candidates / sizeof candidates[0]; i++)
        if (snd_pcm_hw_params_test_rate(pcm, hw, candidates[i], 0) == 0)
            mel_array_push(&d->rates, candidates[i]);
    if (d->rates.count == 0)
    {
        unsigned int rmin = 0;
        unsigned int rmax = 0;
        int          dir = 0;
        snd_pcm_hw_params_get_rate_min(hw, &rmin, &dir);
        snd_pcm_hw_params_get_rate_max(hw, &rmax, &dir);
        if (rmin > 0u)
            mel_array_push(&d->rates, rmin);
        if (rmax > 0u && rmax != rmin)
            mel_array_push(&d->rates, rmax);
    }
    snd_pcm_close(pcm);

    d->channels = channels;
    d->samplerate = alsa_nominal_rate(&d->rates);
    d->probed = d->channels > 0u && d->samplerate > 0u;
    if (d->probed)
        d->probe_warned = false;
    return d->probed;
}

static void alsa_probe_device(Alsa_Device* d)
{
    if (!alsa_probe_pcm(d))
        return;
    snd_mixer_elem_t* elem = NULL;
    snd_mixer_t*      m = alsa_mixer_open(d, &elem);
    d->caps.gain = m != NULL;
    if (m)
        snd_mixer_close(m);
}

static Alsa_Device* alsa_device_create(str8 stable_id, str8 name, str8 ctl_name, const mel_audioin_kind* kind)
{
    Alsa_Device* d = mel_alloc_type(g_alsa.alloc, Alsa_Device);
    memset(d, 0, sizeof *d);
    d->stable_id = stable_id;
    d->name = name;
    d->ctl_name = ctl_name;
    d->kind = kind;
    mel_array_init(&d->rates, g_alsa.alloc);
    mel_array_push(&g_alsa.devices, d);
    return d;
}

static void alsa_device_free(Alsa_Device* d)
{
    if (d->stable_id.data)
        mel_dealloc(g_alsa.alloc, d->stable_id.data);
    if (d->name.data)
        mel_dealloc(g_alsa.alloc, d->name.data);
    if (d->ctl_name.data)
        mel_dealloc(g_alsa.alloc, d->ctl_name.data);
    mel_array_free(&d->rates);
    mel_dealloc(g_alsa.alloc, d);
}

static void alsa_scan(void)
{
    for (usize i = 0; i < g_alsa.devices.count; i++)
        g_alsa.devices.items[i]->present = false;

    snd_ctl_card_info_t* card_info = NULL;
    snd_pcm_info_t*      pcm_info = NULL;
    snd_ctl_card_info_alloca(&card_info);
    snd_pcm_info_alloca(&pcm_info);

    int card = -1;
    while (snd_card_next(&card) == 0 && card >= 0)
    {
        char ctl_addr[16];
        snprintf(ctl_addr, sizeof ctl_addr, "hw:%d", card);
        snd_ctl_t* ctl = NULL;
        int        rc = snd_ctl_open(&ctl, ctl_addr, 0);
        if (rc < 0)
        {
            mel_log_warn("audioin", "alsa: snd_ctl_open(%s) failed: %s", ctl_addr, snd_strerror(rc));
            continue;
        }
        rc = snd_ctl_card_info(ctl, card_info);
        if (rc < 0)
        {
            mel_log_warn("audioin", "alsa: snd_ctl_card_info(%s) failed: %s", ctl_addr, snd_strerror(rc));
            snd_ctl_close(ctl);
            continue;
        }
        const char* card_id = snd_ctl_card_info_get_id(card_info);
        const char* card_name = snd_ctl_card_info_get_name(card_info);
        const char* driver = snd_ctl_card_info_get_driver(card_info);

        int dev = -1;
        while (snd_ctl_pcm_next_device(ctl, &dev) == 0 && dev >= 0)
        {
            snd_pcm_info_set_device(pcm_info, (unsigned int)dev);
            snd_pcm_info_set_subdevice(pcm_info, 0);
            snd_pcm_info_set_stream(pcm_info, SND_PCM_STREAM_CAPTURE);
            if (snd_ctl_pcm_info(ctl, pcm_info) < 0)
                continue;

            str8         id = str8_fmt(g_alsa.alloc, "alsa:hw:CARD=%s,DEV=%d", card_id, dev);
            Alsa_Device* d = alsa_device_find(id);
            if (d)
                mel_dealloc(g_alsa.alloc, id.data);
            else
            {
                const char* pcm_name = snd_pcm_info_get_name(pcm_info);
                str8        name = (pcm_name != NULL && pcm_name[0] != '\0' && strcmp(pcm_name, card_name) != 0) ? str8_fmt(g_alsa.alloc, "%s %s", card_name, pcm_name) : str8_fmt(g_alsa.alloc, "%s", card_name);
                d = alsa_device_create(id, name, str8_fmt(g_alsa.alloc, "hw:CARD=%s", card_id), alsa_classify(driver, card_id));
            }
            d->present = true;
            if (!d->probed)
                alsa_probe_device(d);
        }
        snd_ctl_close(ctl);
    }

    Alsa_Device* def = alsa_device_find(S8("alsa:default"));
    if (!def)
        def = alsa_device_create(str8_fmt(g_alsa.alloc, "alsa:default"), str8_fmt(g_alsa.alloc, "Default"), STR8_EMPTY, &mel_audioin_unknown);
    def->present = true;
    if (!def->probed)
        alsa_probe_device(def);

    for (usize i = 0; i < g_alsa.devices.count;)
    {
        if (g_alsa.devices.items[i]->present)
        {
            i++;
            continue;
        }
        alsa_device_free(g_alsa.devices.items[i]);
        mel_array_remove_unordered(&g_alsa.devices, i);
    }
}

static void alsa_enumerate(void* user, Mel_AudioIn_Enum_Fn fn, void* fn_user)
{
    MEL_UNUSED(user);
    alsa_scan();
    for (usize i = 0; i < g_alsa.devices.count; i++)
    {
        Alsa_Device* d = g_alsa.devices.items[i];
        if (!d->probed && str8_equals(d->stable_id, S8("alsa:default")))
            continue;
        Mel_AudioIn_Raw raw = {
            .stable_id = d->stable_id,
            .name = d->name,
            .kind = d->kind,
            .channels = d->channels,
            .samplerate = d->samplerate,
            .samplerates = d->rates.items,
            .samplerate_count = (u32)d->rates.count,
            .caps = d->caps,
        };
        if (!fn(&raw, fn_user))
            return;
    }
}

static str8 alsa_default_id(void* user)
{
    MEL_UNUSED(user);
    return S8("alsa:default");
}

static void alsa_sinks_swap(Alsa_Engine* e, Alsa_Sink_List* nl)
{
    void* old = atomic_exchange_explicit(&e->sinks, nl, memory_order_acq_rel);
    if (old)
        mel_array_push(&e->garbage, old);
}

static void alsa_engine_fire_lost(Alsa_Engine* e)
{
    if (atomic_exchange_explicit(&e->lost, 1u, memory_order_acq_rel) != 0u)
        return;
    Alsa_Sink_List* sl = atomic_load_explicit(&e->sinks, memory_order_acquire);
    if (!sl)
        return;
    for (u32 i = 0; i < sl->count; i++)
        if (sl->sinks[i].on_lost)
            sl->sinks[i].on_lost(sl->sinks[i].token);
}

static void alsa_engine_convert(Alsa_Engine* e, usize samples)
{
    if (e->format == SND_PCM_FORMAT_S16_LE)
    {
        const i16* src = e->raw;
        for (usize i = 0; i < samples; i++)
            e->samples[i] = (f32)src[i] * (1.0f / 32768.0f);
    }
    else if (e->format == SND_PCM_FORMAT_S32_LE)
    {
        const i32* src = e->raw;
        for (usize i = 0; i < samples; i++)
            e->samples[i] = (f32)src[i] * (1.0f / 2147483648.0f);
    }
}

static int alsa_engine_reader(void* user)
{
    Alsa_Engine* e = user;
    void*        dst = e->format == SND_PCM_FORMAT_FLOAT_LE ? (void*)e->samples : e->raw;

    while (atomic_load_explicit(&e->running, memory_order_acquire) != 0u)
    {
        snd_pcm_sframes_t got = snd_pcm_readi(e->pcm, dst, e->period_frames);
        if (got < 0)
        {
            if (got == -EAGAIN)
                continue;
            int rc = snd_pcm_recover(e->pcm, (int)got, 1);
            if (rc < 0)
            {
                mel_log_error("audioin", "alsa: capture on %.*s unrecoverable: %s", (int)e->stable_id.len, e->stable_id.data, snd_strerror((int)got));
                alsa_engine_fire_lost(e);
                return 1;
            }
            if (snd_pcm_state(e->pcm) == SND_PCM_STATE_PREPARED)
                snd_pcm_start(e->pcm);
            e->xruns++;
            if (!e->overrun_warned)
            {
                e->overrun_warned = true;
                mel_log_warn("audioin", "alsa: overrun on %.*s (recovered)", (int)e->stable_id.len, e->stable_id.data);
            }
            continue;
        }
        if (got == 0)
            continue;
        e->overrun_warned = false;
        if (e->format != SND_PCM_FORMAT_FLOAT_LE)
            alsa_engine_convert(e, (usize)got * e->channels);
        Alsa_Sink_List* sl = atomic_load_explicit(&e->sinks, memory_order_acquire);
        if (sl)
            for (u32 i = 0; i < sl->count; i++)
                if (sl->sinks[i].on_frames)
                    sl->sinks[i].on_frames(sl->sinks[i].token, e->samples, (u32)got, e->samplerate, e->channels);
    }
    return 0;
}

static int alsa_engine_configure(Alsa_Engine* e, const Alsa_Device* d)
{
    snd_pcm_hw_params_t* hw = NULL;
    snd_pcm_hw_params_alloca(&hw);
    int rc = snd_pcm_hw_params_any(e->pcm, hw);
    if (rc < 0)
        return rc;
    rc = snd_pcm_hw_params_set_access(e->pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (rc < 0)
        return rc;

    static const snd_pcm_format_t formats[] = { SND_PCM_FORMAT_FLOAT_LE, SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_S32_LE };
    rc = -EINVAL;
    for (usize i = 0; i < sizeof formats / sizeof formats[0]; i++)
    {
        rc = snd_pcm_hw_params_set_format(e->pcm, hw, formats[i]);
        if (rc == 0)
        {
            e->format = formats[i];
            break;
        }
    }
    if (rc < 0)
        return rc;

    unsigned int channels = d->channels;
    rc = snd_pcm_hw_params_set_channels_near(e->pcm, hw, &channels);
    if (rc < 0)
        return rc;
    unsigned int rate = d->samplerate;
    int          dir = 0;
    rc = snd_pcm_hw_params_set_rate_near(e->pcm, hw, &rate, &dir);
    if (rc < 0)
        return rc;
    snd_pcm_uframes_t period = rate / 100u;
    if (period == 0u)
        period = 64u;
    int pdir = 0;
    rc = snd_pcm_hw_params_set_period_size_near(e->pcm, hw, &period, &pdir);
    if (rc < 0)
        return rc;
    rc = snd_pcm_hw_params(e->pcm, hw);
    if (rc < 0)
        return rc;

    e->channels = channels;
    e->samplerate = rate;
    e->period_frames = period;
    return 0;
}

static void alsa_engine_destroy(Alsa_Engine* e)
{
    atomic_store_explicit(&e->running, 0u, memory_order_release);
    if (e->spawned)
    {
        mel_thread_join(&e->thread, NULL);
        e->spawned = false;
    }
    if (e->pcm)
    {
        snd_pcm_drop(e->pcm);
        snd_pcm_close(e->pcm);
        e->pcm = NULL;
    }
    Alsa_Sink_List* sl = atomic_exchange_explicit(&e->sinks, NULL, memory_order_acq_rel);
    if (sl)
        mel_dealloc(g_alsa.alloc, sl);
    for (usize i = 0; i < e->garbage.count; i++)
        mel_dealloc(g_alsa.alloc, e->garbage.items[i]);
    mel_array_free(&e->garbage);
    if (e->xruns > 0u)
        mel_log_info("audioin", "alsa: capture %.*s closed after %llu overrun(s)", (int)e->stable_id.len, e->stable_id.data, (unsigned long long)e->xruns);
    if (e->raw)
        mel_dealloc(g_alsa.alloc, e->raw);
    if (e->samples)
        mel_dealloc(g_alsa.alloc, e->samples);
    if (e->stable_id.data)
        mel_dealloc(g_alsa.alloc, e->stable_id.data);
    mel_dealloc(g_alsa.alloc, e);
}

static Mel_AudioIn_Status alsa_open(void* user, str8 stable_id, Mel_AudioIn_Sink sink)
{
    MEL_UNUSED(user);
    Alsa_Engine* e = alsa_engine_find(stable_id);
    if (e)
    {
        if (atomic_load_explicit(&e->lost, memory_order_acquire) != 0u)
        {
            mel_log_error("audioin", "alsa: open on lost capture %.*s", (int)stable_id.len, stable_id.data);
            return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_LOST;
        }
        Alsa_Sink_List* cur = atomic_load_explicit(&e->sinks, memory_order_acquire);
        u32             count = cur ? cur->count : 0u;
        Alsa_Sink_List* nl = mel_alloc(g_alsa.alloc, sizeof(Alsa_Sink_List) + sizeof(Mel_AudioIn_Sink) * ((usize)count + 1u));
        for (u32 i = 0; i < count; i++)
            nl->sinks[i] = cur->sinks[i];
        nl->sinks[count] = sink;
        nl->count = count + 1u;
        alsa_sinks_swap(e, nl);
        return MEL_AUDIOIN_OK;
    }

    Alsa_Device* d = alsa_device_find(stable_id);
    if (!d || !d->probed)
    {
        mel_log_error("audioin", "alsa: open on unknown or unprobed device %.*s", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_NO_DEVICE;
    }

    e = mel_alloc_type(g_alsa.alloc, Alsa_Engine);
    memset(e, 0, sizeof *e);
    e->stable_id = str8_dup(d->stable_id, g_alsa.alloc);
    mel_array_init(&e->garbage, g_alsa.alloc);

    const char* open_name = alsa_open_name(d->stable_id);
    int         rc = snd_pcm_open(&e->pcm, open_name, SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0)
    {
        mel_log_error("audioin", "alsa: snd_pcm_open(%s, CAPTURE) failed: %s", open_name, snd_strerror(rc));
        alsa_engine_destroy(e);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_NO_DEVICE;
    }
    rc = alsa_engine_configure(e, d);
    if (rc < 0)
    {
        mel_log_error("audioin", "alsa: hw_params(%s) failed: %s", open_name, snd_strerror(rc));
        alsa_engine_destroy(e);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }

    usize samples = (usize)e->period_frames * e->channels;
    e->samples = mel_alloc(g_alsa.alloc, sizeof(f32) * samples);
    if (e->format != SND_PCM_FORMAT_FLOAT_LE)
        e->raw = mel_alloc(g_alsa.alloc, (e->format == SND_PCM_FORMAT_S16_LE ? sizeof(i16) : sizeof(i32)) * samples);

    Alsa_Sink_List* sl = mel_alloc(g_alsa.alloc, sizeof(Alsa_Sink_List) + sizeof(Mel_AudioIn_Sink));
    sl->count = 1u;
    sl->sinks[0] = sink;
    atomic_store_explicit(&e->sinks, sl, memory_order_release);

    rc = snd_pcm_prepare(e->pcm);
    if (rc == 0)
        rc = snd_pcm_start(e->pcm);
    if (rc < 0)
    {
        mel_log_error("audioin", "alsa: starting capture on %s failed: %s", open_name, snd_strerror(rc));
        alsa_engine_destroy(e);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }

    atomic_store_explicit(&e->running, 1u, memory_order_release);
    if (!mel_thread_spawn(&e->thread, alsa_engine_reader, e, .name = "mel-ain-alsa"))
    {
        mel_log_error("audioin", "alsa: capture thread spawn failed for %s", open_name);
        alsa_engine_destroy(e);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    e->spawned = true;
    mel_array_push(&g_alsa.engines, e);
    mel_log_info("audioin", "alsa: capture opened %s: %u ch @ %u Hz, period %u frames, fmt %s", open_name, e->channels, e->samplerate, (u32)e->period_frames, snd_pcm_format_name(e->format));
    return MEL_AUDIOIN_OK;
}

static void alsa_close(void* user, str8 stable_id, void* token)
{
    MEL_UNUSED(user);
    for (usize idx = 0; idx < g_alsa.engines.count; idx++)
    {
        Alsa_Engine* e = g_alsa.engines.items[idx];
        if (!str8_equals(e->stable_id, stable_id))
            continue;
        Alsa_Sink_List* cur = atomic_load_explicit(&e->sinks, memory_order_acquire);
        u32             count = cur ? cur->count : 0u;
        Alsa_Sink_List* nl = mel_alloc(g_alsa.alloc, sizeof(Alsa_Sink_List) + sizeof(Mel_AudioIn_Sink) * (usize)count);
        u32             kept = 0u;
        for (u32 i = 0; i < count; i++)
            if (cur->sinks[i].token != token)
                nl->sinks[kept++] = cur->sinks[i];
        nl->count = kept;
        if (kept == 0u)
        {
            mel_dealloc(g_alsa.alloc, nl);
            mel_array_remove_unordered(&g_alsa.engines, idx);
            alsa_engine_destroy(e);
        }
        else
            alsa_sinks_swap(e, nl);
        return;
    }
    mel_log_warn("audioin", "alsa: close on device with no open capture: %.*s", (int)stable_id.len, stable_id.data);
}

static f32 alsa_gain(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    Alsa_Device* d = alsa_device_find(stable_id);
    if (!d || !d->caps.gain)
    {
        mel_log_error("audioin", "alsa: gain on device without capture volume: %.*s", (int)stable_id.len, stable_id.data);
        return 0.0f;
    }
    snd_mixer_elem_t* elem = NULL;
    snd_mixer_t*      m = alsa_mixer_open(d, &elem);
    if (!m)
    {
        mel_log_error("audioin", "alsa: mixer open failed for %.*s", (int)stable_id.len, stable_id.data);
        return 0.0f;
    }
    long min = 0;
    long max = 0;
    long v = 0;
    f32  out = 0.0f;
    if (snd_mixer_selem_get_capture_volume_range(elem, &min, &max) == 0 && max > min && snd_mixer_selem_get_capture_volume(elem, SND_MIXER_SCHN_MONO, &v) == 0)
        out = (f32)(v - min) / (f32)(max - min);
    else
        mel_log_error("audioin", "alsa: reading capture volume failed for %.*s", (int)stable_id.len, stable_id.data);
    snd_mixer_close(m);
    return out;
}

static Mel_AudioIn_Status alsa_set_gain(void* user, str8 stable_id, f32 gain)
{
    MEL_UNUSED(user);
    Alsa_Device* d = alsa_device_find(stable_id);
    if (!d || !d->caps.gain)
    {
        mel_log_error("audioin", "alsa: set_gain on device without capture volume: %.*s", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    snd_mixer_elem_t* elem = NULL;
    snd_mixer_t*      m = alsa_mixer_open(d, &elem);
    if (!m)
    {
        mel_log_error("audioin", "alsa: mixer open failed for %.*s", (int)stable_id.len, stable_id.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    long min = 0;
    long max = 0;
    int  rc = snd_mixer_selem_get_capture_volume_range(elem, &min, &max);
    if (rc == 0 && max > min)
        rc = snd_mixer_selem_set_capture_volume_all(elem, min + (long)((f32)(max - min) * gain + 0.5f));
    snd_mixer_close(m);
    if (rc < 0 || max <= min)
    {
        mel_log_error("audioin", "alsa: setting capture volume failed for %.*s: %s", (int)stable_id.len, stable_id.data, snd_strerror(rc));
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    return MEL_AUDIOIN_OK;
}

static const mel_audioin_auth* alsa_authorization(void* user)
{
    MEL_UNUSED(user);
    return &mel_audioin_auth_granted;
}

static void alsa_authorize(void* user, Mel_AudioIn_Sink sink)
{
    MEL_UNUSED(user);
    if (sink.on_auth)
        sink.on_auth(sink.token, &mel_audioin_auth_granted);
}

static void* alsa_native(void* user, str8 stable_id)
{
    MEL_UNUSED(user);
    Alsa_Engine* e = alsa_engine_find(stable_id);
    return e ? e->pcm : NULL;
}

static void alsa_shutdown(void* user, const Mel_Alloc* alloc)
{
    MEL_UNUSED(user);
    MEL_UNUSED(alloc);
    for (usize i = 0; i < g_alsa.engines.count; i++)
        alsa_engine_destroy(g_alsa.engines.items[i]);
    mel_array_free(&g_alsa.engines);
    for (usize i = 0; i < g_alsa.devices.count; i++)
        alsa_device_free(g_alsa.devices.items[i]);
    mel_array_free(&g_alsa.devices);
    memset(&g_alsa, 0, sizeof g_alsa);
}

static const Mel_AudioIn_Provider_Desc ALSA_DESC = {
    .name = "linux-alsa",
    .enumerate = alsa_enumerate,
    .default_id = alsa_default_id,
    .open = alsa_open,
    .close = alsa_close,
    .gain = alsa_gain,
    .set_gain = alsa_set_gain,
    .authorization = alsa_authorization,
    .authorize = alsa_authorize,
    .native = alsa_native,
    .shutdown = alsa_shutdown,
};

void mel_audioin__register_host_providers(void)
{
    g_alsa.alloc = mel_alloc_heap();
    mel_array_init(&g_alsa.devices, g_alsa.alloc);
    mel_array_init(&g_alsa.engines, g_alsa.alloc);
    g_alsa.provider = mel_audioin_provider_register(&ALSA_DESC);
    mel_log_info("audioin", "alsa: host provider registered; ALSA exposes no device-set notification (udev/PipeWire would), so the registry updates on mel_audioin_refresh() only");
}
