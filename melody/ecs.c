#include "ecs.h"
#include <math.h>
#include <tracy/TracyC.h>

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

static bool aabb_overlap(Mel_Vec2 a_pos, Mel_Vec2 a_size, Mel_Vec2 b_pos, Mel_Vec2 b_size)
{
    return a_pos.x < b_pos.x + b_size.x &&
           a_pos.x + a_size.x > b_pos.x &&
           a_pos.y < b_pos.y + b_size.y &&
           a_pos.y + a_size.y > b_pos.y;
}

static ecs_query_t* s_collider_query = nullptr;

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

void mel_ecs_init(Mel_ECS* ecs)
{
    assert(ecs != nullptr);

    ecs->world = ecs_init();
    ecs->player = 0;

    mel_component_transform_register(ecs->world);
    mel_component_sprite_register(ecs->world);
    mel_component_player_register(ecs->world);
    mel_component_npc_register(ecs->world);
    mel_component_collider_register(ecs->world);
    mel_component_wall_register(ecs->world);

    ecs_system(ecs->world, {
        .entity = ecs_entity(ecs->world, {
            .name = "PlayerInput",
            .add = ecs_ids(ecs_dependson(EcsOnUpdate))
        }),
        .query.terms = {
            { .id = ecs_id(Mel_CTransform) },
            { .id = ecs_id(Mel_CPlayer) }
        },
        .callback = system_player_input
    });

    ecs_system(ecs->world, {
        .entity = ecs_entity(ecs->world, {
            .name = "Movement",
            .add = ecs_ids(ecs_dependson(EcsOnUpdate))
        }),
        .query.terms = {
            { .id = ecs_id(Mel_CTransform) },
            { .id = ecs_id(Mel_CCollider) }
        },
        .callback = system_movement
    });
}

void mel_ecs_shutdown(Mel_ECS* ecs)
{
    assert(ecs != nullptr);

    if (s_collider_query)
    {
        ecs_query_fini(s_collider_query);
        s_collider_query = nullptr;
    }

    if (ecs->world)
    {
        ecs_fini(ecs->world);
        ecs->world = nullptr;
    }
}

void mel_ecs_update(Mel_ECS* ecs, f32 dt)
{
    TracyCZoneN(ctx, "ecs_progress", true);
    assert(ecs != nullptr);
    ecs_progress(ecs->world, dt);
    TracyCZoneEnd(ctx);
}

ecs_entity_t mel_ecs_create_player(Mel_ECS* ecs, Mel_Vec2 pos, f32 speed)
{
    ecs_entity_t e = ecs_new(ecs->world);

    ecs_set(ecs->world, e, Mel_CTransform, {
        .pos = pos,
        .vel = mel_vec2(0, 0)
    });

    ecs_set(ecs->world, e, Mel_Sprite, {
        .size = mel_vec2(32, 32),
        .color = mel_vec4(0.9f, 0.2f, 0.2f, 1.0f)
    });

    ecs_set(ecs->world, e, Mel_CPlayer, {
        .speed = speed,
        .input_up = false,
        .input_down = false,
        .input_left = false,
        .input_right = false
    });

    ecs_set(ecs->world, e, Mel_CCollider, {
        .size = mel_vec2(32, 32)
    });

    ecs->player = e;

    return e;
}

ecs_entity_t mel_ecs_create_npc(Mel_ECS* ecs, Mel_Vec2 pos, const char* dialogue)
{
    ecs_entity_t e = ecs_new(ecs->world);

    ecs_set(ecs->world, e, Mel_CTransform, {
        .pos = pos,
        .vel = mel_vec2(0, 0)
    });

    ecs_set(ecs->world, e, Mel_Sprite, {
        .size = mel_vec2(32, 32),
        .color = mel_vec4(0.2f, 0.4f, 0.9f, 1.0f)
    });

    ecs_set(ecs->world, e, Mel_CNPC, {
        .dialogue = dialogue,
        .interact_radius = 50.0f
    });

    ecs_set(ecs->world, e, Mel_CCollider, {
        .size = mel_vec2(32, 32)
    });

    return e;
}

ecs_entity_t mel_ecs_create_wall(Mel_ECS* ecs, Mel_Vec2 pos, Mel_Vec2 size)
{
    ecs_entity_t e = ecs_new(ecs->world);

    ecs_set(ecs->world, e, Mel_CTransform, {
        .pos = pos,
        .vel = mel_vec2(0, 0)
    });

    ecs_set(ecs->world, e, Mel_Sprite, {
        .size = size,
        .color = mel_vec4(0.4f, 0.4f, 0.45f, 1.0f)
    });

    ecs_set(ecs->world, e, Mel_CCollider, {
        .size = size
    });

    ecs_add(ecs->world, e, Wall);

    return e;
}
