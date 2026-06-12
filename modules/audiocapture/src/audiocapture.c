#include <audiocapture/audiocapture.h>

#include <audioin/audioin.h>
#include <audioin/permission.h>
#include <audioin/provider.h>

#include <allocator/allocator.h>
#include <pcm/ring.h>
#include <pcm/resample.h>
#include <pcm/convert.h>
#include <time/nano.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>

#define MEL_AUDIOCAPTURE_STAMP_CAP 512u

typedef struct
{
    u64 stamp;
    u32 frames;
} Stamp_Entry;

typedef struct
{
    Stamp_Entry* items;
    u32          capacity;
    _Atomic(u64) head;
    _Atomic(u64) tail;
} Stamp_Fifo;

struct Mel_AudioCapture
{
    const Mel_Alloc* alloc;
    Mel_AudioIn      device;
    u32              out_rate;
    u32              out_channels;
    Mel_Pcm_Ring*    ring;
    Stamp_Fifo       stamps;

    Mel_AudioCapture_Granted granted_;

    _Atomic(u32) live_bits;
    _Atomic(u64) dropped;

    u32         src_rate;
    u32         src_channels;
    bool        remix_logged;
    f64         cursor;
    f32*        deint;
    f32*        hist;
    f32*        resampled;
    f32*        remixed;
    f32*        inter;
    f32**       planes;
    f32**       converted;
    const f32** remix_planes;
    u32         cap_src_frames;
    u32         cap_dst_frames;

    u64 next_read_stamp;
    u32 head_consumed;
};

static void stamp_push(Stamp_Fifo* f, u64 stamp, u32 frames)
{
    u64 head = atomic_load_explicit(&f->head, memory_order_relaxed);
    u64 tail = atomic_load_explicit(&f->tail, memory_order_acquire);
    if ((u32)(head - tail) >= f->capacity)
        return;
    f->items[(u32)(head % f->capacity)] = (Stamp_Entry){ .stamp = stamp, .frames = frames };
    atomic_store_explicit(&f->head, head + 1, memory_order_release);
}

static bool stamp_peek(Stamp_Fifo* f, Stamp_Entry* out)
{
    u64 tail = atomic_load_explicit(&f->tail, memory_order_relaxed);
    u64 head = atomic_load_explicit(&f->head, memory_order_acquire);
    if (head == tail)
        return false;
    *out = f->items[(u32)(tail % f->capacity)];
    return true;
}

static void stamp_pop(Stamp_Fifo* f)
{
    u64 tail = atomic_load_explicit(&f->tail, memory_order_relaxed);
    atomic_store_explicit(&f->tail, tail + 1, memory_order_release);
}

static u64 frames_to_ns(u32 frames, u32 rate) { return (u64)((f64)frames * 1e9 / (f64)rate); }

static bool cap_reserve(Mel_AudioCapture* c, u32 src_frames)
{
    u32 dst_frames = (u32)((f64)src_frames * (f64)c->out_rate / (f64)c->src_rate) + 4u;
    if (src_frames <= c->cap_src_frames && dst_frames <= c->cap_dst_frames)
        return true;

    u32 new_src = src_frames > c->cap_src_frames ? src_frames * 2u : c->cap_src_frames;
    u32 new_dst = dst_frames > c->cap_dst_frames ? dst_frames * 2u : c->cap_dst_frames;

    f32* deint = mel_alloc(c->alloc, sizeof(f32) * ((usize)new_src + 1u) * c->src_channels);
    f32* resampled = mel_alloc(c->alloc, sizeof(f32) * (usize)new_dst * c->src_channels);
    f32* remixed = mel_alloc(c->alloc, sizeof(f32) * (usize)new_dst * c->out_channels);
    f32* inter = mel_alloc(c->alloc, sizeof(f32) * (usize)new_dst * c->out_channels);
    if (!deint || !resampled || !remixed || !inter)
    {
        if (deint)
            mel_dealloc(c->alloc, deint);
        if (resampled)
            mel_dealloc(c->alloc, resampled);
        if (remixed)
            mel_dealloc(c->alloc, remixed);
        if (inter)
            mel_dealloc(c->alloc, inter);
        mel_log_error("audiocapture", "conversion scratch allocation failed (%u src frames)", src_frames);
        return false;
    }

    if (c->deint)
        mel_dealloc(c->alloc, c->deint);
    if (c->resampled)
        mel_dealloc(c->alloc, c->resampled);
    if (c->remixed)
        mel_dealloc(c->alloc, c->remixed);
    if (c->inter)
        mel_dealloc(c->alloc, c->inter);
    c->deint = deint;
    c->resampled = resampled;
    c->remixed = remixed;
    c->inter = inter;
    c->cap_src_frames = new_src;
    c->cap_dst_frames = new_dst;
    return true;
}

static void cap_reconfigure(Mel_AudioCapture* c, u32 samplerate, u32 channels)
{
    if (c->src_rate == samplerate && c->src_channels == channels)
        return;
    if (c->src_rate != 0)
        mel_log_warn("audiocapture", "source format changed mid-stream: %u ch @ %u Hz -> %u ch @ %u Hz", c->src_channels, c->src_rate, channels, samplerate);
    c->src_rate = samplerate;
    c->src_channels = channels;
    c->cursor = 1.0;
    c->cap_src_frames = 0;
    c->cap_dst_frames = 0;
    if (c->hist)
    {
        mel_dealloc(c->alloc, c->hist);
        c->hist = NULL;
    }
    c->hist = mel_calloc(c->alloc, sizeof(f32) * channels);
    if (c->planes)
        mel_dealloc(c->alloc, c->planes);
    if (c->converted)
        mel_dealloc(c->alloc, c->converted);
    if (c->remix_planes)
        mel_dealloc(c->alloc, c->remix_planes);
    c->planes = mel_alloc(c->alloc, sizeof(f32*) * channels);
    c->converted = mel_alloc(c->alloc, sizeof(f32*) * channels);
    c->remix_planes = mel_alloc(c->alloc, sizeof(const f32*) * c->out_channels);
    c->remix_logged = false;
}

static void cap_remix(Mel_AudioCapture* c, f32* const* src_planes, u32 frames)
{
    u32 in = c->src_channels;
    u32 out = c->out_channels;

    if (!c->remix_logged && in != out)
    {
        mel_log_info("audiocapture", "remixing %u -> %u channels", in, out);
        c->remix_logged = true;
    }

    for (u32 o = 0; o < out; o++)
    {
        f32* dst = c->remixed + (usize)o * c->cap_dst_frames;
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

static void cap_on_frames(void* token, const f32* interleaved, u32 frames, u32 samplerate, u32 channels, u64 timestamp_ns)
{
    Mel_AudioCapture* c = token;
    if (frames == 0 || samplerate == 0 || channels == 0)
        return;

    u64 stamp = timestamp_ns != 0 ? timestamp_ns : mel_nanos_since_unspecified_epoch();

    cap_reconfigure(c, samplerate, channels);
    if (!cap_reserve(c, frames))
        return;

    const f32* out_interleaved;
    u32        out_frames;

    if (samplerate == c->out_rate && channels == c->out_channels)
    {
        out_interleaved = interleaved;
        out_frames = frames;
    }
    else
    {
        if (!c->planes || !c->converted || !c->remix_planes)
            return;
        f32** planes = c->planes;
        for (u32 ch = 0; ch < channels; ch++)
        {
            planes[ch] = c->deint + (usize)ch * (c->cap_src_frames + 1u);
            planes[ch][0] = c->hist[ch];
        }
        for (u32 i = 0; i < frames; i++)
            for (u32 ch = 0; ch < channels; ch++)
                planes[ch][1u + i] = interleaved[(usize)i * channels + ch];
        for (u32 ch = 0; ch < channels; ch++)
            c->hist[ch] = planes[ch][frames];

        f32** converted = c->converted;
        if (samplerate != c->out_rate)
        {
            f64 ratio = (f64)samplerate / (f64)c->out_rate;
            u32 virtual_len = frames + 1u;
            f64 reach = ((f64)(virtual_len - 1u) - c->cursor) / ratio;
            u32 n_out = reach >= 0.0 ? (u32)reach + 1u : 0u;
            if (n_out > c->cap_dst_frames)
                n_out = c->cap_dst_frames;

            f64 committed = c->cursor;
            for (u32 ch = 0; ch < channels; ch++)
            {
                f64  cur = c->cursor;
                f32* dst = c->resampled + (usize)ch * c->cap_dst_frames;
                mel_pcm_resample_linear(planes[ch], virtual_len, dst, n_out, ratio, &cur);
                committed = cur;
                converted[ch] = dst;
            }
            c->cursor = committed - (f64)frames;
            out_frames = n_out;
        }
        else
        {
            for (u32 ch = 0; ch < channels; ch++)
                converted[ch] = planes[ch] + 1u;
            out_frames = frames;
        }

        if (out_frames == 0)
            return;

        cap_remix(c, converted, out_frames);

        for (u32 o = 0; o < c->out_channels; o++)
            c->remix_planes[o] = c->remixed + (usize)o * c->cap_dst_frames;
        mel_pcm_interleave(c->inter, c->remix_planes, c->out_channels, out_frames);
        out_interleaved = c->inter;
    }

    u32 accepted = mel_pcm_ring_write(c->ring, out_interleaved, out_frames);
    if (accepted > 0)
        stamp_push(&c->stamps, stamp, accepted);
    if (accepted < out_frames)
    {
        atomic_fetch_add_explicit(&c->dropped, out_frames - accepted, memory_order_relaxed);
        atomic_fetch_or_explicit(&c->live_bits, MEL_AUDIOCAPTURE_WARN_OVERRUN, memory_order_release);
    }
}

static void cap_on_lost(void* token)
{
    Mel_AudioCapture* c = token;
    atomic_fetch_or_explicit(&c->live_bits, MEL_AUDIOCAPTURE_RESULT_LOST, memory_order_release);
    mel_log_warn("audiocapture", "input device lost; buffered frames keep draining");
}

Mel_AudioCapture_Open_Result mel_audiocapture_open(const Mel_Alloc* alloc, Mel_AudioIn device, Mel_AudioCapture_Opt opt)
{
    Mel_AudioCapture_Open_Result r = { 0 };
    assert(alloc != NULL);
    assert(opt.sample_rate > 0);
    assert(opt.channels > 0);
    assert(opt.ring_capacity_frames > 0);
    if (!alloc || opt.sample_rate == 0 || opt.channels == 0 || opt.ring_capacity_frames == 0)
    {
        mel_log_error("audiocapture", "open with zero sample_rate/channels/ring_capacity_frames: the format is mandatory");
        r.status = MEL_AUDIOCAPTURE_ERROR | MEL_AUDIOCAPTURE_RESULT_UNSUPPORTED;
        return r;
    }
    if (!mel_audioin_alive(device))
    {
        mel_log_error("audiocapture", "open on dead device handle");
        r.status = MEL_AUDIOCAPTURE_ERROR | MEL_AUDIOCAPTURE_RESULT_NO_DEVICE;
        return r;
    }
    if (!mel_audioin_auth_is_granted(mel_audioin_authorization()))
    {
        mel_log_error("audiocapture", "open without granted consent (%s); mel_audioin_authorize first", mel_audioin_auth_name(mel_audioin_authorization()));
        r.status = MEL_AUDIOCAPTURE_ERROR | MEL_AUDIOCAPTURE_RESULT_DENIED;
        return r;
    }

    Mel_AudioCapture* c = mel_alloc_type(alloc, Mel_AudioCapture);
    if (!c)
    {
        r.status = MEL_AUDIOCAPTURE_ERROR | MEL_AUDIOCAPTURE_RESULT_UNSUPPORTED;
        return r;
    }
    memset(c, 0, sizeof *c);
    c->alloc = alloc;
    c->device = device;
    c->out_rate = opt.sample_rate;
    c->out_channels = opt.channels;
    c->ring = mel_pcm_ring_create(alloc, opt.channels, opt.ring_capacity_frames);
    u32 stamp_cap = opt.ring_capacity_frames < MEL_AUDIOCAPTURE_STAMP_CAP ? opt.ring_capacity_frames : MEL_AUDIOCAPTURE_STAMP_CAP;
    c->stamps.items = mel_alloc(alloc, sizeof(Stamp_Entry) * (usize)stamp_cap);
    c->stamps.capacity = stamp_cap;
    if (!c->ring || !c->stamps.items)
    {
        mel_audiocapture_close(c);
        r.status = MEL_AUDIOCAPTURE_ERROR | MEL_AUDIOCAPTURE_RESULT_UNSUPPORTED;
        return r;
    }

    u32 warn = 0;

    Mel_AudioIn_Describe_Result d = mel_audioin_describe(device, alloc);
    if (!mel_audioin_status_failed(d.status))
    {
        if (d.value.samplerate != opt.sample_rate || d.value.channels != opt.channels)
        {
            warn |= MEL_AUDIOCAPTURE_WARN_CONVERTED;
            mel_log_info("audiocapture", "converting %u ch @ %u Hz -> %u ch @ %u Hz", d.value.channels, d.value.samplerate, opt.channels, opt.sample_rate);
        }
        mel_audioin_describe_free(&d);
    }

    Mel_AudioIn_Sink sink = {
        .on_frames = cap_on_frames,
        .on_lost = cap_on_lost,
        .token = c,
    };
    Mel_AudioIn_Open_Opt in_opt = {
        .processing = {
            .echo_cancellation = opt.processing.echo_cancellation,
            .noise_suppression = opt.processing.noise_suppression,
            .auto_gain = opt.processing.auto_gain,
        },
        .exclusive = opt.exclusive,
    };
    Mel_AudioIn_Granted in_granted = { 0 };

    Mel_AudioIn_Status ist = mel_audioin__open(device, sink, in_opt, &in_granted);
    if (mel_audioin_status_failed(ist))
    {
        u32 bit = MEL_AUDIOCAPTURE_RESULT_UNSUPPORTED;
        if (ist & MEL_AUDIOIN_RESULT_BUSY)
            bit = MEL_AUDIOCAPTURE_RESULT_BUSY;
        else if (ist & MEL_AUDIOIN_RESULT_DENIED)
            bit = MEL_AUDIOCAPTURE_RESULT_DENIED;
        else if (ist & (MEL_AUDIOIN_RESULT_LOST | MEL_AUDIOIN_RESULT_NO_DEVICE))
            bit = MEL_AUDIOCAPTURE_RESULT_NO_DEVICE;
        mel_log_error("audiocapture", "provider open failed (audioin status 0x%x)", ist);
        mel_audiocapture_close(c);
        r.status = MEL_AUDIOCAPTURE_ERROR | bit;
        return r;
    }

    c->granted_ = (Mel_AudioCapture_Granted){
        .processing = {
            .echo_cancellation = in_granted.processing.echo_cancellation,
            .noise_suppression = in_granted.processing.noise_suppression,
            .auto_gain = in_granted.processing.auto_gain,
        },
        .exclusive = in_granted.exclusive,
        .os_timestamps = in_granted.os_timestamps,
    };

    bool processing_requested = opt.processing.echo_cancellation || opt.processing.noise_suppression || opt.processing.auto_gain;
    bool processing_dropped = (opt.processing.echo_cancellation && !c->granted_.processing.echo_cancellation) || (opt.processing.noise_suppression && !c->granted_.processing.noise_suppression) ||
                              (opt.processing.auto_gain && !c->granted_.processing.auto_gain);
    if (processing_requested && processing_dropped)
    {
        warn |= MEL_AUDIOCAPTURE_WARN_PROCESSING_DROPPED;
        mel_log_warn("audiocapture",
                     "voice processing lowered: requested {aec=%d ns=%d agc=%d} granted {aec=%d ns=%d agc=%d}",
                     opt.processing.echo_cancellation,
                     opt.processing.noise_suppression,
                     opt.processing.auto_gain,
                     c->granted_.processing.echo_cancellation,
                     c->granted_.processing.noise_suppression,
                     c->granted_.processing.auto_gain);
    }
    if (opt.exclusive && !c->granted_.exclusive)
    {
        warn |= MEL_AUDIOCAPTURE_WARN_EXCLUSIVE_DROPPED;
        mel_log_warn("audiocapture", "exclusive access lowered to shared");
    }

    r.capture = c;
    r.status = warn != 0 ? (MEL_AUDIOCAPTURE_WARNED | warn) : MEL_AUDIOCAPTURE_OK;
    return r;
}

static u64 read_stamp_advance(Mel_AudioCapture* c, u32 frames)
{
    u64 first = 0;
    u32 remaining = frames;
    while (remaining > 0)
    {
        Stamp_Entry e;
        if (!stamp_peek(&c->stamps, &e))
        {
            if (first == 0)
                first = c->next_read_stamp;
            c->next_read_stamp += frames_to_ns(remaining, c->out_rate);
            return first;
        }
        u32 left_in_entry = e.frames - c->head_consumed;
        if (first == 0)
            first = e.stamp + frames_to_ns(c->head_consumed, c->out_rate);
        u32 take = remaining < left_in_entry ? remaining : left_in_entry;
        c->head_consumed += take;
        remaining -= take;
        if (c->head_consumed == e.frames)
        {
            stamp_pop(&c->stamps);
            c->head_consumed = 0;
            c->next_read_stamp = e.stamp + frames_to_ns(e.frames, c->out_rate);
        }
        else
            c->next_read_stamp = e.stamp + frames_to_ns(c->head_consumed, c->out_rate);
    }
    return first;
}

Mel_AudioCapture_Read mel_audiocapture_read_ex(Mel_AudioCapture* c, f32* interleaved_dst, u32 max_frames)
{
    assert(c != NULL);
    assert(interleaved_dst != NULL);

    Mel_AudioCapture_Read out = { 0 };
    out.frames = mel_pcm_ring_read(c->ring, interleaved_dst, max_frames);
    if (out.frames > 0)
    {
        out.timestamp_ns = read_stamp_advance(c, out.frames);
        atomic_fetch_and_explicit(&c->live_bits, ~MEL_AUDIOCAPTURE_WARN_OVERRUN, memory_order_release);
    }
    return out;
}

u32 mel_audiocapture_read(Mel_AudioCapture* c, f32* interleaved_dst, u32 max_frames) { return mel_audiocapture_read_ex(c, interleaved_dst, max_frames).frames; }

u32 mel_audiocapture_available(const Mel_AudioCapture* c)
{
    assert(c != NULL);
    return mel_pcm_ring_read_available(c->ring);
}

Mel_AudioCapture_Status mel_audiocapture_status(const Mel_AudioCapture* c)
{
    assert(c != NULL);
    u32 bits = atomic_load_explicit((_Atomic(u32)*)&c->live_bits, memory_order_acquire);
    if (bits & MEL_AUDIOCAPTURE_RESULT_LOST)
        return MEL_AUDIOCAPTURE_ERROR | bits;
    if (bits != 0)
        return MEL_AUDIOCAPTURE_WARNED | bits;
    return MEL_AUDIOCAPTURE_OK;
}

Mel_AudioCapture_Granted mel_audiocapture_granted(const Mel_AudioCapture* c)
{
    assert(c != NULL);
    return c->granted_;
}

u64 mel_audiocapture_dropped_frames(const Mel_AudioCapture* c)
{
    assert(c != NULL);
    return atomic_load_explicit((_Atomic(u64)*)&c->dropped, memory_order_relaxed);
}

void mel_audiocapture_close(Mel_AudioCapture* c)
{
    if (!c)
        return;
    mel_audioin__close(c->device, c);
    if (c->ring)
        mel_pcm_ring_destroy(c->ring);
    if (c->stamps.items)
        mel_dealloc(c->alloc, c->stamps.items);
    if (c->deint)
        mel_dealloc(c->alloc, c->deint);
    if (c->hist)
        mel_dealloc(c->alloc, c->hist);
    if (c->resampled)
        mel_dealloc(c->alloc, c->resampled);
    if (c->remixed)
        mel_dealloc(c->alloc, c->remixed);
    if (c->inter)
        mel_dealloc(c->alloc, c->inter);
    if (c->planes)
        mel_dealloc(c->alloc, c->planes);
    if (c->converted)
        mel_dealloc(c->alloc, c->converted);
    if (c->remix_planes)
        mel_dealloc(c->alloc, c->remix_planes);
    mel_dealloc(c->alloc, c);
}
