#include "wall.h"

#include <cimgui/cimgui.h>

bool mel_ed_wall_draw(ecs_world_t* world, ecs_entity_t e)
{
    if (!ecs_has(world, e, Wall)) return false;

    if (igCollapsingHeader_TreeNodeFlags("Wall (Tag)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        igIndent(10);
        igTextDisabled("This entity is a wall");
        igUnindent(10);
    }

    return true;
}
