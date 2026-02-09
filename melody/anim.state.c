#include "anim.state.h"

static bool mel__anim_state_has_condition(Mel_Anim_State_Player* player, u64 hash)
{
    for (u32 i = 0; i < player->active_condition_count; i++)
    {
        if (player->active_conditions[i] == hash)
            return true;
    }
    return false;
}

static void mel__anim_state_enter(Mel_Anim_State_Player* player, u32 state_idx)
{
    assert(state_idx < player->machine->state_count);
    player->current_state = state_idx;
    Mel_Anim_State_Def* state = &player->machine->states[state_idx];
    mel_anim_playback_start(&player->playback, state->timeline);
}

void mel_anim_state_player_init(Mel_Anim_State_Player* player, Mel_Anim_State_Machine* machine)
{
    assert(player != nullptr);
    assert(machine != nullptr);
    assert(machine->state_count > 0);

    *player = (Mel_Anim_State_Player){0};
    player->machine = machine;
    player->active_conditions = nullptr;
    player->active_condition_count = 0;
    mel__anim_state_enter(player, machine->initial_state);
}

void mel_anim_state_player_update(Mel_Anim_State_Player* player, f32 dt)
{
    assert(player != nullptr);
    assert(player->machine != nullptr);

    mel_anim_playback_update(&player->playback, dt);

    Mel_Anim_State_Def* state = &player->machine->states[player->current_state];

    for (u32 i = 0; i < state->transition_count; i++)
    {
        Mel_Anim_Transition* t = &state->transitions[i];

        if (!mel__anim_state_has_condition(player, t->condition_hash))
            continue;

        if (t->mode == MEL_ANIM_TRANSITION_AT_END && !player->playback.finished)
            continue;

        mel__anim_state_enter(player, t->target_state);
        return;
    }
}

void mel_anim_state_player_set_condition(Mel_Anim_State_Player* player, u64 hash, bool active)
{
    assert(player != nullptr);

    for (u32 i = 0; i < player->active_condition_count; i++)
    {
        if (player->active_conditions[i] == hash)
        {
            if (!active)
            {
                player->active_conditions[i] = player->active_conditions[player->active_condition_count - 1];
                player->active_condition_count--;
            }
            return;
        }
    }

    if (active)
    {
        assert(player->active_condition_count < 64);
        player->active_conditions[player->active_condition_count++] = hash;
    }
}

u32 mel_anim_state_player_frame_index(Mel_Anim_State_Player* player)
{
    assert(player != nullptr);
    return mel_anim_playback_frame_index(&player->playback);
}
