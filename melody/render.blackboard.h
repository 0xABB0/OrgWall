#ifndef MEL_RENDER_BLACKBOARD_H
#define MEL_RENDER_BLACKBOARD_H

#include "core.types.h"
#include "allocator.h"
#include "string.str8.fwd.h"

typedef struct Mel_Render_Blackboard Mel_Render_Blackboard;

struct Mel_Render_Blackboard {
    u64* keys;
    void** values;
    u32* sizes;
    u32 count;
    u32 capacity;
    const Mel_Alloc* alloc;
};

typedef struct {
    const Mel_Alloc* alloc;
    u32 initial_capacity;
} Mel_Render_Blackboard_Opt;

void mel_render_blackboard_init_opt(Mel_Render_Blackboard* bb, Mel_Render_Blackboard_Opt opt);
#define mel_render_blackboard_init(bb, ...) mel_render_blackboard_init_opt((bb), (Mel_Render_Blackboard_Opt){__VA_ARGS__})

void mel_render_blackboard_shutdown(Mel_Render_Blackboard* bb);

void mel_render_blackboard_set(Mel_Render_Blackboard* bb, str8 name, const void* data, u32 size);
void* mel_render_blackboard_get(Mel_Render_Blackboard* bb, str8 name);
bool mel_render_blackboard_has(Mel_Render_Blackboard* bb, str8 name);
void mel_render_blackboard_clear(Mel_Render_Blackboard* bb);

#endif
