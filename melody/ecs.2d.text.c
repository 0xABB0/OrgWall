#include "ecs.2d.text.h"
#include "ecs.2d.transform.h"
#include "font.atlas.h"
#include "render.list.h"
#include "string.str8.h"

ECS_COMPONENT_DECLARE(Mel_CText);

void mel_component_text_register(ecs_world_t* world)
{
    ECS_COMPONENT_DEFINE(world, Mel_CText);
}

void mel_text_system_run_opt(ecs_world_t* world, Mel_Text_System_Opt opt)
{
    assert(world != nullptr);
    assert(opt.list != nullptr);
    assert(opt.font_pool != nullptr);

    ecs_query_t* q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(Mel_CTransform), .inout = EcsIn },
            { .id = ecs_id(Mel_CText), .inout = EcsIn },
        }
    });

    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it))
    {
        const Mel_CTransform* transforms = ecs_field(&it, Mel_CTransform, 0);
        const Mel_CText* texts = ecs_field(&it, Mel_CText, 1);

        for (int i = 0; i < it.count; i++)
        {
            mel_font_atlas_draw_text(opt.font_pool, texts[i].font, opt.list,
                texts[i].text, transforms[i].pos.x, transforms[i].pos.y, texts[i].color);
        }
    }

    ecs_query_fini(q);
}
