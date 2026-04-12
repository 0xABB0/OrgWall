#include "ecs.2d.sprite.editor.h"
#include "ecs.world.h"
#include "ecs.2d.sprite.h"

#include <cimgui/cimgui.h>

bool mel_editor_sprite(ecs_world_t* world, ecs_entity_t e)
{
    Mel_Sprite* sprite = ecs_get_mut(world, e, Mel_Sprite);
    if (!sprite) return false;

    if (igCollapsingHeader_TreeNodeFlags("Sprite", ImGuiTreeNodeFlags_DefaultOpen))
    {
        igIndent(10);
        bool modified = false;
        if (igDragFloat2("Size##sprite", sprite->size.e, 0.1f, 0, 0, "%.2f", 0)) {
            modified = true;
        }
        if (igDragFloat2("Color##sprite", sprite->color.e, 0.1f, 0, 0, "%.2f", 0)) {
            modified = true;
        }
        if (igDragFloat4("UV##sprite", &sprite->uv.x, 0.01f, 0, 1, "%.3f", 0)) {
            modified = true;
        }

        if (modified) ecs_modified(world, e, Mel_Sprite);
        igUnindent(10);
    }
    return true;
}

