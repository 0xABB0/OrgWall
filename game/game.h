#ifndef MEL_GAME_H
#define MEL_GAME_H

#include "core.types.h"
#include "ecs.world.h"
#include "ecs.2d.transform.h"
#include "ecs.2d.sprite.h"
#include "ecs.2d.collider.h"
#include "player.h"
#include "npc.h"
#include "wall.h"
#include "render.list.fwd.h"
#include <SDL3/SDL_events.h>

typedef struct
{
    Mel_ECS ecs;

    ecs_entity_t player;
    ecs_entity_t npc;

    bool dialogue_open;
    const char* dialogue_text;

    bool input_interact;
} Mel_Game;

void mel_game_init(Mel_Game* game);
void mel_game_shutdown(Mel_Game* game);

void mel_game_handle_event(Mel_Game* game, SDL_Event* event);
void mel_game_update(Mel_Game* game, f32 dt);
void mel_game_draw(Mel_Game* game, Mel_Render_List* list);

bool mel_game_can_interact(Mel_Game* game);

#endif
