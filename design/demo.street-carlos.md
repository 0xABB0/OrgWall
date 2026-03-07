# Street Carlos — Game Architecture

A fighting game demo (1v1 + beat-em-up story mode) built on top of the Melody engine.
Lives in `demos/street-carlos/`.

## Core Data Model

### Move_Def

The atomic unit. Every attack, special, super — it's all a Move_Def.

```c
typedef u32 Move_Phase;
#define MOVE_PHASE_NONE     0
#define MOVE_PHASE_STARTUP  1
#define MOVE_PHASE_ACTIVE   2
#define MOVE_PHASE_RECOVERY 3

typedef struct {
    str8 name;

    Motion_Def motion;
    u8 button_mask;
    u32 priority;

    bool requires_ground;
    bool requires_air;
    bool requires_crouch;

    u32 startup;
    u32 active;
    u32 recovery;

    f32 hit_x, hit_y, hit_w, hit_h;

    f32 damage;
    f32 knockback_x;
    f32 knockback_y;
    u32 hitstun;

    bool airborne;
    f32 launch_vel_x;
    f32 launch_vel_y;

    bool spawns_projectile;
    f32 projectile_speed;
} Move_Def;
```

Duration is implicit: `startup + active + recovery`.
Hitbox is relative to fighter feet position, facing right. Flipped at runtime when facing left.
Priority determines check order — DP (20) > QCF (10) > normals (0). Higher wins.

### Character_Def

Template for a character. Defines stats and default boxes.

```c
typedef struct {
    str8 name;

    f32 walk_speed;
    f32 jump_vel;
    f32 gravity;

    f32 width;
    f32 height;
    f32 crouch_height;

    f32 hurt_x, hurt_y, hurt_w, hurt_h;
    f32 crouch_hurt_x, crouch_hurt_y, crouch_hurt_w, crouch_hurt_h;
} Character_Def;
```

### Fighter (runtime)

Instance of a character in a match.

```c
typedef u32 Locomotion;
#define LOCO_IDLE          0
#define LOCO_WALK_FORWARD  1
#define LOCO_WALK_BACK     2
#define LOCO_CROUCH        3
#define LOCO_JUMP          4
#define LOCO_HITSTUN       5

typedef struct {
    Character_Def* character;

    Locomotion locomotion;

    Move_Def* current_move;
    Move_Phase move_phase;
    u32 move_frame;
    bool hit_confirmed;

    f32 x, y;
    f32 vel_x, vel_y;
    bool facing_right;

    f32 health;
    u32 hitstun_remaining;

    Input_History history;
    Projectile projectiles[4];

    Move_Def* moves;
    u32 move_count;
} Fighter;
```

`moves` / `move_count` — the fighter's available move list. Points into a registry or a local array.
Adding a move at runtime = append to this list. Buying moves in the shop = same operation.

## Fighter Tick (data-driven)

The tick is a generic executor. No move-specific code paths.

```
1. input_history_tick
2. update projectiles
3. if hitstun > 0: decrement, return
4. if current_move != NULL:
     advance move_frame
     compute phase from frame vs startup/active/recovery
     if airborne: apply gravity, air movement
     if move complete (frame >= total duration):
       current_move = NULL, return to locomotion
     return
5. neutral state — check inputs:
     sort available moves by priority (descending)
     for each move:
       if requirements met (ground/air/crouch) AND input matches:
         enter move (set current_move, move_frame=0, apply launch velocities)
         return
     (if no move matched, check locomotion: jump, crouch, walk, idle)
```

No switch statements on state IDs. No per-move if/else chains.
New move = new Move_Def entry. The tick handles it automatically.

## Hit Detection

Per tick, after all fighters update:

```
for each attacker:
  if attacker.move_phase == ACTIVE and not hit_confirmed:
    compute world-space hitbox (flip if facing left)
    for each defender (that isn't the attacker):
      compute world-space hurtbox from defender's state
      if AABB overlap:
        attacker.hit_confirmed = true
        apply damage, knockback, hitstun to defender
        break

for each projectile:
  for each defender:
    if AABB overlap:
      apply damage, hitstun
      deactivate projectile
```

hit_confirmed resets when entering a new move or returning to neutral.
Hurtbox comes from Character_Def defaults (standing or crouching).
Move_Def can override the hurtbox (not yet — future extension).

## Debug Visualization

Toggle with Tab. Draws colored outlines on the game framebuffer:
- Green: hurtboxes
- Red: hitboxes (only during active phase)
- Yellow: projectile hitboxes

Uses the existing sprite pass — just push colored quads with low alpha.
No separate render pass needed.

## File Structure

```
demos/street-carlos/
  main.c              — app shell, rendering, input wiring
  move.h              — Move_Def, Move_Phase
  character.h         — Character_Def, Locomotion
  fighter.h            — Fighter struct, fighter_init/tick/take_hit
  fighter.c            — data-driven fighter tick
  carlos.h             — Carlos character definition (stats + moves)
  combat.h             — hit detection interface
  combat.c             — AABB overlap, hit application
  input_history.h/c   — unchanged
```

Carlos is defined in `carlos.h` as static data. A second character = another `somename.h`.

## Future Extensions

- **Move shop**: add/remove Move_Defs from fighter's move list at runtime
- **Modding**: load Move_Defs from data files, register at startup
- **New characters**: new `character.h` file with different stats and moves
- **Cancel rules**: Move_Def gains a `cancel_into` mask — which moves can interrupt recovery
- **Blockstun**: new locomotion state, block detection based on input direction vs attack
- **Supers**: moves with meter cost, cinematic freeze
- **Story mode**: enemy AI, wave spawning, level progression
