#include "anim.state.h"
#include "anim.mixer.h"
#include "anim.clip.h"
#include "allocator.h"
#include "collection.array.h"

void mel_anim_state_machine_init(Mel_Anim_State_Machine* machine,
                                 Mel_Anim_State_Def* states, u32 state_count,
                                 u32 initial_state)
{
    assert(machine != NULL);
    assert(states != NULL);
    assert(state_count > 0);
    assert(initial_state < state_count);

    machine->states = states;
    machine->state_count = state_count;
    machine->initial_state = initial_state;
}

u32 mel_anim_state_machine_find_state(Mel_Anim_State_Machine* machine, u64 name_hash)
{
    assert(machine != NULL);

    for (u32 i = 0; i < machine->state_count; i++)
    {
        if (machine->states[i].name_hash == name_hash)
            return i;
    }

    assert(false);
    return 0;
}

static void mel__anim_state_enter(Mel_Anim_State_Player* player, u32 state_idx,
                                   f32 mix_duration)
{
    Mel_Anim_State_Def* state = &player->machine->states[state_idx];
    player->current_state = state_idx;
    mel_anim_mixer_play(player->mixer, player->mixer_layer, state->clip, mix_duration);
}

void mel_anim_state_player_init_opt(Mel_Anim_State_Player* player,
                                    const Mel_Alloc* alloc,
                                    Mel_Anim_State_Player_Opt opt)
{
    assert(player != NULL);
    assert(alloc != NULL);
    assert(opt.machine != NULL);
    assert(opt.mixer != NULL);
    assert(opt.machine->state_count > 0);

    player->machine = opt.machine;
    player->mixer = opt.mixer;
    player->mixer_layer = opt.mixer_layer;
    player->on_transition = opt.on_transition;
    player->on_transition_ctx = opt.on_transition_ctx;
    mel_array_init(&player->active_conditions, alloc);

    mel__anim_state_enter(player, opt.machine->initial_state, 0.0f);
}

void mel_anim_state_player_destroy(Mel_Anim_State_Player* player)
{
    assert(player != NULL);
    mel_array_free(&player->active_conditions);
}

void mel_anim_state_player_update(Mel_Anim_State_Player* player)
{
    assert(player != NULL);
    assert(player->machine != NULL);

    Mel_Anim_State_Def* state = &player->machine->states[player->current_state];
    Mel_Anim_Layer* layer = mel_anim_mixer_layer(player->mixer, player->mixer_layer);

    for (u32 i = 0; i < state->transition_count; i++)
    {
        Mel_Anim_Transition* t = &state->transitions[i];

        if (!mel_anim_state_player_has_condition(player, t->condition_hash))
            continue;

        if (t->mode == MEL_ANIM_TRANSITION_AT_END && !layer->finished)
            continue;

        u32 from = player->current_state;
        u32 to = t->target_state;

        if (t->auto_clear_condition)
            mel_anim_state_player_set_condition(player, t->condition_hash, false);

        mel__anim_state_enter(player, to, t->mix_duration);

        if (player->on_transition)
            player->on_transition(player->on_transition_ctx, from, to);

        return;
    }
}

void mel_anim_state_player_set_condition(Mel_Anim_State_Player* player,
                                         u64 hash, bool active)
{
    assert(player != NULL);

    for (usize i = 0; i < player->active_conditions.count; i++)
    {
        if (player->active_conditions.items[i] == hash)
        {
            if (!active)
                mel_array_remove_unordered(&player->active_conditions, i);
            return;
        }
    }

    if (active)
        mel_array_push(&player->active_conditions, hash);
}

bool mel_anim_state_player_has_condition(const Mel_Anim_State_Player* player,
                                         u64 hash)
{
    assert(player != NULL);

    for (usize i = 0; i < player->active_conditions.count; i++)
    {
        if (player->active_conditions.items[i] == hash)
            return true;
    }

    return false;
}

u32 mel_anim_state_player_current_state(const Mel_Anim_State_Player* player)
{
    assert(player != NULL);
    return player->current_state;
}
