#include <animation/anim.player.h>
#include <animation/anim.clip.h>
#include <animation/anim.pose.h>
#include <animation/anim.pipeline.h>
#include <animation/anim.skeleton.h>
#include <animation/anim.registry.h>
#include <allocator/allocator.h>
#include <collection.slotmap/slotmap.h>

#include <string.h>
#include <stdlib.h>

static const Mel_Anim_Clip* mel__player_resolve(Mel_Anim_Player* player,
                                                 Mel_Anim_Clip_Handle handle)
{
    assert(mel_slotmap_alive(player->clip_pool, handle));
    return (const Mel_Anim_Clip*)mel_slotmap_get(player->clip_pool, handle);
}

static void mel__player_free_state(Mel_Anim_Player* player, Mel_Anim_Clip_State* state)
{
    if (state->cursors)
    {
        mel_dealloc(player->alloc, state->cursors);
        state->cursors = NULL;
    }
    state->cursor_count = 0;
}

static Mel_Anim_Clip_State mel__player_make_state(Mel_Anim_Player* player,
                                                   Mel_Anim_Clip_Handle clip_handle,
                                                   f32 start_time, f32 speed,
                                                   f32 xfade_duration)
{
    const Mel_Anim_Clip* clip = mel__player_resolve(player, clip_handle);
    u32 cc = mel_anim_clip_cursor_count(clip);

    Mel_Anim_Clip_State state = {
        .clip = clip_handle,
        .time = start_time,
        .speed = speed,
        .cursors = cc > 0 ? mel_alloc_array(player->alloc, u32, cc) : NULL,
        .cursor_count = cc,
        .xfade_elapsed = 0.0f,
        .xfade_duration = xfade_duration,
    };
    return state;
}

static void mel__player_push_event(Mel_Anim_Player* player, Mel_Anim_Event ev)
{
    if (player->event_count >= player->event_capacity)
    {
        u32 new_cap = player->event_capacity == 0 ? 16 : player->event_capacity * 2;
        usize new_size = sizeof(Mel_Anim_Event) * new_cap;
        if (player->pending_events == NULL)
            player->pending_events = mel_alloc(player->alloc, new_size);
        else
            player->pending_events = mel_realloc(player->alloc, player->pending_events, new_size);
        player->event_capacity = new_cap;
    }
    player->pending_events[player->event_count++] = ev;
}

static void mel__player_push_chain(Mel_Anim_Player* player, Mel_Anim_Clip_State state)
{
    if (player->chain_count >= player->chain_capacity)
    {
        u32 new_cap = player->chain_capacity == 0 ? 4 : player->chain_capacity * 2;
        usize new_size = sizeof(Mel_Anim_Clip_State) * new_cap;
        if (player->chain == NULL)
            player->chain = mel_alloc(player->alloc, new_size);
        else
            player->chain = mel_realloc(player->alloc, player->chain, new_size);
        player->chain_capacity = new_cap;
    }
    player->chain[player->chain_count++] = state;
}

void mel_anim_player_init(Mel_Anim_Player* player, const Mel_Alloc* alloc,
                          Mel_Anim_Clip_Pool* clip_pool)
{
    assert(player != NULL);
    assert(alloc != NULL);
    assert(clip_pool != NULL);

    *player = (Mel_Anim_Player){0};
    player->alloc = alloc;
    player->clip_pool = clip_pool;
    player->prev_root_rot = MEL_QUAT_IDENTITY;
}

void mel_anim_player_destroy(Mel_Anim_Player* player)
{
    assert(player != NULL);

    for (u32 i = 0; i < player->chain_count; i++)
        mel__player_free_state(player, &player->chain[i]);

    if (player->chain)
        mel_dealloc(player->alloc, player->chain);
    if (player->pending_events)
        mel_dealloc(player->alloc, player->pending_events);

    *player = (Mel_Anim_Player){0};
}

void mel_anim_player_play_opt(Mel_Anim_Player* player,
                              Mel_Anim_Clip_Handle clip,
                              Mel_Anim_Play_Opt opt)
{
    assert(player != NULL);
    assert(mel_slotmap_handle_valid(clip));

    if (opt.crossfade > 0.0f && player->chain_count > 0)
    {
        Mel_Anim_Clip_State new_state = mel__player_make_state(
            player, clip, opt.start_time, opt.speed, opt.crossfade);
        mel__player_push_chain(player, new_state);
    }
    else
    {
        for (u32 i = 0; i < player->chain_count; i++)
            mel__player_free_state(player, &player->chain[i]);
        player->chain_count = 0;

        Mel_Anim_Clip_State new_state = mel__player_make_state(
            player, clip, opt.start_time, opt.speed, 0.0f);
        mel__player_push_chain(player, new_state);
    }
}

static void mel__player_collect_events(Mel_Anim_Player* player,
                                        const Mel_Anim_Clip* clip,
                                        f32 prev_time, f32 new_time)
{
    for (u32 eg = 0; eg < clip->event_group_count; eg++)
    {
        const Mel_Event_Group* egrp = &clip->event_groups[eg];

        for (u32 t = 0; t < egrp->track_count; t++)
        {
            u32 kf_count = egrp->keyframe_counts[t];
            u32 data_off = egrp->data_offsets[t];
            u64 prop_id = egrp->property_ids[t];

            for (u32 k = 0; k < kf_count; k++)
            {
                f32 et = egrp->flat_times[data_off + k];
                if (et > prev_time && et <= new_time)
                {
                    mel__player_push_event(player, (Mel_Anim_Event){
                        .event_hash = egrp->flat_event_hashes[data_off + k],
                        .property_id = prop_id,
                        .time = et,
                    });
                }
            }
        }
    }
}

void mel_anim_player_update(Mel_Anim_Player* player, f32 dt)
{
    assert(player != NULL);
    assert(player->chain_count > 0);

    player->event_count = 0;

    for (u32 i = 0; i < player->chain_count; i++)
    {
        Mel_Anim_Clip_State* state = &player->chain[i];
        const Mel_Anim_Clip* clip = mel__player_resolve(player, state->clip);

        f32 prev_time = state->time;
        state->time += dt * state->speed;

        if (clip->is_looping)
        {
            while (state->time >= clip->duration && clip->duration > 0.0f)
                state->time = clip->loop_start_time + (state->time - clip->duration);
        }
        else
        {
            if (state->time > clip->duration)
                state->time = clip->duration;
        }

        if (i > 0)
            state->xfade_elapsed += dt;

        bool is_active = (i == player->chain_count - 1);
        if (is_active)
            mel__player_collect_events(player, clip, prev_time, state->time);
    }

    u32 prune_to = 0;
    for (u32 i = 1; i < player->chain_count; i++)
    {
        if (player->chain[i].xfade_elapsed >= player->chain[i].xfade_duration)
            prune_to = i;
    }

    if (prune_to > 0)
    {
        for (u32 i = 0; i < prune_to; i++)
            mel__player_free_state(player, &player->chain[i]);

        u32 remaining = player->chain_count - prune_to;
        memmove(&player->chain[0], &player->chain[prune_to],
                sizeof(Mel_Anim_Clip_State) * remaining);
        player->chain_count = remaining;
    }

    if (player->event_count > 1)
    {
        for (u32 i = 1; i < player->event_count; i++)
        {
            Mel_Anim_Event key = player->pending_events[i];
            u32 j = i;
            while (j > 0 && player->pending_events[j - 1].time > key.time)
            {
                player->pending_events[j] = player->pending_events[j - 1];
                j--;
            }
            player->pending_events[j] = key;
        }
    }

    Mel_Anim_Clip_State* active = &player->chain[player->chain_count - 1];
    const Mel_Anim_Clip* active_clip = mel__player_resolve(player, active->clip);
    player->phase = (active_clip->duration > 0.0f)
                    ? active->time / active_clip->duration
                    : 0.0f;
}

void mel_anim_player_sample(Mel_Anim_Player* player,
                            const Mel_Alloc* scratch,
                            Mel_Local_Pose* out_pose)
{
    assert(player != NULL);
    assert(scratch != NULL);
    assert(out_pose != NULL);
    assert(player->chain_count > 0);

    Mel_Anim_Clip_State* first = &player->chain[0];
    const Mel_Anim_Clip* first_clip = mel__player_resolve(player, first->clip);

    mel_pose_allocate(out_pose, first_clip, scratch);
    mel_anim_sample(first_clip, first->time, first->cursors, scratch, out_pose);

    for (u32 i = 1; i < player->chain_count; i++)
    {
        Mel_Anim_Clip_State* state = &player->chain[i];
        const Mel_Anim_Clip* clip = mel__player_resolve(player, state->clip);

        Mel_Local_Pose next_pose;
        mel_pose_allocate(&next_pose, clip, scratch);
        mel_anim_sample(clip, state->time, state->cursors, scratch, &next_pose);

        f32 alpha = (state->xfade_duration > 0.0f)
                    ? state->xfade_elapsed / state->xfade_duration
                    : 1.0f;
        if (alpha > 1.0f) alpha = 1.0f;

        mel_anim_blend(out_pose, &next_pose, alpha, NULL, 0);
    }
}

void mel_anim_player_get_float(Mel_Anim_Player* player, u64 property_id,
                               const Mel_Alloc* scratch, f32* out)
{
    Mel_Local_Pose pose;
    mel_anim_player_sample(player, scratch, &pose);
    mel__pose_extract(&pose, MEL_ANIM_TYPE_F32, property_id, out);
}

void mel_anim_player_get_vec2(Mel_Anim_Player* player, u64 property_id,
                              const Mel_Alloc* scratch, void* out)
{
    Mel_Local_Pose pose;
    mel_anim_player_sample(player, scratch, &pose);
    mel__pose_extract(&pose, MEL_ANIM_TYPE_VEC2, property_id, out);
}

void mel_anim_player_get_vec3(Mel_Anim_Player* player, u64 property_id,
                              const Mel_Alloc* scratch, void* out)
{
    Mel_Local_Pose pose;
    mel_anim_player_sample(player, scratch, &pose);
    mel__pose_extract(&pose, MEL_ANIM_TYPE_VEC3, property_id, out);
}

void mel_anim_player_get_vec4(Mel_Anim_Player* player, u64 property_id,
                              const Mel_Alloc* scratch, void* out)
{
    Mel_Local_Pose pose;
    mel_anim_player_sample(player, scratch, &pose);
    mel__pose_extract(&pose, MEL_ANIM_TYPE_VEC4, property_id, out);
}

void mel_anim_player_get_quat(Mel_Anim_Player* player, u64 property_id,
                              const Mel_Alloc* scratch, void* out)
{
    Mel_Local_Pose pose;
    mel_anim_player_sample(player, scratch, &pose);
    mel__pose_extract(&pose, MEL_ANIM_TYPE_QUAT, property_id, out);
}

void mel_anim_player_extract_root_motion(Mel_Anim_Player* player,
                                         const Mel_Local_Pose* pose,
                                         const Mel_Skeleton* skeleton,
                                         Mel_Root_Motion* out)
{
    assert(player != NULL);
    assert(pose != NULL);
    assert(skeleton != NULL);
    assert(out != NULL);

    Mel_Vec3 current_pos;
    Mel_Quat current_rot;

    mel__pose_extract(pose, MEL_ANIM_TYPE_VEC3, skeleton->root_bone_hash, &current_pos);
    mel__pose_extract(pose, MEL_ANIM_TYPE_QUAT, skeleton->root_bone_hash, &current_rot);

    out->delta_position = mel_vec3_sub(current_pos, player->prev_root_pos);
    out->delta_rotation = mel_quat_mul(mel_quat_inverse(player->prev_root_rot), current_rot);

    player->prev_root_pos = current_pos;
    player->prev_root_rot = current_rot;
}
