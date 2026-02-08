#include "game.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <tracy/TracyC.h>

static ecs_query_t* s_draw_query = nullptr;
static ecs_query_t* s_collider_query = nullptr;

static bool aabb_overlap(Mel_Vec2 a_pos, Mel_Vec2 a_size, Mel_Vec2 b_pos, Mel_Vec2 b_size)
{
    return a_pos.x < b_pos.x + b_size.x &&
           a_pos.x + a_size.x > b_pos.x &&
           a_pos.y < b_pos.y + b_size.y &&
           a_pos.y + a_size.y > b_pos.y;
}

static bool would_collide(ecs_world_t* world, ecs_entity_t self, Mel_Vec2 new_pos, Mel_Vec2 size)
{
    if (!s_collider_query)
    {
        s_collider_query = ecs_query(world, {
            .terms = {
                { .id = ecs_id(Mel_CTransform) },
                { .id = ecs_id(Mel_CCollider) }
            }
        });
    }

    bool collided = false;

    ecs_iter_t it = ecs_query_iter(world, s_collider_query);
    while (ecs_query_next(&it))
    {
        Mel_CTransform* t = ecs_field(&it, Mel_CTransform, 0);
        Mel_CCollider* c = ecs_field(&it, Mel_CCollider, 1);

        for (int i = 0; i < it.count; i++)
        {
            if (it.entities[i] == self) continue;

            if (aabb_overlap(new_pos, size, t[i].pos, c[i].size))
            {
                collided = true;
                break;
            }
        }

        if (collided)
        {
            ecs_iter_fini(&it);
            break;
        }
    }

    return collided;
}

static void system_player_input(ecs_iter_t* it)
{
    Mel_CTransform* t = ecs_field(it, Mel_CTransform, 0);
    Mel_CPlayer* p = ecs_field(it, Mel_CPlayer, 1);

    for (int i = 0; i < it->count; i++)
    {
        Mel_Vec2 move = mel_vec2(0, 0);

        if (p[i].input_up) move.y -= 1.0f;
        if (p[i].input_down) move.y += 1.0f;
        if (p[i].input_left) move.x -= 1.0f;
        if (p[i].input_right) move.x += 1.0f;

        f32 len_sq = move.x * move.x + move.y * move.y;
        if (len_sq > 0.0f)
        {
            f32 len = sqrtf(len_sq);
            move.x /= len;
            move.y /= len;
        }

        t[i].vel.x = move.x * p[i].speed;
        t[i].vel.y = move.y * p[i].speed;
    }
}

static void system_movement(ecs_iter_t* it)
{
    Mel_CTransform* t = ecs_field(it, Mel_CTransform, 0);
    Mel_CCollider* c = ecs_field(it, Mel_CCollider, 1);

    for (int i = 0; i < it->count; i++)
    {
        if (t[i].vel.x == 0.0f && t[i].vel.y == 0.0f) continue;

        Mel_Vec2 new_pos_x = mel_vec2(
            t[i].pos.x + t[i].vel.x * it->delta_time,
            t[i].pos.y
        );

        if (!would_collide(it->world, it->entities[i], new_pos_x, c[i].size))
        {
            t[i].pos.x = new_pos_x.x;
        }

        Mel_Vec2 new_pos_y = mel_vec2(
            t[i].pos.x,
            t[i].pos.y + t[i].vel.y * it->delta_time
        );

        if (!would_collide(it->world, it->entities[i], new_pos_y, c[i].size))
        {
            t[i].pos.y = new_pos_y.y;
        }
    }
}

static ecs_entity_t create_player(ecs_world_t* world, Mel_Vec2 pos, f32 speed)
{
    ecs_entity_t e = ecs_new(world);

    ecs_set(world, e, Mel_CTransform, {
        .pos = pos,
        .vel = mel_vec2(0, 0)
    });

    ecs_set(world, e, Mel_Sprite, {
        .size = mel_vec2(32, 32),
        .color = mel_vec4(0.9f, 0.2f, 0.2f, 1.0f)
    });

    ecs_set(world, e, Mel_CPlayer, {
        .speed = speed,
        .input_up = false,
        .input_down = false,
        .input_left = false,
        .input_right = false
    });

    ecs_set(world, e, Mel_CCollider, {
        .size = mel_vec2(32, 32)
    });

    return e;
}

static ecs_entity_t create_npc(ecs_world_t* world, Mel_Vec2 pos, const char* dialogue)
{
    ecs_entity_t e = ecs_new(world);

    ecs_set(world, e, Mel_CTransform, {
        .pos = pos,
        .vel = mel_vec2(0, 0)
    });

    ecs_set(world, e, Mel_Sprite, {
        .size = mel_vec2(32, 32),
        .color = mel_vec4(0.2f, 0.4f, 0.9f, 1.0f)
    });

    ecs_set(world, e, Mel_CNPC, {
        .dialogue = dialogue,
        .interact_radius = 50.0f
    });

    ecs_set(world, e, Mel_CCollider, {
        .size = mel_vec2(32, 32)
    });

    return e;
}

static ecs_entity_t create_wall(ecs_world_t* world, Mel_Vec2 pos, Mel_Vec2 size)
{
    ecs_entity_t e = ecs_new(world);

    ecs_set(world, e, Mel_CTransform, {
        .pos = pos,
        .vel = mel_vec2(0, 0)
    });

    ecs_set(world, e, Mel_Sprite, {
        .size = size,
        .color = mel_vec4(0.4f, 0.4f, 0.45f, 1.0f)
    });

    ecs_set(world, e, Mel_CCollider, {
        .size = size
    });

    ecs_add(world, e, Wall);

    return e;
}

void mel_game_init(Mel_Game* game)
{
    assert(game != nullptr);

    *game = (Mel_Game){0};

    mel_ecs_init(&game->ecs);
    ecs_world_t* world = game->ecs.world;

    mel_component_transform_register(world);
    mel_component_sprite_register(world);
    mel_component_player_register(world);
    mel_component_npc_register(world);
    mel_component_collider_register(world);
    mel_component_wall_register(world);

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "PlayerInput",
            .add = ecs_ids(ecs_dependson(EcsOnUpdate))
        }),
        .query.terms = {
            { .id = ecs_id(Mel_CTransform) },
            { .id = ecs_id(Mel_CPlayer) }
        },
        .callback = system_player_input
    });

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "Movement",
            .add = ecs_ids(ecs_dependson(EcsOnUpdate))
        }),
        .query.terms = {
            { .id = ecs_id(Mel_CTransform) },
            { .id = ecs_id(Mel_CCollider) }
        },
        .callback = system_movement
    });

    game->player = create_player(world, mel_vec2(100, 100), 200.0f);
    game->npc = create_npc(world, mel_vec2(300, 200), "Hello! I am an NPC.\nPress E to close.");

    create_wall(world, mel_vec2(0, 0), mel_vec2(640, 16));
    create_wall(world, mel_vec2(0, 464), mel_vec2(640, 16));
    create_wall(world, mel_vec2(0, 0), mel_vec2(16, 480));
    create_wall(world, mel_vec2(624, 0), mel_vec2(16, 480));

    const Mel_CNPC* npc_data = ecs_get(world, game->npc, Mel_CNPC);
    game->dialogue_text = npc_data->dialogue;
}

void mel_game_shutdown(Mel_Game* game)
{
    assert(game != nullptr);

    if (s_collider_query)
    {
        ecs_query_fini(s_collider_query);
        s_collider_query = nullptr;
    }

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
