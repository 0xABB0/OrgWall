#include "transform.h"
#include "ed_helpers.h"

#include <cimgui/cimgui.h>

bool mel_ed_transform_draw(ecs_world_t* world, ecs_entity_t e)
{
    Mel_CTransform* transform = ecs_get_mut(world, e, Mel_CTransform);
    if (!transform) return false;

    if (igCollapsingHeader_TreeNodeFlags("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        igIndent(10);
        bool modified = false;
        mel_ed_draw_vec2("Position##transform", &transform->pos, &modified);
        mel_ed_draw_vec2("Velocity##transform", &transform->vel, &modified);
        if (modified) ecs_modified(world, e, Mel_CTransform);
        igUnindent(10);
    }
    return true;
}
