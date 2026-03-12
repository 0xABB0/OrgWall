#include "stage.h"

#include <assert.h>

static Mel_Stage* s_stage_active;
static Mel_Stage* s_stage_dirty;

static void mel__stage_queue(Mel_Stage* stage)
{
    if (stage->queued)
        return;

    stage->queued = true;
    stage->next_dirty = s_stage_dirty;
    s_stage_dirty = stage;
}

static void mel__stage_active_push(Mel_Stage* stage)
{
    assert(stage != nullptr);
    assert(stage->next_active == nullptr);

    stage->next_active = s_stage_active;
    s_stage_active = stage;
}

static void mel__stage_active_remove(Mel_Stage* stage)
{
    Mel_Stage** it = &s_stage_active;
    while (*it) {
        if (*it == stage) {
            *it = stage->next_active;
            stage->next_active = nullptr;
            return;
        }
        it = &(*it)->next_active;
    }

    assert(false);
}

static void mel__stage_apply(Mel_Stage* stage)
{
    if (stage->want_attached && !stage->attached) {
        mel__stage_active_push(stage);
        stage->attached = true;
    }

    if (stage->attached && !stage->enabled && stage->want_enabled) {
        stage->enabled = true;
        if (stage->on_start)
            stage->on_start(stage, stage->user);
    }

    if (stage->attached && stage->enabled && !stage->want_enabled) {
        if (stage->on_end)
            stage->on_end(stage, stage->user);
        stage->enabled = false;
    }

    if (!stage->want_attached && stage->attached) {
        if (stage->enabled) {
            if (stage->on_end)
                stage->on_end(stage, stage->user);
            stage->enabled = false;
        }

        mel__stage_active_remove(stage);
        stage->attached = false;
    }

    if (!stage->want_attached)
        stage->want_enabled = false;
}

static void mel__stage_flush_dirty(void)
{
    while (s_stage_dirty) {
        Mel_Stage* batch = s_stage_dirty;
        s_stage_dirty = nullptr;

        while (batch) {
            Mel_Stage* next = batch->next_dirty;
            batch->next_dirty = nullptr;
            batch->queued = false;
            mel__stage_apply(batch);
            batch = next;
        }
    }
}

void mel_stage_init_opt(Mel_Stage* stage, Mel_Stage_Opt opt)
{
    assert(stage != nullptr);

    *stage = (Mel_Stage){
        .on_start = opt.on_start,
        .on_end = opt.on_end,
        .on_tick = opt.on_tick,
        .user = opt.user,
        .want_enabled = opt.start_enabled,
    };
}

void mel_stage_shutdown(Mel_Stage* stage)
{
    assert(stage != nullptr);
    assert(!stage->queued);
    assert(!stage->attached);
    assert(!stage->enabled);
    assert(!stage->want_attached);
    assert(!stage->want_enabled);
    assert(stage->next_active == nullptr);
    assert(stage->next_dirty == nullptr);

    *stage = (Mel_Stage){0};
}

void mel_stage_attach(Mel_Stage* stage)
{
    assert(stage != nullptr);

    stage->want_attached = true;
    mel__stage_queue(stage);
}

void mel_stage_detach(Mel_Stage* stage)
{
    assert(stage != nullptr);

    stage->want_attached = false;
    stage->want_enabled = false;
    mel__stage_queue(stage);
}

void mel_stage_enable(Mel_Stage* stage)
{
    assert(stage != nullptr);

    stage->want_attached = true;
    stage->want_enabled = true;
    mel__stage_queue(stage);
}

void mel_stage_disable(Mel_Stage* stage)
{
    assert(stage != nullptr);

    stage->want_enabled = false;
    mel__stage_queue(stage);
}

bool mel_stage_is_attached(const Mel_Stage* stage)
{
    assert(stage != nullptr);
    return stage->attached;
}

bool mel_stage_is_enabled(const Mel_Stage* stage)
{
    assert(stage != nullptr);
    return stage->enabled;
}

void mel_stage_tick(void)
{
    mel__stage_flush_dirty();

    for (Mel_Stage* stage = s_stage_active; stage; stage = stage->next_active) {
        if (stage->enabled && stage->on_tick)
            stage->on_tick(stage, stage->user);
    }

    mel__stage_flush_dirty();
}

void mel_stage_shutdown_all(void)
{
    while (s_stage_dirty) {
        Mel_Stage* batch = s_stage_dirty;
        s_stage_dirty = nullptr;

        while (batch) {
            Mel_Stage* next = batch->next_dirty;
            batch->next_dirty = nullptr;
            batch->queued = false;
            batch->want_attached = false;
            batch->want_enabled = false;
            batch = next;
        }
    }

    while (s_stage_active) {
        Mel_Stage* stage = s_stage_active;
        stage->want_attached = false;
        stage->want_enabled = false;
        mel__stage_queue(stage);
        mel__stage_flush_dirty();
    }

    assert(s_stage_dirty == nullptr);
}

static void mel__loading_stage_tick(Mel_Stage* base, void* user)
{
    Mel_Loading_Stage* stage = user;
    assert(base == &stage->stage);

    if (!mel_progress_is_ready(stage->progress, stage->ready_at))
        return;

    if (stage->next && stage->attach_next)
        mel_stage_attach(stage->next);
    if (stage->next && stage->enable_next)
        mel_stage_enable(stage->next);
    if (stage->detach_self)
        mel_stage_detach(base);
    else if (stage->disable_self)
        mel_stage_disable(base);
}

void mel_loading_stage_init_opt(Mel_Loading_Stage* stage, Mel_Loading_Stage_Opt opt)
{
    assert(stage != nullptr);
    assert(opt.progress != nullptr);
    assert(opt.ready_at >= 0.0f);
    assert(opt.ready_at <= 1.0f);

    *stage = (Mel_Loading_Stage){
        .progress = opt.progress,
        .next = opt.next,
        .ready_at = opt.ready_at,
        .attach_next = opt.attach_next || (!opt.attach_next && !opt.enable_next),
        .enable_next = opt.enable_next || (!opt.attach_next && !opt.enable_next),
        .disable_self = opt.disable_self || (!opt.disable_self && !opt.detach_self),
        .detach_self = opt.detach_self,
    };

    mel_stage_init(&stage->stage,
        .on_tick = mel__loading_stage_tick,
        .user = stage,
        .start_enabled = true);
}

void mel_loading_stage_shutdown(Mel_Loading_Stage* stage)
{
    assert(stage != nullptr);
    mel_stage_shutdown(&stage->stage);
    *stage = (Mel_Loading_Stage){0};
}
