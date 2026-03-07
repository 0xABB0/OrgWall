# Rendering

This game is simple enough not not require custom rendering code. It should integrate 100% with melody.
If something's missing from the rendering point of view, then it's melody's fault and we should change stuff inside melody to match the requirements.

## World

First important thing is that the world should be initialized to be a 2.5 world.

```c
Mel_World_Handle game_world = mel_create_world(...);
mel_world_2_5_d(game_world);
```

Then, technically, the knowledge of how to draw from the game is done.
The game adds sprites to the world, and they should be drawn just fine.

```c
mel_world_add_sprite(game_world, entity, (...));
```

The sprite is a live component: it can change freely, and is used by the animation system to handle animations
