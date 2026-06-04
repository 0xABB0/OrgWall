#include <vibration/ffb.h>
#include <vibration/provider.h>

#include "vibration_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.slotmap/slotmap.h>
#include <collection.array/array.h>
#include <log/log.h>

#include <string.h>

typedef struct
{
    Mel_Vib_Device    device;
    u32               provider_idx;
    u64               stable_id;
    u64               token;
    Mel_Vib_FF_Effect effect;
    Mel_Vib_FF_Caps   caps;
    bool              playing;
    bool              paused;
    u32               loops_remaining;
} FF_Slot;

typedef struct
{
    bool        initialized;
    Mel_SlotMap effects;
    Mel_Array(Mel_Vib_FF_Slot) live;
} FF;

static FF gff;

static void ff_ensure(void)
{
    if (gff.initialized)
        return;
    const Mel_Alloc* alloc = mel_vib__alloc();
    if (!alloc)
        alloc = mel_alloc_heap();
    mel_slotmap_init(&gff.effects, alloc, .item_size = sizeof(FF_Slot), .initial_capacity = 4);
    mel_array_init(&gff.live, alloc);
    gff.initialized = true;
}

static FF_Slot* ff_slot(Mel_Vib_FF_Slot s) { return gff.initialized ? (FF_Slot*)mel_slotmap_get(&gff.effects, s.h) : NULL; }

static void live_remove(Mel_Vib_FF_Slot s)
{
    for (usize i = 0; i < gff.live.count; i++)
    {
        if (gff.live.items[i].h.index == s.h.index && gff.live.items[i].h.generation == s.h.generation)
        {
            gff.live.items[i] = gff.live.items[gff.live.count - 1];
            gff.live.count--;
            return;
        }
    }
}

static bool device_provider(Mel_Vib_Device d, Mel_Vib_Device_Slot** out_ds, Mel_Vib_Provider_Entry** out_prov)
{
    Mel_Vib_Device_Slot* ds = mel_vib__device_slot(d.h);
    if (!ds)
        return false;
    Mel_Vib_Provider_Entry* prov = mel_vib__provider(ds->provider_idx);
    if (!prov)
        return false;
    *out_ds = ds;
    *out_prov = prov;
    return true;
}

static Mel_Vib_Status fold_status(Mel_Vib_Status a, Mel_Vib_Status b)
{
    Mel_Vib_Status flags = (a | b) & ~MEL_VIB_SEVERITY_MASK;
    return flags ? (flags | MEL_VIB_WARNED) : MEL_VIB_OK;
}

static Mel_Vib_Status lower_effect(const Mel_Vib_FF_Effect* in, const Mel_Vib_FF_Caps* caps, Mel_Vib_FF_Effect* out)
{
    Mel_Vib_Status warn = 0;
    *out = *in;

    if ((caps->effects & out->effect) == 0u)
    {
        if (out->effect == MEL_VIB_FF_EFFECT_CONDITION)
        {
            warn |= MEL_VIB_FF_WARN_CONDITION_DROPPED;
            out->effect = MEL_VIB_FF_EFFECT_RUMBLE;
            out->condition_count = 0;
            out->conditions = NULL;
        }
        else if (out->effect == MEL_VIB_FF_EFFECT_RAMP)
        {
            warn |= MEL_VIB_FF_WARN_RAMP_APPROX;
            out->effect = (caps->effects & MEL_VIB_FF_EFFECT_CONSTANT) ? MEL_VIB_FF_EFFECT_CONSTANT : MEL_VIB_FF_EFFECT_RUMBLE;
            out->constant.magnitude = (in->ramp.start + in->ramp.end) * 0.5f;
        }
        else if (out->effect == MEL_VIB_FF_EFFECT_PERIODIC)
        {
            warn |= MEL_VIB_FF_WARN_WAVEFORM_APPROX;
            out->effect = (caps->effects & MEL_VIB_FF_EFFECT_CONSTANT) ? MEL_VIB_FF_EFFECT_CONSTANT : MEL_VIB_FF_EFFECT_RUMBLE;
            out->constant.magnitude = in->periodic.magnitude;
        }
        else
        {
            out->effect = MEL_VIB_FF_EFFECT_RUMBLE;
        }
    }

    if (out->effect == MEL_VIB_FF_EFFECT_PERIODIC)
    {
        if ((caps->waveforms & out->periodic.waveform) == 0u)
        {
            warn |= MEL_VIB_FF_WARN_WAVEFORM_APPROX;
            if (caps->waveforms & MEL_VIB_FF_WAVE_SINE)
                out->periodic.waveform = MEL_VIB_FF_WAVE_SINE;
            else if (caps->waveforms)
                out->periodic.waveform = (u32)(caps->waveforms & (~caps->waveforms + 1u));
        }
        if (caps->max_frequency_hz > 0.0f && out->periodic.frequency_hz > caps->max_frequency_hz)
        {
            out->periodic.frequency_hz = caps->max_frequency_hz;
            warn |= MEL_VIB_FF_WARN_FREQUENCY_CLAMPED;
        }
        if (caps->min_frequency_hz > 0.0f && out->periodic.frequency_hz < caps->min_frequency_hz)
        {
            out->periodic.frequency_hz = caps->min_frequency_hz;
            warn |= MEL_VIB_FF_WARN_FREQUENCY_CLAMPED;
        }
    }

    if (out->effect == MEL_VIB_FF_EFFECT_CONDITION && out->condition_count > 0 && out->conditions)
    {
        for (u32 i = 0; i < out->condition_count; i++)
        {
            if ((caps->conditions & out->conditions[i].kind) == 0u)
            {
                warn |= MEL_VIB_FF_WARN_CONDITION_DROPPED;
                break;
            }
        }
    }

    if (!caps->envelope && (in->envelope.attack_s > 0.0f || in->envelope.fade_s > 0.0f))
    {
        warn |= MEL_VIB_FF_WARN_ENVELOPE_DROPPED;
        out->envelope = (Mel_Vib_FF_Envelope){ 0 };
    }

    u32 want_axes = 1;
    if (out->direction.encoding == MEL_VIB_FF_DIR_CARTESIAN)
        want_axes = 3;
    else if (out->direction.encoding == MEL_VIB_FF_DIR_SPHERICAL)
        want_axes = 2;
    if (caps->direction_axes > 0 && want_axes > caps->direction_axes)
    {
        warn |= MEL_VIB_FF_WARN_AXES_REDUCED;
        if (out->direction.encoding == MEL_VIB_FF_DIR_CARTESIAN && caps->direction_axes < 3)
            warn |= MEL_VIB_FF_WARN_DIRECTION_FLATTENED;
    }

    return warn;
}

bool mel_vib_ff_supported(Mel_Vib_Device d)
{
    Mel_Vib_Device_Slot*    ds;
    Mel_Vib_Provider_Entry* prov;
    if (!device_provider(d, &ds, &prov))
        return false;
    if (!prov->desc.ff_query)
        return false;
    Mel_Vib_FF_Caps caps;
    memset(&caps, 0, sizeof caps);
    if (!prov->desc.ff_query(prov->desc.user, ds->stable_id, &caps))
        return false;
    return caps.present;
}

Mel_Vib_FF_Caps_Result mel_vib_ff_caps(Mel_Vib_Device d)
{
    Mel_Vib_FF_Caps_Result r;
    memset(&r, 0, sizeof r);
    Mel_Vib_Device_Slot*    ds;
    Mel_Vib_Provider_Entry* prov;
    if (!device_provider(d, &ds, &prov))
    {
        mel_log_error("vibration", "ff_caps on dead device handle");
        r.status = MEL_VIB_ERROR;
        return r;
    }
    if (!prov->desc.ff_query || !prov->desc.ff_query(prov->desc.user, ds->stable_id, &r.value))
        memset(&r.value, 0, sizeof r.value);
    r.status = MEL_VIB_OK;
    return r;
}

Mel_Vib_FF_Upload_Result mel_vib_ff_upload(Mel_Vib_Device d, const Mel_Vib_FF_Effect* effect)
{
    Mel_Vib_FF_Upload_Result r = { .value = MEL_VIB_FF_SLOT_NULL, .status = MEL_VIB_ERROR };
    if (!mel_vib__ready() || !effect)
    {
        mel_log_error("vibration", "ff_upload before init or with null effect");
        return r;
    }
    Mel_Vib_Device_Slot*    ds;
    Mel_Vib_Provider_Entry* prov;
    if (!device_provider(d, &ds, &prov))
    {
        mel_log_error("vibration", "ff_upload on dead device handle");
        return r;
    }
    if (!prov->desc.ff_query || !prov->desc.ff_upload)
    {
        mel_log_error("vibration", "device has no force-feedback provider");
        return r;
    }
    Mel_Vib_FF_Caps caps;
    memset(&caps, 0, sizeof caps);
    if (!prov->desc.ff_query(prov->desc.user, ds->stable_id, &caps) || !caps.present)
    {
        mel_log_error("vibration", "device reports no force-feedback");
        return r;
    }

    ff_ensure();

    if (caps.max_effects > 0 && mel_slotmap_count(&gff.effects) >= caps.max_effects)
    {
        mel_log_error("vibration", "force-feedback effect slots exhausted (max=%u)", caps.max_effects);
        return r;
    }

    Mel_Vib_FF_Effect lowered;
    Mel_Vib_Status    warn = lower_effect(effect, &caps, &lowered);

    FF_Slot slot;
    memset(&slot, 0, sizeof slot);
    slot.device = d;
    slot.provider_idx = ds->provider_idx;
    slot.stable_id = ds->stable_id;
    slot.token = mel_vib__next_token();
    slot.effect = lowered;
    slot.caps = caps;
    slot.loops_remaining = lowered.loop;

    Mel_SlotMap_Handle h = mel_slotmap_insert(&gff.effects, &slot);

    Mel_Vib_FF_Lowered low = { .effect = lowered, .caps = caps };
    Mel_Vib_Status     sub = prov->desc.ff_upload(prov->desc.user, ds->stable_id, slot.token, &low);
    if (mel_vib_failed(sub))
    {
        mel_slotmap_remove(&gff.effects, h);
        r.status = sub;
        return r;
    }

    Mel_Vib_FF_Slot fh = { h };
    mel_array_push(&gff.live, fh);
    r.value = fh;
    r.status = fold_status(warn, sub);
    return r;
}

Mel_Vib_Status mel_vib_ff_update(Mel_Vib_FF_Slot s, const Mel_Vib_FF_Effect* effect)
{
    FF_Slot* fs = ff_slot(s);
    if (!fs || !effect)
    {
        mel_log_error("vibration", "ff_update on dead slot or null effect");
        return MEL_VIB_ERROR;
    }
    Mel_Vib_Provider_Entry* prov = mel_vib__provider(fs->provider_idx);
    if (!prov || !prov->desc.ff_update)
    {
        mel_log_error("vibration", "provider does not support ff_update");
        return MEL_VIB_ERROR;
    }
    Mel_Vib_FF_Effect lowered;
    Mel_Vib_Status    warn = lower_effect(effect, &fs->caps, &lowered);
    fs->effect = lowered;
    Mel_Vib_FF_Lowered low = { .effect = lowered, .caps = fs->caps };
    Mel_Vib_Status     sub = prov->desc.ff_update(prov->desc.user, fs->stable_id, fs->token, &low);
    if (mel_vib_failed(sub))
        return sub;
    return fold_status(warn, sub);
}

Mel_Vib_Status mel_vib_ff_start(Mel_Vib_FF_Slot s, u32 loop)
{
    FF_Slot* fs = ff_slot(s);
    if (!fs)
    {
        mel_log_error("vibration", "ff_start on dead slot");
        return MEL_VIB_ERROR;
    }
    Mel_Vib_Provider_Entry* prov = mel_vib__provider(fs->provider_idx);
    if (!prov || !prov->desc.ff_start)
    {
        mel_log_error("vibration", "provider does not support ff_start");
        return MEL_VIB_ERROR;
    }
    Mel_Vib_Status sub = prov->desc.ff_start(prov->desc.user, fs->stable_id, fs->token, loop);
    if (mel_vib_failed(sub))
        return sub;
    fs->playing = true;
    fs->paused = false;
    fs->loops_remaining = loop;
    return sub;
}

Mel_Vib_Status mel_vib_ff_stop(Mel_Vib_FF_Slot s)
{
    FF_Slot* fs = ff_slot(s);
    if (!fs)
    {
        mel_log_error("vibration", "ff_stop on dead slot");
        return MEL_VIB_ERROR;
    }
    Mel_Vib_Provider_Entry* prov = mel_vib__provider(fs->provider_idx);
    if (prov && prov->desc.ff_stop)
        prov->desc.ff_stop(prov->desc.user, fs->stable_id, fs->token);
    fs->playing = false;
    fs->paused = false;
    return MEL_VIB_OK;
}

Mel_Vib_Status mel_vib_ff_pause(Mel_Vib_FF_Slot s)
{
    FF_Slot* fs = ff_slot(s);
    if (!fs)
    {
        mel_log_error("vibration", "ff_pause on dead slot");
        return MEL_VIB_ERROR;
    }
    if (fs->paused)
        return MEL_VIB_OK;
    if (!fs->playing)
    {
        mel_log_error("vibration", "ff_pause on a stopped effect");
        return MEL_VIB_ERROR;
    }
    Mel_Vib_Provider_Entry* prov = mel_vib__provider(fs->provider_idx);
    if (prov && prov->desc.ff_pause)
    {
        Mel_Vib_Status sub = prov->desc.ff_pause(prov->desc.user, fs->stable_id, fs->token);
        if (mel_vib_failed(sub))
            return sub;
        fs->paused = true;
        return sub;
    }
    if (prov && prov->desc.ff_stop)
        prov->desc.ff_stop(prov->desc.user, fs->stable_id, fs->token);
    fs->paused = true;
    return MEL_VIB_OK;
}

Mel_Vib_Status mel_vib_ff_resume(Mel_Vib_FF_Slot s)
{
    FF_Slot* fs = ff_slot(s);
    if (!fs)
    {
        mel_log_error("vibration", "ff_resume on dead slot");
        return MEL_VIB_ERROR;
    }
    if (!fs->paused)
        return MEL_VIB_OK;
    Mel_Vib_Provider_Entry* prov = mel_vib__provider(fs->provider_idx);
    if (!prov)
        return MEL_VIB_ERROR;
    if (prov->desc.ff_resume)
    {
        Mel_Vib_Status sub = prov->desc.ff_resume(prov->desc.user, fs->stable_id, fs->token);
        if (mel_vib_failed(sub))
            return sub;
        fs->paused = false;
        return sub;
    }
    if (prov->desc.ff_start)
        prov->desc.ff_start(prov->desc.user, fs->stable_id, fs->token, fs->loops_remaining);
    fs->paused = false;
    return MEL_VIB_OK;
}

void mel_vib_ff_release(Mel_Vib_FF_Slot s)
{
    FF_Slot* fs = ff_slot(s);
    if (!fs)
        return;
    Mel_Vib_Provider_Entry* prov = mel_vib__provider(fs->provider_idx);
    if (prov)
    {
        if (fs->playing && prov->desc.ff_stop)
            prov->desc.ff_stop(prov->desc.user, fs->stable_id, fs->token);
        if (prov->desc.ff_release)
            prov->desc.ff_release(prov->desc.user, fs->stable_id, fs->token);
    }
    live_remove(s);
    mel_slotmap_remove(&gff.effects, s.h);
}

Mel_Vib_FF_State_Result mel_vib_ff_status(Mel_Vib_FF_Slot s)
{
    Mel_Vib_FF_State_Result r;
    memset(&r, 0, sizeof r);
    FF_Slot* fs = ff_slot(s);
    if (!fs)
    {
        r.status = MEL_VIB_ERROR;
        return r;
    }
    r.value.active = true;
    r.value.playing = fs->playing && !fs->paused;
    r.value.paused = fs->paused;
    r.value.loops_remaining = fs->loops_remaining;
    r.status = MEL_VIB_OK;
    return r;
}

bool mel_vib_ff_alive(Mel_Vib_FF_Slot s) { return gff.initialized && mel_slotmap_alive(&gff.effects, s.h); }

Mel_Vib_Status mel_vib_ff_set_gain(Mel_Vib_Device d, f32 gain)
{
    Mel_Vib_Device_Slot*    ds;
    Mel_Vib_Provider_Entry* prov;
    if (!device_provider(d, &ds, &prov))
    {
        mel_log_error("vibration", "ff_set_gain on dead device handle");
        return MEL_VIB_ERROR;
    }
    if (gain < 0.0f)
        gain = 0.0f;
    if (gain > 1.0f)
        gain = 1.0f;
    Mel_Vib_FF_Caps caps;
    memset(&caps, 0, sizeof caps);
    bool has = prov->desc.ff_query && prov->desc.ff_query(prov->desc.user, ds->stable_id, &caps) && caps.present;
    if (!has || !caps.gain || !prov->desc.ff_set_gain)
        return MEL_VIB_FF_WARN_GAIN_QUANTIZED | MEL_VIB_WARNED;
    return prov->desc.ff_set_gain(prov->desc.user, ds->stable_id, gain);
}

Mel_Vib_Status mel_vib_ff_set_autocenter(Mel_Vib_Device d, bool enabled) { return mel_vib_ff_set_autocenter_strength(d, enabled ? 1.0f : 0.0f); }

Mel_Vib_Status mel_vib_ff_set_autocenter_strength(Mel_Vib_Device d, f32 strength)
{
    Mel_Vib_Device_Slot*    ds;
    Mel_Vib_Provider_Entry* prov;
    if (!device_provider(d, &ds, &prov))
    {
        mel_log_error("vibration", "ff_set_autocenter on dead device handle");
        return MEL_VIB_ERROR;
    }
    if (strength < 0.0f)
        strength = 0.0f;
    if (strength > 1.0f)
        strength = 1.0f;
    Mel_Vib_FF_Caps caps;
    memset(&caps, 0, sizeof caps);
    bool has = prov->desc.ff_query && prov->desc.ff_query(prov->desc.user, ds->stable_id, &caps) && caps.present;
    if (!has || !caps.autocenter || !prov->desc.ff_set_autocenter)
        return MEL_VIB_FF_WARN_AUTOCENTER_ABSENT | MEL_VIB_WARNED;
    Mel_Vib_Status st = prov->desc.ff_set_autocenter(prov->desc.user, ds->stable_id, strength > 0.0f, strength);
    if (!mel_vib_failed(st) && !caps.autocenter_continuous && strength > 0.0f && strength < 1.0f)
        st |= MEL_VIB_FF_WARN_AUTOCENTER_QUANTIZED | MEL_VIB_WARNED;
    return st;
}

void mel_vib_ff_stop_all(Mel_Vib_Device d)
{
    if (!gff.initialized)
        return;
    for (usize i = 0; i < gff.live.count; i++)
    {
        FF_Slot* fs = ff_slot(gff.live.items[i]);
        if (!fs || !mel_vib_equal(fs->device, d) || !fs->playing)
            continue;
        Mel_Vib_Provider_Entry* prov = mel_vib__provider(fs->provider_idx);
        if (prov && prov->desc.ff_stop)
            prov->desc.ff_stop(prov->desc.user, fs->stable_id, fs->token);
        fs->playing = false;
        fs->paused = false;
    }
}

void mel_vib_ff__shutdown(void)
{
    if (!gff.initialized)
        return;
    for (usize i = 0; i < gff.live.count; i++)
    {
        FF_Slot* fs = ff_slot(gff.live.items[i]);
        if (!fs)
            continue;
        Mel_Vib_Provider_Entry* prov = mel_vib__provider(fs->provider_idx);
        if (!prov)
            continue;
        if (fs->playing && prov->desc.ff_stop)
            prov->desc.ff_stop(prov->desc.user, fs->stable_id, fs->token);
        if (prov->desc.ff_release)
            prov->desc.ff_release(prov->desc.user, fs->stable_id, fs->token);
    }
    mel_array_free(&gff.live);
    mel_slotmap_free(&gff.effects);
    memset(&gff, 0, sizeof gff);
}
