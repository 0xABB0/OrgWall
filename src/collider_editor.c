#include "collider.h"
#include "ed_helpers.h"

#include <cimgui/cimgui.h>

bool mel_ed_collider_draw(ecs_world_t* world, ecs_entity_t e)
{
    Mel_CCollider* collider = ecs_get_mut(world, e, Mel_CCollider);
    if (!collider) return false;

    if (igCollapsingHeader_TreeNodeFlags("Collider", ImGuiTreeNodeFlags_DefaultOpen))
    {
        igIndent(10);
        bool modified = false;
        mel_ed_draw_vec2("Size##collider", &collider->size, &modified);
        if (modified) ecs_modified(world, e, Mel_CCollider);
        igUnindent(10);
    }
    return true;
}
