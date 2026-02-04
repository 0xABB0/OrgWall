#include "player.h"

#include <cimgui/cimgui.h>

bool mel_ed_player_draw(ecs_world_t* world, ecs_entity_t e)
{
    Mel_CPlayer* player = ecs_get_mut(world, e, Mel_CPlayer);
    if (!player) return false;

    if (igCollapsingHeader_TreeNodeFlags("Player", ImGuiTreeNodeFlags_DefaultOpen))
    {
        igIndent(10);
        bool modified = false;
        if (igDragFloat("Speed##player", &player->speed, 1.0f, 0, 1000, "%.1f", 0))
        {
            modified = true;
        }
        igText("Input: %s%s%s%s",
            player->input_up ? "U" : "-",
            player->input_down ? "D" : "-",
            player->input_left ? "L" : "-",
            player->input_right ? "R" : "-");
        if (modified) ecs_modified(world, e, Mel_CPlayer);
        igUnindent(10);
    }
    return true;
}
