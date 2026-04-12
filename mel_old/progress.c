#include "progress.h"
#include "collection.array.h"
#include "async.signal.h"

static f32 mel__progress_clamp01(f32 value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

void mel_progress_init(Mel_Progress* progress, const Mel_Alloc* alloc)
{
    assert(progress != NULL);
    assert(alloc != NULL);

    mel_array_init(&progress->sources, alloc);
}

void mel_progress_destroy(Mel_Progress* progress)
{
    assert(progress != NULL);
    mel_array_free(&progress->sources);
}

void mel_progress_clear(Mel_Progress* progress)
{
    assert(progress != NULL);
    mel_array_clear(&progress->sources);
}

void mel_progress_add_custom(Mel_Progress* progress, Mel_Progress_Fn fn, void* user, f32 weight)
{
    assert(progress != NULL);
    assert(fn != NULL);
    assert(weight > 0.0f);

    mel_array_push(&progress->sources, ((Mel_Progress_Source){
        .kind = MEL_PROGRESS_SOURCE_CUSTOM,
        .weight = weight,
        .custom = {
            .fn = fn,
            .user = user,
        },
    }));
}

void mel_progress_add_counter(Mel_Progress* progress, Mel_Counter* counter, i64 total, f32 weight)
{
    assert(progress != NULL);
    assert(counter != NULL);
    assert(total > 0);
    assert(weight > 0.0f);

    mel_array_push(&progress->sources, ((Mel_Progress_Source){
        .kind = MEL_PROGRESS_SOURCE_COUNTER,
        .weight = weight,
        .counter = {
            .counter = counter,
            .total = total,
        },
    }));
}

void mel_progress_add_child(Mel_Progress* progress, const Mel_Progress* child, f32 weight)
{
    assert(progress != NULL);
    assert(child != NULL);
    assert(weight > 0.0f);

    mel_array_push(&progress->sources, ((Mel_Progress_Source){
        .kind = MEL_PROGRESS_SOURCE_CHILD,
        .weight = weight,
        .child = child,
    }));
}

static Mel_Progress_Status mel__progress_source_state(const Mel_Progress_Source* source)
{
    switch (source->kind) {
    case MEL_PROGRESS_SOURCE_CUSTOM:
        return source->custom.fn(source->custom.user);

    case MEL_PROGRESS_SOURCE_COUNTER: {
        i32 state = atomic_load_explicit(&source->counter.counter->signal.state, memory_order_acquire);
        u16 remaining = mel__signal_counter(state);
        i64 completed = source->counter.total - (i64)remaining;
        f32 frac = (f32)completed / (f32)source->counter.total;
        return (Mel_Progress_Status){
            .value = mel__progress_clamp01(frac),
            .failed = false,
        };
    }

    case MEL_PROGRESS_SOURCE_CHILD:
        return mel_progress_state(source->child);
    }

    assert(false);
    return (Mel_Progress_Status){ .value = 0.0f, .failed = true };
}

Mel_Progress_Status mel_progress_state(const Mel_Progress* progress)
{
    assert(progress != NULL);

    if (progress->sources.count == 0) {
        return (Mel_Progress_Status){
            .value = 1.0f,
            .failed = false,
        };
    }

    f32 total_weight = 0.0f;
    f32 weighted_value = 0.0f;
    bool failed = false;

    for (usize i = 0; i < progress->sources.count; i++) {
        const Mel_Progress_Source* source = &progress->sources.items[i];
        Mel_Progress_Status state = mel__progress_source_state(source);
        total_weight += source->weight;
        weighted_value += mel__progress_clamp01(state.value) * source->weight;
        failed = failed || state.failed;
    }

    assert(total_weight > 0.0f);
    return (Mel_Progress_Status){
        .value = mel__progress_clamp01(weighted_value / total_weight),
        .failed = failed,
    };
}

f32 mel_progress_value(const Mel_Progress* progress)
{
    return mel_progress_state(progress).value;
}

bool mel_progress_is_ready(const Mel_Progress* progress, f32 threshold)
{
    assert(progress != NULL);
    assert(threshold >= 0.0f);
    assert(threshold <= 1.0f);

    Mel_Progress_Status state = mel_progress_state(progress);
    return !state.failed && state.value >= threshold;
}

bool mel_progress_is_failed(const Mel_Progress* progress)
{
    return mel_progress_state(progress).failed;
}
