#include "stage.h"

#include <assert.h>

#include "allocator.h"
#include "string.str8.h"

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

static Mel_Stage_Reg_Entry* mel__stage_registry_find_entry(Mel_Stage_Registry* registry, str8 name)
{
    if (!registry)
        return nullptr;

    for (u32 i = 0; i < registry->count; i++)
    {
        if (str8_equals(registry->items[i].name, name))
            return &registry->items[i];
    }

    return nullptr;
}

void mel_stage_registry_init(Mel_Stage_Registry* registry, const Mel_Alloc* alloc)
{
    assert(registry != nullptr);
    assert(alloc != nullptr);

    *registry = (Mel_Stage_Registry){
        .alloc = alloc,
    };
}

void mel_stage_registry_shutdown(Mel_Stage_Registry* registry)
{
    assert(registry != nullptr);

    for (u32 i = 0; i < registry->count; i++)
    {
        if (registry->items[i].name.data)
            mel_dealloc(registry->alloc, registry->items[i].name.data);
    }

    if (registry->items)
        mel_dealloc(registry->alloc, registry->items);

    *registry = (Mel_Stage_Registry){0};
}

bool mel_stage_registry_add_opt(Mel_Stage_Registry* registry, Mel_Stage_Reg_Opt opt)
{
    assert(registry != nullptr);
    assert(registry->alloc != nullptr);
    assert(opt.stage != nullptr);
    assert(!str8_is_empty(opt.name));

    if (mel__stage_registry_find_entry(registry, opt.name))
        return false;

    if (registry->count >= registry->capacity)
    {
        u32 new_capacity = registry->capacity == 0 ? 8 : registry->capacity * 2;
        usize new_size = sizeof(*registry->items) * new_capacity;
        registry->items = registry->items
            ? mel_realloc(registry->alloc, registry->items, new_size)
            : mel_alloc(registry->alloc, new_size);
        registry->capacity = new_capacity;
    }

    registry->items[registry->count++] = (Mel_Stage_Reg_Entry){
        .name = str8_dup(opt.name, registry->alloc),
        .stage = opt.stage,
        .tags = opt.tags,
    };
    return true;
}

Mel_Stage* mel_stage_registry_find(Mel_Stage_Registry* registry, str8 name)
{
    Mel_Stage_Reg_Entry* entry = mel__stage_registry_find_entry(registry, name);
    return entry ? entry->stage : nullptr;
}

bool mel_stage_registry_attach_named(Mel_Stage_Registry* registry, str8 name)
{
    Mel_Stage* stage = mel_stage_registry_find(registry, name);
    if (!stage)
        return false;

    mel_stage_attach(stage);
    return true;
}

bool mel_stage_registry_detach_named(Mel_Stage_Registry* registry, str8 name)
{
    Mel_Stage* stage = mel_stage_registry_find(registry, name);
    if (!stage)
        return false;

    mel_stage_detach(stage);
    return true;
}

bool mel_stage_registry_enable_named(Mel_Stage_Registry* registry, str8 name)
{
    Mel_Stage* stage = mel_stage_registry_find(registry, name);
    if (!stage)
        return false;

    mel_stage_enable(stage);
    return true;
}

bool mel_stage_registry_disable_named(Mel_Stage_Registry* registry, str8 name)
{
    Mel_Stage* stage = mel_stage_registry_find(registry, name);
    if (!stage)
        return false;

    mel_stage_disable(stage);
    return true;
}

u32 mel_stage_registry_detach_tagged(Mel_Stage_Registry* registry, u32 tags)
{
    assert(registry != nullptr);

    u32 count = 0;
    for (u32 i = 0; i < registry->count; i++)
    {
        if ((registry->items[i].tags & tags) == 0)
            continue;

        mel_stage_detach(registry->items[i].stage);
        count++;
    }

    return count;
}

u32 mel_stage_registry_disable_tagged(Mel_Stage_Registry* registry, u32 tags)
{
    assert(registry != nullptr);

    u32 count = 0;
    for (u32 i = 0; i < registry->count; i++)
    {
        if ((registry->items[i].tags & tags) == 0)
            continue;

        mel_stage_disable(registry->items[i].stage);
        count++;
    }

    return count;
}

bool mel_stage_registry_enable_exclusive(Mel_Stage_Registry* registry, str8 name, u32 within_tags)
{
    Mel_Stage_Reg_Entry* target = mel__stage_registry_find_entry(registry, name);
    if (!target)
        return false;

    for (u32 i = 0; i < registry->count; i++)
    {
        Mel_Stage_Reg_Entry* entry = &registry->items[i];
        if ((entry->tags & within_tags) == 0 || entry == target)
            continue;

        mel_stage_disable(entry->stage);
    }

    mel_stage_enable(target->stage);
    return true;
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
