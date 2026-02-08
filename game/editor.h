#ifndef MEL_EDITOR_H
#define MEL_EDITOR_H

#include "types.h"
#include "allocator.h"
#include "vk_context.h"
#include "vk_swapchain.h"
#include "ed_tiles.h"
#include "ed_spritesheet.h"
#include "ed_entities.h"
#include "asset_registry.h"

typedef struct
{
    SDL_Window* window;
    Mel_VkContext* vk;
    Mel_VkSwapchain* swapchain;
    const Mel_Alloc* alloc;
} Mel_Editor_Opt;

typedef struct Mel_Editor
{
    SDL_Window* window;
    VkDescriptorPool descriptor_pool;
    Mel_AssetRegistry* registry;

    Mel_EdTiles ed_tiles;
    Mel_EdSpritesheet ed_spritesheet;
    Mel_EdEntities ed_entities;

    char pending_file_path[512];
    char save_path_buffer[256];
    char texture_picker_filter[128];

    bool initialized;
    bool visible;
    bool show_main_menu;
    bool show_tile_editor;
    bool show_spritesheet_editor;
    bool show_entity_editor;
    bool show_asset_browser;
    bool show_texture_picker;
    bool file_dialog_pending;
    bool file_dialog_for_tiles;
    bool file_dialog_for_spritesheet;
} Mel_Editor;

bool mel_editor_init_opt(Mel_Editor* editor, Mel_Editor_Opt opt);
#define mel_editor_init(editor, ...) mel_editor_init_opt((editor), (Mel_Editor_Opt){__VA_ARGS__})

void mel_editor_shutdown(Mel_Editor* editor, Mel_VkContext* vk);
void mel_editor_process_event(Mel_Editor* editor, SDL_Event* event);
void mel_editor_begin_frame(Mel_Editor* editor, f32 dt);
void mel_editor_end_frame(Mel_Editor* editor, VkCommandBuffer cmd);
void mel_editor_toggle(Mel_Editor* editor);

void mel_editor_set_ecs_world(Mel_Editor* editor, ecs_world_t* world);
void mel_editor_set_asset_registry(Mel_Editor* editor, Mel_AssetRegistry* registry);

#endif
