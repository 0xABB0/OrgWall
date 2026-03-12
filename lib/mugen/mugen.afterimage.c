#include "mugen.cns.h"
#include <stdlib.h>
#include <string.h>

void mugen_afterimage_record(Mugen_Char_State* state)
{
    if (state->afterimage.time == 0) return;

    if (state->afterimage.record_counter > 0)
    {
        state->afterimage.record_counter--;
        return;
    }
    state->afterimage.record_counter = state->afterimage.timegap - 1;

    u32 max_frames = (u32)state->afterimage.length;
    if (max_frames == 0) max_frames = 20;

    if (state->afterimage.frame_cap < max_frames)
    {
        Mugen_AfterImage_Snap* buf = realloc(state->afterimage.frames, max_frames * sizeof(Mugen_AfterImage_Snap));
        state->afterimage.frames = buf;
        state->afterimage.frame_cap = max_frames;
    }

    Mugen_AfterImage_Snap snap = {
        .pos_x = state->pos_x,
        .pos_y = state->pos_y,
        .facing = state->facing,
        .anim = state->anim,
        .anim_frame_index = state->anim_frame_index,
        .anim_tick = state->anim_tick,
    };

    if (state->afterimage.frame_count < max_frames)
    {
        state->afterimage.frames[state->afterimage.frame_count] = snap;
        state->afterimage.head = state->afterimage.frame_count;
        state->afterimage.frame_count++;
    }
    else
    {
        state->afterimage.head = (state->afterimage.head + 1) % max_frames;
        state->afterimage.frames[state->afterimage.head] = snap;
    }

    if (state->afterimage.time > 0)
        state->afterimage.time--;
}

void mugen_afterimage_free(Mugen_Char_State* state)
{
    free(state->afterimage.frames);
    memset(&state->afterimage, 0, sizeof(state->afterimage));
}

u32 mugen_afterimage_visible_count(Mugen_Char_State* state)
{
    if (state->afterimage.frame_count == 0) return 0;
    i32 gap = state->afterimage.framegap;
    if (gap < 1) gap = 1;
    u32 visible = 0;
    for (u32 i = 0; i < state->afterimage.frame_count; i++)
    {
        if ((i % (u32)gap) == 0) visible++;
    }
    return visible;
}

Mugen_AfterImage_Snap* mugen_afterimage_get(Mugen_Char_State* state, u32 index)
{
    if (state->afterimage.frame_count == 0) return NULL;

    i32 gap = state->afterimage.framegap;
    if (gap < 1) gap = 1;

    u32 max_frames = state->afterimage.frame_count;
    u32 target = 0;
    u32 visible_idx = 0;

    for (u32 i = 0; i < max_frames; i++)
    {
        u32 ring_idx = (state->afterimage.head + max_frames - i) % max_frames;
        if ((i % (u32)gap) == 0)
        {
            if (visible_idx == index)
            {
                target = ring_idx;
                return &state->afterimage.frames[target];
            }
            visible_idx++;
        }
    }
    return NULL;
}
