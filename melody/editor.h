#ifndef MEL_EDITOR_H
#define MEL_EDITOR_H

#include "types.h"
#include "string.str8.fwd.h"
#include "editor.registry.h"
#include "texture.pool.fwd.h"
#include "tile.set.fwd.h"
#include "tile.map.fwd.h"

#include <SDL3/SDL.h>

typedef void (*Mel_TexturePickerCallback)(str8 path, void* userdata);

typedef struct
{
    Mel_EdRegistry*    registry;
    Mel_Texture_Pool*  texture_pool;
    Mel_Tileset_Pool*  tileset_pool;
    Mel_Tilemap_Pool*  tilemap_pool;
    SDL_Window*        window;

    char pending_file_path[512];
    char texture_picker_filter[128];

    bool visible;
    bool show_asset_browser;
    bool show_texture_picker;
    bool file_dialog_pending;

    Mel_TexturePickerCallback texture_picker_callback;
    void* texture_picker_userdata;
} Mel_GameEditor;

void mel_game_editor_init(Mel_GameEditor* ge, Mel_EdRegistry* registry, Mel_Texture_Pool* tex_pool, Mel_Tileset_Pool* ts_pool, Mel_Tilemap_Pool* tm_pool, SDL_Window* window);
void mel_game_editor_shutdown(Mel_GameEditor* ge);
void mel_game_editor_draw(Mel_GameEditor* ge, f32 dt);
void mel_game_editor_toggle(Mel_GameEditor* ge);

void mel_game_editor_show_texture_picker(Mel_GameEditor* ge, Mel_TexturePickerCallback cb, void* userdata);

#endif
