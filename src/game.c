#include "game.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <tracy/TracyC.h>

static ecs_query_t* s_draw_query = nullptr;

void mel_game_init(Mel_Game* game)
{
    assert(game != nullptr);

    *game = (Mel_Game){0};

    mel_ecs_init(&game->ecs);

    game->player = mel_ecs_create_player(&game->ecs, mel_vec2(100, 100), 200.0f);
    game->npc = mel_ecs_create_npc(&game->ecs, mel_vec2(300, 200), "Hello! I am an NPC.\nPress E to close.");

    mel_ecs_create_wall(&game->ecs, mel_vec2(0, 0), mel_vec2(640, 16));
    mel_ecs_create_wall(&game->ecs, mel_vec2(0, 464), mel_vec2(640, 16));
    mel_ecs_create_wall(&game->ecs, mel_vec2(0, 0), mel_vec2(16, 480));
    mel_ecs_create_wall(&game->ecs, mel_vec2(624, 0), mel_vec2(16, 480));

    const Mel_CNPC* npc_data = ecs_get(game->ecs.world, game->npc, Mel_CNPC);
    game->dialogue_text = npc_data->dialogue;
}

void mel_game_shutdown(Mel_Game* game)
{
    assert(game != nullptr);

    if (s_draw_query)
    {
        ecs_query_fini(s_draw_query);
        s_draw_query = nullptr;
    }

    mel_ecs_shutdown(&game->ecs);
}

void mel_game_handle_event(Mel_Game* game, SDL_Event* event)
{
    assert(game != nullptr);

    if (event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP)
    {
        bool down = event->type == SDL_EVENT_KEY_DOWN;
        SDL_Scancode scancode = event->key.scancode;

        Mel_CPlayer* player = ecs_get_mut(game->ecs.world, game->player, Mel_CPlayer);
        if (!player) return;

        switch (scancode)
        {
            case SDL_SCANCODE_W: player->input_up = down; break;
            case SDL_SCANCODE_S: player->input_down = down; break;
            case SDL_SCANCODE_A: player->input_left = down; break;
            case SDL_SCANCODE_D: player->input_right = down; break;
            case SDL_SCANCODE_E:
                if (down && !event->key.repeat)
                {
                    game->input_interact = true;
                }
                break;
            default: break;
        }
    }
}

bool mel_game_can_interact(Mel_Game* game)
{
    const Mel_CTransform* player_t = ecs_get(game->ecs.world, game->player, Mel_CTransform);
    const Mel_CTransform* npc_t = ecs_get(game->ecs.world, game->npc, Mel_CTransform);
    const Mel_CNPC* npc = ecs_get(game->ecs.world, game->npc, Mel_CNPC);

    if (!player_t || !npc_t || !npc) return false;

    Mel_Vec2 player_center = mel_vec2(player_t->pos.x + 16, player_t->pos.y + 16);
    Mel_Vec2 npc_center = mel_vec2(npc_t->pos.x + 16, npc_t->pos.y + 16);

    f32 dx = player_center.x - npc_center.x;
    f32 dy = player_center.y - npc_center.y;
    f32 dist_sq = dx * dx + dy * dy;

    return dist_sq < npc->interact_radius * npc->interact_radius;
}

void mel_game_update(Mel_Game* game, f32 dt)
{
    TracyCZoneN(ctx, "mel_game_update", true);
    assert(game != nullptr);

    if (game->input_interact)
    {
        game->input_interact = false;

        if (game->dialogue_open)
        {
            game->dialogue_open = false;
        }
        else if (mel_game_can_interact(game))
        {
            game->dialogue_open = true;
        }
    }

    if (!game->dialogue_open)
    {
        mel_ecs_update(&game->ecs, dt);
    }
    TracyCZoneEnd(ctx);
}

void mel_game_draw(Mel_Game* game, Mel_SpriteBatch* batch)
{
    TracyCZoneN(ctx, "mel_game_draw", true);
    assert(game != nullptr);
    assert(batch != nullptr);

    if (!s_draw_query)
    {
        s_draw_query = ecs_query(game->ecs.world, {
            .terms = {
                { .id = ecs_id(Mel_CTransform) },
                { .id = ecs_id(Mel_Sprite) }
            }
        });
    }

    ecs_iter_t it = ecs_query_iter(game->ecs.world, s_draw_query);
    while (ecs_query_next(&it))
    {
        Mel_CTransform* t = ecs_field(&it, Mel_CTransform, 0);
        Mel_Sprite* s = ecs_field(&it, Mel_Sprite, 1);

        for (int i = 0; i < it.count; i++)
        {
            mel_sprite_batch_draw(batch, t[i].pos.x, t[i].pos.y, s[i].size.x, s[i].size.y, s[i].color);
        }
    }
    TracyCZoneEnd(ctx);
}
