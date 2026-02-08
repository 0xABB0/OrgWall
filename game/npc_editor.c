#include "npc.h"

#include <cimgui/cimgui.h>

bool mel_ed_npc_draw(ecs_world_t* world, ecs_entity_t e)
{
    Mel_CNPC* npc = ecs_get_mut(world, e, Mel_CNPC);
    if (!npc) return false;

    if (igCollapsingHeader_TreeNodeFlags("NPC", ImGuiTreeNodeFlags_DefaultOpen))
    {
        igIndent(10);
        igText("Dialogue: %s", npc->dialogue ? npc->dialogue : "(none)");
        if (igDragFloat("Interact Radius##npc", &npc->interact_radius, 1.0f, 0, 500, "%.1f", 0))
        {
            ecs_modified(world, e, Mel_CNPC);
        }
        igUnindent(10);
    }
    return true;
}
