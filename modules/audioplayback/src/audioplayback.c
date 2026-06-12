#include <audioplayback/audioplayback.h>

#include <audioout/audioout.h>
#include <audioout/provider.h>

#include <allocator/allocator.h>
#include <pcm/ring.h>
#include <pcm/resample.h>
#include <pcm/convert.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>

struct Mel_AudioPlayback
{
    const Mel_Alloc* alloc;
    Mel_AudioOut     device;

    u32 in_rate;
    u32 in_channels;
    u32 dev_rate;
    u32 dev_channels;

    Mel_Pcm_Ring*             ring;
    Mel_AudioPlayback_Pull_Fn pull;
    void*                     user;

    Mel_AudioPlayback_Granted granted_;
    u32                       dev_latency;

    _Atomic(u32) live_bits;
    _Atomic(u64) underruns;

    bool        convert;
    bool        remix_logged;
    f64         cursor;
    f32*        fetch;
    f32*        deint;
    f32*        hist;
    f32*        resampled;
    f32*        remixed;
    f32**       planes;
    f32**       converted;
    const f32** remix_planes;
    u32         cap_in;
    u32         cap_out;
};

static u32 pb_fetch(Mel_AudioPlayback* p, f32* dst, u32 frames)
{
    if (p->pull)
    {
        u32 got = p->pull(p->user, dst, frames);
        if (got > frames)
            got = frames;
        if (got < frames)
            memset(dst + (usize)got * p->in_channels, 0, sizeof(f32) * (usize)(frames - got) * p->in_channels);
        return frames;
    }

    u32 got = mel_pcm_ring_read(p->ring, dst, frames);
    if (got < frames)
    {
        memset(dst + (usize)got * p->in_channels, 0, sizeof(f32) * (usize)(frames - got) * p->in_channels);
        atomic_fetch_add_explicit(&p->underruns, frames - got, memory_order_relaxed);
        atomic_fetch_or_explicit(&p->live_bits, MEL_AUDIOPLAYBACK_WARN_UNDERRUN, memory_order_release);
    }
    return frames;
}

static bool pb_reserve(Mel_AudioPlayback* p, u32 in_frames, u32 out_frames)
{
    if (in_frames <= p->cap_in && out_frames <= p->cap_out)
        return true;

    u32 new_in = in_frames > p->cap_in ? in_frames * 2u : p->cap_in;
    u32 new_out = out_frames > p->cap_out ? out_frames * 2u : p->cap_out;

    f32* fetch = mel_alloc(p->alloc, sizeof(f32) * (usize)new_in * p->in_channels);
    f32* deint = mel_alloc(p->alloc, sizeof(f32) * ((usize)new_in + 1u) * p->in_channels);
    f32* resampled = mel_alloc(p->alloc, sizeof(f32) * (usize)new_out * p->in_channels);
    f32* remixed = mel_alloc(p->alloc, sizeof(f32) * (usize)new_out * p->dev_channels);
    if (!fetch || !deint || !resampled || !remixed)
    {
        if (fetch)
            mel_dealloc(p->alloc, fetch);
        if (deint)
            mel_dealloc(p->alloc, deint);
        if (resampled)
            mel_dealloc(p->alloc, resampled);
        if (remixed)
            mel_dealloc(p->alloc, remixed);
        mel_log_error("audioplayback", "conversion scratch allocation failed (%u device frames)", out_frames);
        return false;
    }

    if (p->fetch)
        mel_dealloc(p->alloc, p->fetch);
    if (p->deint)
        mel_dealloc(p->alloc, p->deint);
    if (p->resampled)
        mel_dealloc(p->alloc, p->resampled);
    if (p->remixed)
        mel_dealloc(p->alloc, p->remixed);
    p->fetch = fetch;
    p->deint = deint;
    p->resampled = resampled;
    p->remixed = remixed;
    p->cap_in = new_in;
    p->cap_out = new_out;
    return true;
}

static void pb_remix(Mel_AudioPlayback* p, f32* const* src_planes, u32 frames)
{
    u32 in = p->in_channels;
    u32 out = p->dev_channels;

    if (!p->remix_logged && in != out)
    {
        mel_log_info("audioplayback", "remixing %u -> %u channels", in, out);
        p->remix_logged = true;
    }

    for (u32 o = 0; o < out; o++)
    {
        f32* dst = p->remixed + (usize)o * p->cap_out;
        if (in == out)
            memcpy(dst, src_planes[o], sizeof(f32) * frames);
        else if (in == 1)
            memcpy(dst, src_planes[0], sizeof(f32) * frames);
        else if (out == 1)
        {
            for (u32 i = 0; i < frames; i++)
            {
                f32 sum = 0.0f;
                for (u32 ch = 0; ch < in; ch++)
                    sum += src_planes[ch][i];
                dst[i] = sum / (f32)in;
            }
        }
        else
            memcpy(dst, src_planes[o % in], sizeof(f32) * frames);
    }
}

static u32 pb_provider_pull(void* token, f32* interleaved_dst, u32 frames)
{
    Mel_AudioPlayback* p = token;
    if (frames == 0)
        return 0;

    if (!p->convert)
    {
        if (p->pull)
            return p->pull(p->user, interleaved_dst, frames);
        return pb_fetch(p, interleaved_dst, frames);
    }

    u32 n_in;
    if (p->in_rate != p->dev_rate)
    {
        f64 ratio = (f64)p->in_rate / (f64)p->dev_rate;
        n_in = (u32)(p->cursor + (f64)(frames - 1u) * ratio) + 1u;
    }
    else
        n_in = frames;

    if (!pb_reserve(p, n_in, frames))
        return 0;

    pb_fetch(p, p->fetch, n_in);

    f32** planes = p->planes;
    for (u32 ch = 0; ch < p->in_channels; ch++)
    {
        planes[ch] = p->deint + (usize)ch * (p->cap_in + 1u);
        planes[ch][0] = p->hist[ch];
    }
    for (u32 i = 0; i < n_in; i++)
        for (u32 ch = 0; ch < p->in_channels; ch++)
            planes[ch][1u + i] = p->fetch[(usize)i * p->in_channels + ch];
    for (u32 ch = 0; ch < p->in_channels; ch++)
        p->hist[ch] = planes[ch][n_in];

    f32** converted = p->converted;
    if (p->in_rate != p->dev_rate)
    {
        f64 ratio = (f64)p->in_rate / (f64)p->dev_rate;
        f64 committed = p->cursor;
        for (u32 ch = 0; ch < p->in_channels; ch++)
        {
            f64  cur = p->cursor;
            f32* dst = p->resampled + (usize)ch * p->cap_out;
            mel_pcm_resample_linear(planes[ch], n_in + 1u, dst, frames, ratio, &cur);
            committed = cur;
            converted[ch] = dst;
        }
        p->cursor = committed - (f64)n_in;
    }
    else
        for (u32 ch = 0; ch < p->in_channels; ch++)
            converted[ch] = planes[ch] + 1u;

    pb_remix(p, converted, frames);

    for (u32 o = 0; o < p->dev_channels; o++)
        p->remix_planes[o] = p->remixed + (usize)o * p->cap_out;
    mel_pcm_interleave(interleaved_dst, p->remix_planes, p->dev_channels, frames);
    return frames;
}

static void pb_on_lost(void* token)
{
    Mel_AudioPlayback* p = token;
    atomic_fetch_or_explicit(&p->live_bits, MEL_AUDIOPLAYBACK_RESULT_LOST, memory_order_release);
    mel_log_warn("audioplayback", "output device lost; writes are rejected until a new open");
}

static void pb_free(Mel_AudioPlayback* p)
{
    if (p->ring)
        mel_pcm_ring_destroy(p->ring);
    if (p->fetch)
        mel_dealloc(p->alloc, p->fetch);
    if (p->deint)
        mel_dealloc(p->alloc, p->deint);
    if (p->hist)
        mel_dealloc(p->alloc, p->hist);
    if (p->resampled)
        mel_dealloc(p->alloc, p->resampled);
    if (p->remixed)
        mel_dealloc(p->alloc, p->remixed);
    if (p->planes)
        mel_dealloc(p->alloc, p->planes);
    if (p->converted)
        mel_dealloc(p->alloc, p->converted);
    if (p->remix_planes)
        mel_dealloc(p->alloc, p->remix_planes);
    mel_dealloc(p->alloc, p);
}

Mel_AudioPlayback_Open_Result mel_audioplayback_open(const Mel_Alloc* alloc, Mel_AudioOut device, Mel_AudioPlayback_Opt opt)
{
    Mel_AudioPlayback_Open_Result r = { 0 };
    bool                          write_mode = opt.ring_capacity_frames > 0;
    bool                          pull_mode = opt.pull != NULL;
    assert(alloc != NULL);
    assert(opt.sample_rate > 0);
    assert(opt.channels > 0);
    assert(write_mode != pull_mode);
    if (!alloc || opt.sample_rate == 0 || opt.channels == 0)
    {
        mel_log_error("audioplayback", "open with zero sample_rate/channels: the format is mandatory");
        r.status = MEL_AUDIOPLAYBACK_ERROR | MEL_AUDIOPLAYBACK_RESULT_UNSUPPORTED;
        return r;
    }
    if (write_mode == pull_mode)
    {
        mel_log_error("audioplayback", "open must choose exactly one mode: ring_capacity_frames > 0 XOR pull != NULL");
        r.status = MEL_AUDIOPLAYBACK_ERROR | MEL_AUDIOPLAYBACK_RESULT_UNSUPPORTED;
        return r;
    }
    if (!mel_audioout_alive(device))
    {
        mel_log_error("audioplayback", "open on dead device handle");
        r.status = MEL_AUDIOPLAYBACK_ERROR | MEL_AUDIOPLAYBACK_RESULT_NO_DEVICE;
        return r;
    }

    Mel_AudioPlayback* p = mel_alloc_type(alloc, Mel_AudioPlayback);
    if (!p)
    {
        r.status = MEL_AUDIOPLAYBACK_ERROR | MEL_AUDIOPLAYBACK_RESULT_UNSUPPORTED;
        return r;
    }
    memset(p, 0, sizeof *p);
    p->alloc = alloc;
    p->device = device;
    p->in_rate = opt.sample_rate;
    p->in_channels = opt.channels;
    p->pull = opt.pull;
    p->user = opt.user;
    p->cursor = 1.0;

    if (write_mode)
    {
        p->ring = mel_pcm_ring_create(alloc, opt.channels, opt.ring_capacity_frames);
        if (!p->ring)
        {
            pb_free(p);
            r.status = MEL_AUDIOPLAYBACK_ERROR | MEL_AUDIOPLAYBACK_RESULT_UNSUPPORTED;
            return r;
        }
    }

    Mel_AudioOut_Format req = {
        .samplerate = opt.sample_rate,
        .channels = opt.channels,
    };
    Mel_AudioOut_Open_Opt out_opt = { .exclusive = opt.exclusive };
    Mel_AudioOut_Granted  granted = { 0 };
    Mel_AudioOut_Source   src = {
          .pull = pb_provider_pull,
          .on_lost = pb_on_lost,
          .token = p,
    };

    Mel_AudioOut_Status ost = mel_audioout__open(device, req, out_opt, &granted, src);
    if (mel_audioout_status_failed(ost))
    {
        u32 bit = MEL_AUDIOPLAYBACK_RESULT_UNSUPPORTED;
        if (ost & MEL_AUDIOOUT_RESULT_BUSY)
            bit = MEL_AUDIOPLAYBACK_RESULT_BUSY;
        else if (ost & (MEL_AUDIOOUT_RESULT_LOST | MEL_AUDIOOUT_RESULT_NO_DEVICE))
            bit = MEL_AUDIOPLAYBACK_RESULT_NO_DEVICE;
        mel_log_error("audioplayback", "provider open failed (audioout status 0x%x)", ost);
        pb_free(p);
        r.status = MEL_AUDIOPLAYBACK_ERROR | bit;
        return r;
    }

    assert(granted.format.samplerate > 0);
    assert(granted.format.channels > 0);
    p->dev_rate = granted.format.samplerate;
    p->dev_channels = granted.format.channels;
    p->dev_latency = granted.latency_frames;
    p->granted_ = (Mel_AudioPlayback_Granted){
        .exclusive = granted.exclusive,
        .os_timestamps = granted.os_timestamps,
    };

    u32 warn = 0;
    p->convert = p->dev_rate != p->in_rate || p->dev_channels != p->in_channels;
    if (p->convert)
    {
        warn |= MEL_AUDIOPLAYBACK_WARN_CONVERTED;
        mel_log_info("audioplayback", "converting %u ch @ %u Hz -> %u ch @ %u Hz", p->in_channels, p->in_rate, p->dev_channels, p->dev_rate);

        p->hist = mel_calloc(alloc, sizeof(f32) * p->in_channels);
        p->planes = mel_alloc(alloc, sizeof(f32*) * p->in_channels);
        p->converted = mel_alloc(alloc, sizeof(f32*) * p->in_channels);
        p->remix_planes = mel_alloc(alloc, sizeof(const f32*) * p->dev_channels);
        if (!p->hist || !p->planes || !p->converted || !p->remix_planes)
        {
            mel_audioout__close(device, p);
            pb_free(p);
            r.status = MEL_AUDIOPLAYBACK_ERROR | MEL_AUDIOPLAYBACK_RESULT_UNSUPPORTED;
            return r;
        }
    }
    if (opt.exclusive && !granted.exclusive)
    {
        warn |= MEL_AUDIOPLAYBACK_WARN_EXCLUSIVE_DROPPED;
        mel_log_warn("audioplayback", "exclusive access lowered to shared");
    }

    mel_audioout__start(device, p);

    r.playback = p;
    r.status = warn != 0 ? (MEL_AUDIOPLAYBACK_WARNED | warn) : MEL_AUDIOPLAYBACK_OK;
    return r;
}

u32 mel_audioplayback_write(Mel_AudioPlayback* p, const f32* interleaved_src, u32 frames)
{
    assert(p != NULL);
    assert(interleaved_src != NULL);
    assert(p->ring != NULL);
    if (!p->ring)
    {
        mel_log_error("audioplayback", "write on a pull-mode stream; the engine serves pull instead");
        return 0;
    }
    u32 bits = atomic_load_explicit(&p->live_bits, memory_order_acquire);
    if (bits & MEL_AUDIOPLAYBACK_RESULT_LOST)
        return 0;
    u32 accepted = mel_pcm_ring_write(p->ring, interleaved_src, frames);
    if (accepted > 0)
        atomic_fetch_and_explicit(&p->live_bits, ~MEL_AUDIOPLAYBACK_WARN_UNDERRUN, memory_order_release);
    return accepted;
}

u32 mel_audioplayback_writable(const Mel_AudioPlayback* p)
{
    assert(p != NULL);
    assert(p->ring != NULL);
    if (!p->ring)
    {
        mel_log_error("audioplayback", "writable on a pull-mode stream; there is no ring");
        return 0;
    }
    return mel_pcm_ring_write_available(p->ring);
}

Mel_AudioPlayback_Status mel_audioplayback_status(const Mel_AudioPlayback* p)
{
    assert(p != NULL);
    u32 bits = atomic_load_explicit((_Atomic(u32)*)&p->live_bits, memory_order_acquire);
    if (bits & MEL_AUDIOPLAYBACK_RESULT_LOST)
        return MEL_AUDIOPLAYBACK_ERROR | bits;
    if (bits != 0)
        return MEL_AUDIOPLAYBACK_WARNED | bits;
    return MEL_AUDIOPLAYBACK_OK;
}

Mel_AudioPlayback_Granted mel_audioplayback_granted(const Mel_AudioPlayback* p)
{
    assert(p != NULL);
    return p->granted_;
}

u32 mel_audioplayback_latency_frames(const Mel_AudioPlayback* p)
{
    assert(p != NULL);
    u64 device_part = (u64)p->dev_latency * p->in_rate / p->dev_rate;
    u64 ring_part = p->ring ? mel_pcm_ring_read_available(p->ring) : 0u;
    return (u32)(device_part + ring_part);
}

u64 mel_audioplayback_underrun_frames(const Mel_AudioPlayback* p)
{
    assert(p != NULL);
    return atomic_load_explicit((_Atomic(u64)*)&p->underruns, memory_order_relaxed);
}

void mel_audioplayback_close(Mel_AudioPlayback* p)
{
    if (!p)
        return;
    mel_audioout__stop(p->device, p);
    mel_audioout__close(p->device, p);
    pb_free(p);
}
