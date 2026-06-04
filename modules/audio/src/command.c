#include "audio_internal.h"

#include <core/types.h>
#include <collection/array.h>
#include <collection/slotmap.fwd.h>
#include <log/log.h>

void mel_audio__command_push(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    assert(eng != NULL);
    assert(cmd != NULL);
    assert(cmd->apply != NULL);

    mel_spinlock_lock(&eng->command_lock);
    mel_array_push(&eng->commands, *cmd);
    mel_spinlock_unlock(&eng->command_lock);
}

static void mel_audio__fade_arm(Mel_Audio__Scalar_Fade* fade, f64 from, f64 to, f64 seconds, f64 clock, u32 samplerate)
{
    fade->active = 1u;
    fade->kind = MEL_AUDIO__FADER_LINEAR;
    fade->from = from;
    fade->to = to;
    fade->start_clock = clock;
    fade->duration_frames = seconds * (f64)samplerate;
    fade->period_frames = 0.0;
    fade->on_complete_pause = 0u;
    fade->on_complete_stop = 0u;
}

void mel_audio__cmd_create(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice payload = { 0 };
    payload.self = cmd->handle;
    payload.source = cmd->source;
    payload.instance = cmd->instance;
    payload.tail = cmd->tail;
    payload.has_tail = 0u;
    payload.cursor = 0.0;
    payload.play_speed = cmd->d0;
    payload.volume = cmd->f0;
    payload.pan = cmd->f1;
    payload.flags = cmd->u0;
    mel_audio__pan_gains(payload.pan, &payload.gain_l, &payload.gain_r);

    if (!mel_audio__voice_activate(eng, cmd->handle, &payload))
    {
        mel_audio__voice_remove_reserved(eng, cmd->handle, cmd->source, cmd->instance, cmd->tail);
        mel_audio__end_future_resolve(eng, cmd->handle, MEL_FUTURE_BROKEN);
    }
}

void mel_audio__cmd_set_volume(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice* v = mel_audio__voice_get(&eng->voices, cmd->handle);
    if (v == NULL)
    {
        mel_log_debug("audio", "set_volume on stale voice");
        return;
    }
    v->volume = cmd->f0;
    v->fade_volume.active = 0u;
}

void mel_audio__cmd_set_pan(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice* v = mel_audio__voice_get(&eng->voices, cmd->handle);
    if (v == NULL)
    {
        mel_log_debug("audio", "set_pan on stale voice");
        return;
    }
    v->pan = cmd->f0;
    mel_audio__pan_gains(v->pan, &v->gain_l, &v->gain_r);
    v->fade_pan.active = 0u;
}

void mel_audio__cmd_set_speed(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice* v = mel_audio__voice_get(&eng->voices, cmd->handle);
    if (v == NULL)
    {
        mel_log_debug("audio", "set_play_speed on stale voice");
        return;
    }
    assert(cmd->d0 > 0.0);
    v->play_speed = cmd->d0;
    v->fade_speed.active = 0u;
}

void mel_audio__cmd_set_paused(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice* v = mel_audio__voice_get(&eng->voices, cmd->handle);
    if (v == NULL)
    {
        mel_log_debug("audio", "set_paused on stale voice");
        return;
    }
    if (cmd->u0)
        v->flags |= MEL_AUDIO_VOICE_PAUSED;
    else
        v->flags &= ~(u32)MEL_AUDIO_VOICE_PAUSED;
}

void mel_audio__cmd_set_loop(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice* v = mel_audio__voice_get(&eng->voices, cmd->handle);
    if (v == NULL)
    {
        mel_log_debug("audio", "set_looping on stale voice");
        return;
    }
    if (cmd->u0)
        v->flags |= MEL_AUDIO_VOICE_LOOPING;
    else
        v->flags &= ~(u32)MEL_AUDIO_VOICE_LOOPING;
}

void mel_audio__cmd_seek(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice* v = mel_audio__voice_get(&eng->voices, cmd->handle);
    if (v == NULL)
    {
        mel_log_debug("audio", "seek on stale voice");
        return;
    }
    if (v->source != NULL && v->source->seek != NULL)
        v->source->seek(v->source, v->instance, cmd->d0);
    v->cursor = 0.0;
    v->has_tail = 0u;
}

void mel_audio__cmd_stop(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    if (!mel_audio__voice_alive(&eng->voices, cmd->handle))
    {
        mel_log_debug("audio", "stop on stale voice");
        return;
    }
    mel_audio__voice_remove(eng, cmd->handle);
}

void mel_audio__cmd_stop_all(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    MEL_UNUSED(cmd);
    Mel_Audio__Voice_Table* t = &eng->voices;
    while (t->packed.count > 0u)
        mel_audio__voice_remove(eng, mel_array_last(&t->packed).self);
}

void mel_audio__cmd_fade_volume(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice* v = mel_audio__voice_get(&eng->voices, cmd->handle);
    if (v == NULL)
    {
        mel_log_debug("audio", "fade_volume on stale voice");
        return;
    }
    mel_audio__fade_arm(&v->fade_volume, (f64)v->volume, (f64)cmd->f0, cmd->d0, eng->stream_clock, eng->caps.samplerate);
}

void mel_audio__cmd_fade_pan(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice* v = mel_audio__voice_get(&eng->voices, cmd->handle);
    if (v == NULL)
    {
        mel_log_debug("audio", "fade_pan on stale voice");
        return;
    }
    mel_audio__fade_arm(&v->fade_pan, (f64)v->pan, (f64)cmd->f0, cmd->d0, eng->stream_clock, eng->caps.samplerate);
}

void mel_audio__cmd_fade_speed(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice* v = mel_audio__voice_get(&eng->voices, cmd->handle);
    if (v == NULL)
    {
        mel_log_debug("audio", "fade_play_speed on stale voice");
        return;
    }
    mel_audio__fade_arm(&v->fade_speed, v->play_speed, cmd->d0, cmd->d1, eng->stream_clock, eng->caps.samplerate);
}

void mel_audio__cmd_oscillate_volume(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice* v = mel_audio__voice_get(&eng->voices, cmd->handle);
    if (v == NULL)
    {
        mel_log_debug("audio", "oscillate_volume on stale voice");
        return;
    }
    v->fade_volume.active = 1u;
    v->fade_volume.kind = MEL_AUDIO__FADER_OSC;
    v->fade_volume.from = (f64)cmd->f0;
    v->fade_volume.to = (f64)cmd->f1;
    v->fade_volume.start_clock = eng->stream_clock;
    v->fade_volume.period_frames = cmd->d0 * (f64)eng->caps.samplerate;
    v->fade_volume.on_complete_pause = 0u;
    v->fade_volume.on_complete_stop = 0u;
}

void mel_audio__cmd_schedule_pause(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice* v = mel_audio__voice_get(&eng->voices, cmd->handle);
    if (v == NULL)
    {
        mel_log_debug("audio", "schedule_pause on stale voice");
        return;
    }
    mel_audio__fade_arm(&v->fade_volume, (f64)v->volume, 0.0, cmd->d0, eng->stream_clock, eng->caps.samplerate);
    v->fade_volume.on_complete_pause = 1u;
}

void mel_audio__cmd_schedule_stop(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    Mel_Audio__Voice* v = mel_audio__voice_get(&eng->voices, cmd->handle);
    if (v == NULL)
    {
        mel_log_debug("audio", "schedule_stop on stale voice");
        return;
    }
    mel_audio__fade_arm(&v->fade_volume, (f64)v->volume, 0.0, cmd->d0, eng->stream_clock, eng->caps.samplerate);
    v->fade_volume.on_complete_stop = 1u;
}

void mel_audio__cmd_fade_master(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    mel_audio__fade_arm(&eng->master_fade, (f64)eng->master_volume, (f64)cmd->f0, cmd->d0, eng->stream_clock, eng->caps.samplerate);
}

void mel_audio__end_future_register(Mel_Audio* eng, Mel_SlotMap_Handle handle, Mel_Future* fut)
{
    assert(eng != NULL);
    assert(fut != NULL);
    Mel_Audio__End_Future entry = { .handle = handle, .fut = fut, .resolved = 0u };
    mel_array_push(&eng->end_futures, entry);
}

void mel_audio__end_future_resolve(Mel_Audio* eng, Mel_SlotMap_Handle handle, Mel_Future_Status status)
{
    assert(eng != NULL);
    for (usize i = 0; i < eng->end_futures.count; i++)
    {
        Mel_Audio__End_Future* e = &eng->end_futures.items[i];
        if (e->resolved == 0u && mel_slotmap_handle_pack64(e->handle) == mel_slotmap_handle_pack64(handle))
        {
            e->resolved = 1u;
            mel_future_resolve(e->fut, NULL, status);
        }
    }
}

void mel_audio__end_future_release(Mel_Audio* eng, Mel_Future* fut)
{
    assert(eng != NULL);
    assert(fut != NULL);
    for (usize i = 0; i < eng->end_futures.count; i++)
    {
        Mel_Audio__End_Future* e = &eng->end_futures.items[i];
        if (e->fut == fut)
        {
            if (e->resolved == 0u)
                mel_future_resolve(e->fut, NULL, MEL_FUTURE_BROKEN);
            mel_dealloc(eng->alloc, e->fut);
            usize last = eng->end_futures.count - 1u;
            if (i != last)
                eng->end_futures.items[i] = eng->end_futures.items[last];
            eng->end_futures.count--;
            return;
        }
    }
    mel_log_warn("audio", "voice_end_future_free: future not in registry (double free or foreign future)");
}

void mel_audio__end_futures_free(Mel_Audio* eng)
{
    assert(eng != NULL);
    for (usize i = 0; i < eng->end_futures.count; i++)
    {
        Mel_Audio__End_Future* e = &eng->end_futures.items[i];
        if (e->resolved == 0u)
            mel_future_resolve(e->fut, NULL, MEL_FUTURE_BROKEN);
        mel_dealloc(eng->alloc, e->fut);
    }
    mel_array_free(&eng->end_futures);
}

void mel_audio__cmd_attach_end_future(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    mel_audio__end_future_register(eng, cmd->handle, cmd->fut);
    if (!mel_audio__voice_alive(&eng->voices, cmd->handle))
        mel_audio__end_future_resolve(eng, cmd->handle, MEL_FUTURE_OK);
}

void mel_audio__cmd_release_end_future(Mel_Audio* eng, const Mel_Audio__Command* cmd)
{
    mel_audio__end_future_release(eng, cmd->fut);
}

void mel_audio__commands_drain(Mel_Audio* eng)
{
    assert(eng != NULL);

    mel_spinlock_lock(&eng->command_lock);
    Mel_Audio__Command_Queue front = eng->commands;
    eng->commands = eng->commands_back;
    mel_spinlock_unlock(&eng->command_lock);

    for (usize i = 0; i < front.count; i++)
        front.items[i].apply(eng, &front.items[i]);

    front.count = 0;
    eng->commands_back = front;
}
