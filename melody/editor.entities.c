#include "editor.entities.h"
#include "collection.array.h"
#include "ecs.world.h"

#include <cimgui/cimgui.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "ecs.2d.transform.h"
#include "ecs.2d.sprite.h"

#ifndef IM_COL32
#define IM_COL32(R,G,B,A) (((ImU32)(A)<<24) | ((ImU32)(B)<<16) | ((ImU32)(G)<<8) | ((ImU32)(R)))
#endif

void mel_ed_entities_init(Mel_EdEntities* ed, const Mel_Alloc* alloc)
{
    assert(ed != nullptr);
    assert(alloc != nullptr);

    *ed = (Mel_EdEntities){0};
    ed->selected_entity = 0;
    ed->show_builtin = false;
    ed->show_disabled = true;
    ed->refresh_interval = 0.5f;
    ed->active_query = nullptr;
    strcpy(ed->query_buffer, "Mel_CTransform");
    mel_array_init(&ed->inspectors, alloc);
}

void mel_ed_entities_shutdown(Mel_EdEntities* ed)
{
    assert(ed != nullptr);

    if (ed->active_query)
    {
        ecs_query_fini(ed->active_query);
        ed->active_query = nullptr;
    }

    mel_array_free(&ed->inspectors);
    ed->world = nullptr;
}

void mel_ed_entities_register_inspector(Mel_EdEntities* ed, Mel_ComponentInspector_Fn fn)
{
    assert(ed != nullptr);
    assert(fn != nullptr);

    mel_array_push(&ed->inspectors, fn);
}

void mel_ed_entities_set_world(Mel_EdEntities* ed, ecs_world_t* world)
{
    assert(ed != nullptr);
    ed->world = world;
    ed->selected_entity = 0;

    if (ed->active_query)
    {
        ecs_query_fini(ed->active_query);
        ed->active_query = nullptr;
    }
}

static bool is_builtin_entity(ecs_world_t* world, ecs_entity_t e)
{
    if (e < 256) return true;

    const char* name = ecs_get_name(world, e);
    if (!name) return false;

    if (name[0] == 'E' && name[1] == 'c' && name[2] == 's') return true;
    if (name[0] == 'f' && name[1] == 'l' && name[2] == 'e') return true;
    if (strncmp(name, "flecs", 5) == 0) return true;

    return false;
}

static void draw_entity_tree_node(Mel_EdEntities* ed, ecs_entity_t entity)
{
    if (!ed->world) return;
    if (!ecs_is_valid(ed->world, entity)) return;

    if (!ed->show_builtin && is_builtin_entity(ed->world, entity))
    {
        return;
    }

    if (!ed->show_disabled && ecs_has_id(ed->world, entity, EcsDisabled))
    {
        return;
    }

    const char* name = ecs_get_name(ed->world, entity);
    char label[256];
    if (name)
    {
        snprintf(label, sizeof(label), "%s (%" PRIu64 ")###e%" PRIu64, name, (u64)entity, (u64)entity);
    }
    else
    {
        snprintf(label, sizeof(label), "Entity %" PRIu64 "###e%" PRIu64, (u64)entity, (u64)entity);
    }

    ecs_iter_t child_it = ecs_each_id(ed->world, ecs_pair(EcsChildOf, entity));
    bool has_children = ecs_each_next(&child_it);
    if (has_children)
    {
        ecs_iter_fini(&child_it);
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!has_children)
    {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (ed->selected_entity == entity)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool node_open = igTreeNodeEx_Str(label, flags);

    if (igIsItemClicked(0))
    {
        ed->selected_entity = entity;
    }

    if (igBeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonRight))
    {
        if (igMenuItem_Bool("Delete Entity", nullptr, false, true))
        {
            ecs_delete(ed->world, entity);
            if (ed->selected_entity == entity)
            {
                ed->selected_entity = 0;
            }
        }

        bool enabled = !ecs_has_id(ed->world, entity, EcsDisabled);
        if (igMenuItem_Bool(enabled ? "Disable" : "Enable", nullptr, false, true))
        {
            ecs_enable(ed->world, entity, !enabled);
        }

        igEndPopup();
    }

    if (node_open)
    {
        ecs_iter_t it = ecs_each_id(ed->world, ecs_pair(EcsChildOf, entity));
        while (ecs_each_next(&it))
        {
            for (int i = 0; i < it.count; i++)
            {
                draw_entity_tree_node(ed, it.entities[i]);
            }
        }
        igTreePop();
    }
}

static void draw_entity_tree(Mel_EdEntities* ed)
{
    if (!ed->world) return;

    igText("Entity Tree");
    igSameLine(0, 10);
    igCheckbox("Builtin##tree", &ed->show_builtin);
    igSameLine(0, 10);
    igCheckbox("Disabled##tree", &ed->show_disabled);

    if (igBeginChild_Str("EntityTree", (ImVec2){0, 200}, ImGuiChildFlags_Borders, 0))
    {
        ecs_entity_t root_entities[1024];
        i32 root_count = 0;

        ecs_iter_t it = ecs_each_id(ed->world, ecs_id(Mel_CTransform));
        while (ecs_each_next(&it))
        {
            for (int i = 0; i < it.count && root_count < 1024; i++)
            {
                ecs_entity_t e = it.entities[i];

                if (!ed->show_builtin && is_builtin_entity(ed->world, e))
                {
                    continue;
                }

                ecs_entity_t parent = ecs_get_target(ed->world, e, EcsChildOf, 0);
                if (parent == 0)
                {
                    root_entities[root_count++] = e;
                }
            }
        }

        for (i32 i = 0; i < root_count; i++)
        {
            draw_entity_tree_node(ed, root_entities[i]);
        }
    }
    igEndChild();
}

static void draw_entity_inspector(Mel_EdEntities* ed)
{
    if (!ed->world) return;

    igText("Entity Inspector");

    if (ed->selected_entity == 0 || !ecs_is_valid(ed->world, ed->selected_entity))
    {
        igTextDisabled("No entity selected");
        return;
    }

    ecs_entity_t e = ed->selected_entity;

    const char* name = ecs_get_name(ed->world, e);
    igText("Name: %s", name ? name : "(unnamed)");
    igText("ID: %" PRIu64, (u64)e);

    bool enabled = !ecs_has_id(ed->world, e, EcsDisabled);
    igText("Enabled: %s", enabled ? "Yes" : "No");

    ecs_entity_t parent = ecs_get_target(ed->world, e, EcsChildOf, 0);
    if (parent)
    {
        const char* parent_name = ecs_get_name(ed->world, parent);
        igText("Parent: %s (%" PRIu64 ")", parent_name ? parent_name : "(unnamed)", (u64)parent);

        igSameLine(0, 10);
        if (igSmallButton("Select Parent"))
        {
            ed->selected_entity = parent;
        }
    }

    igSeparator();
    igText("Components:");

    if (igBeginChild_Str("Components", (ImVec2){0, 0}, ImGuiChildFlags_Borders, 0))
    {
        for (usize i = 0; i < ed->inspectors.count; i++)
        {
            ed->inspectors.items[i](ed->world, e);
        }

        const ecs_type_t* type = ecs_get_type(ed->world, e);
        if (type)
        {
            igSeparator();
            igText("All IDs (%d):", type->count);
            for (i32 i = 0; i < type->count; i++)
            {
                ecs_id_t id = type->array[i];
                char* id_str = ecs_id_str(ed->world, id);
                if (id_str)
                {
                    igBulletText("%s", id_str);
                    ecs_os_free(id_str);
                }
            }
        }
    }
    igEndChild();
}

static void draw_query_panel(Mel_EdEntities* ed)
{
    if (!ed->world) return;

    igText("Query");

    if (igInputText("##query", ed->query_buffer, sizeof(ed->query_buffer),
        ImGuiInputTextFlags_EnterReturnsTrue, nullptr, nullptr))
    {
        if (ed->active_query)
        {
            ecs_query_fini(ed->active_query);
            ed->active_query = nullptr;
        }

        if (strlen(ed->query_buffer) > 0)
        {
            ed->active_query = ecs_query(ed->world, {
                .expr = ed->query_buffer
            });

            if (!ed->active_query)
            {
                igOpenPopup_Str("Query Error", 0);
            }
        }
    }
    igSameLine(0, 5);
    if (igButton("Run", (ImVec2){50, 0}))
    {
        if (ed->active_query)
        {
            ecs_query_fini(ed->active_query);
            ed->active_query = nullptr;
        }

        if (strlen(ed->query_buffer) > 0)
        {
            ed->active_query = ecs_query(ed->world, {
                .expr = ed->query_buffer
            });
        }
    }
    igSameLine(0, 5);
    if (igButton("Clear", (ImVec2){50, 0}))
    {
        if (ed->active_query)
        {
            ecs_query_fini(ed->active_query);
            ed->active_query = nullptr;
        }
    }

    if (igBeginPopupModal("Query Error", nullptr, 0))
    {
        igText("Failed to create query.");
        igText("Check your query syntax.");
        if (igButton("OK", (ImVec2){120, 0}))
        {
            igCloseCurrentPopup();
        }
        igEndPopup();
    }

    if (igBeginChild_Str("QueryResults", (ImVec2){0, 150}, ImGuiChildFlags_Borders, 0))
    {
        if (ed->active_query)
        {
            i32 match_count = 0;
            ecs_iter_t it = ecs_query_iter(ed->world, ed->active_query);

            if (igBeginTable("QueryTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable, (ImVec2){0, 0}, 0))
            {
                igTableSetupColumn("Entity", 0, 0, 0);
                igTableSetupColumn("Name", 0, 0, 0);
                igTableSetupColumn("Components", 0, 0, 0);
                igTableHeadersRow();

                while (ecs_query_next(&it))
                {
                    for (int i = 0; i < it.count; i++)
                    {
                        ecs_entity_t entity = it.entities[i];
                        match_count++;

                        igTableNextRow(0, 0);

                        igTableSetColumnIndex(0);
                        char id_label[64];
                        snprintf(id_label, sizeof(id_label), "%" PRIu64, (u64)entity);
                        if (igSelectable_Bool(id_label, ed->selected_entity == entity,
                            ImGuiSelectableFlags_SpanAllColumns, (ImVec2){0, 0}))
                        {
                            ed->selected_entity = entity;
                        }

                        igTableSetColumnIndex(1);
                        const char* entity_name = ecs_get_name(ed->world, entity);
                        igText("%s", entity_name ? entity_name : "-");

                        igTableSetColumnIndex(2);
                        const ecs_type_t* type = ecs_get_type(ed->world, entity);
                        igText("%d", type ? type->count : 0);
                    }
                }

                igEndTable();
            }

            igText("Matches: %d", match_count);
        }
        else
        {
            igTextDisabled("Enter a query and press Run or Enter");
            igTextDisabled("Examples: Mel_CTransform, Mel_Sprite");
            igTextDisabled("          Mel_CTransform, Mel_CPlayer");
        }
    }
    igEndChild();
}

static void draw_world_stats(Mel_EdEntities* ed)
{
    if (!ed->world) return;

    igText("World Statistics");

    ed->refresh_timer -= 0.016f;
    if (ed->refresh_timer <= 0)
    {
        ed->refresh_timer = ed->refresh_interval;

        ed->entity_count = 0;
        ed->table_count = 0;
        ed->component_count = 0;
        ed->system_count = 0;

        ecs_iter_t it = ecs_each_id(ed->world, ecs_id(Mel_CTransform));
        while (ecs_each_next(&it))
        {
            ed->entity_count += it.count;
        }

        ecs_iter_t comp_it = ecs_each_id(ed->world, ecs_id(EcsComponent));
        while (ecs_each_next(&comp_it))
        {
            ed->component_count += comp_it.count;
        }

        ecs_iter_t sys_it = ecs_each_id(ed->world, ecs_pair(EcsDependsOn, EcsOnUpdate));
        while (ecs_each_next(&sys_it))
        {
            ed->system_count += sys_it.count;
        }
    }

    if (igBeginChild_Str("WorldStats", (ImVec2){0, 100}, ImGuiChildFlags_Borders, 0))
    {
        igColumns(2, "StatsColumns", false);

        igText("Entities:");
        igNextColumn();
        igText("%d", ed->entity_count);
        igNextColumn();

        igText("Components:");
        igNextColumn();
        igText("%d", ed->component_count);
        igNextColumn();

        igText("Systems:");
        igNextColumn();
        igText("%d", ed->system_count);
        igNextColumn();

        igText("Selected:");
        igNextColumn();
        if (ed->selected_entity != 0)
        {
            const char* selected_name = ecs_get_name(ed->world, ed->selected_entity);
            igText("%s", selected_name ? selected_name : "(unnamed)");
        }
        else
        {
            igTextDisabled("None");
        }

        igColumns(1, nullptr, false);
    }
    igEndChild();
}

static void draw_entity_creation(Mel_EdEntities* ed)
{
    if (!ed->world) return;

    igText("Create Entity");

    igInputText("Name##newentity", ed->entity_name_buffer, sizeof(ed->entity_name_buffer), 0, nullptr, nullptr);

    if (igButton("Create Empty", (ImVec2){100, 0}))
    {
        ecs_entity_t entity = ecs_new(ed->world);
        if (strlen(ed->entity_name_buffer) > 0)
        {
            ecs_set_name(ed->world, entity, ed->entity_name_buffer);
        }
        ed->selected_entity = entity;
        ed->entity_name_buffer[0] = '\0';
    }

    igSameLine(0, 10);

    if (igButton("+ Transform", (ImVec2){100, 0}))
    {
        ecs_entity_t entity = ecs_new(ed->world);
        if (strlen(ed->entity_name_buffer) > 0)
        {
            ecs_set_name(ed->world, entity, ed->entity_name_buffer);
        }
        ecs_set(ed->world, entity, Mel_CTransform, {
            .pos = mel_vec2(0, 0),
            .vel = mel_vec2(0, 0)
        });
        ed->selected_entity = entity;
        ed->entity_name_buffer[0] = '\0';
    }

    igSameLine(0, 10);

    if (igButton("+ Sprite", (ImVec2){100, 0}))
    {
        ecs_entity_t entity = ecs_new(ed->world);
        if (strlen(ed->entity_name_buffer) > 0)
        {
            ecs_set_name(ed->world, entity, ed->entity_name_buffer);
        }
        ecs_set(ed->world, entity, Mel_CTransform, {
            .pos = mel_vec2(100, 100),
            .vel = mel_vec2(0, 0)
        });
        ecs_set(ed->world, entity, Mel_Sprite, {
            .size = mel_vec2(32, 32),
            .color = mel_vec4(1, 1, 1, 1)
        });
        ed->selected_entity = entity;
        ed->entity_name_buffer[0] = '\0';
    }
}

void mel_ed_entities_draw(Mel_EdEntities* ed, f32 dt)
{
    assert(ed != nullptr);
    MEL_UNUSED(dt);

    if (!ed->world)
    {
        igTextColored((ImVec4){1.0f, 0.5f, 0.5f, 1.0f}, "No ECS world attached");
        igText("Call mel_ed_entities_set_world() to connect.");
        return;
    }

    igColumns(2, "EntityEditorColumns", true);
    igSetColumnWidth(0, 300);

    draw_entity_tree(ed);
    igSeparator();
    draw_world_stats(ed);
    igSeparator();
    draw_entity_creation(ed);

    igNextColumn();

    draw_entity_inspector(ed);
    igSeparator();
    draw_query_panel(ed);

    igColumns(1, nullptr, false);
}
