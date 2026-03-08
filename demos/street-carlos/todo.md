# Street Carlos — VM TODO

## Critical (game logic breaks)

### rand() not deterministic
- `mugen_expr.c:801` — `MUGEN_QUERY_RANDOM` uses `rand() % 1000`
- `mugen_cns.exec.c:244` — `MUGEN_SC_VARRANDOM` uses `rand()`
- MUGEN random returns 0-999 (range is correct), but `rand()` is platform-dependent
- For rollback netcode this WILL desync
- Fix: add a seeded PRNG (e.g. `mel_rng`) to `Mugen_Char_State`, use it everywhere

### Throw system entirely non-functional
- `TargetBind` — mapped to `MUGEN_SC_NULL`, does nothing. Should bind opponent to attacker's position
- `TargetState` — mapped to `MUGEN_SC_NULL`. Should put throw victim into a custom state
- `TargetLifeAdd` — mapped to `MUGEN_SC_NULL`. Should apply damage to throw victim
- `TargetFacing` — mapped to `MUGEN_SC_NULL`. Should force victim facing direction
- `TargetPowerAdd` — mapped to `MUGEN_SC_NULL`. Should give power to victim
- `ChangeAnim2` — mapped to `MUGEN_SC_NULL`. Should change the opponent's animation (used in throws)
- Used by: kfm.cns (states 800-821), poi-son.cns
- Requires: target tracking system (who did we hit, reference to their state)

### ignorehitpause never checked
- `Mugen_State_Controller.ignorehitpause` is parsed and stored but never used
- `tick_controllers` in `mugen_cns.exec.c` doesn't check `state->hitpause_time` vs `sc->ignorehitpause`
- Controllers with `ignorehitpause = 1` should execute even when `hitpause_time > 0`
- Statedef -2 controllers commonly use this

### Animation doesn't freeze during hitpause
- `fighter.c:302` — `mel_anim_player_update` called unconditionally every tick
- During hitpause, animation should freeze (no advance)
- Fix: skip `mel_anim_player_update` when `st->hitpause_time > 0`

### AssertSpecial is a no-op
- Mapped to `MUGEN_SC_NULL` in parser
- Used by common1.cns to prevent guard recovery, walk, crouch during certain states
- Flags: `nostandguard`, `nocrouchguard`, `noairguard`, `nowalk`, `noautoturn`, `nojugglecheck`, etc.
- Without it, characters can act during states they shouldn't

### AnimElemTime semantics wrong
- `mugen_expr.c:771-778` — returns `state->animelem - elem` (element index difference)
- MUGEN's `AnimElemTime(N)` returns the TIME since element N started playing
- Requires per-element timing which we don't track
- Used extensively in common1.cns and kfm.cns for timing-based conditions

## Medium (incorrect behavior)

### P2BodyDist X == P2Dist X (should differ)
- `mugen_expr.c:806-817` — both return identical `p2_pos_x - pos_x`
- `P2BodyDist X` should account for the opponent's collision box (front/back width)
- `state->p2_width` exists on the struct but is never populated by `combat_resolve`

### Guard damage can kill
- `combat.c:101-102` — guard damage reduces life, clamps to 0
- In MUGEN, guard/chip damage cannot kill (life stops at 1)
- Fix: `if (vst->life < 1) vst->life = 1;`

### Liedown physics conflated with MUGEN_PHYSICS_N
- `mugen_cns.exec.c:464` — liedown is `case 4:` which IS `MUGEN_PHYSICS_N`
- MUGEN physics types: S=1, C=2, A=3, N=4, U=5 (unchanged)
- Liedown (`L`) is a STATE TYPE, not a physics type
- Current code overloads `MUGEN_PHYSICS_N` to mean both "no physics" and "liedown physics"
- Liedown physics: apply stand friction + ground movement (currently correct behavior)
- N physics: no friction, just pos += vel (currently WRONG — N also applies friction because of the overload)
- Fix: define `MUGEN_STATETYPE_L = 4` separately, don't use it in the physics switch. Liedown physics should be a separate case or use the statedef's physics field

### No juggle point tracking
- MUGEN has a juggle system where air combos consume juggle points
- Without this, infinite air combos are possible
- Needs: `airjuggle` from character constants, tracking consumed points per combo

### Missing const() entries
- `mugen_expr.c:821-844` handles velocity/friction constants but missing:
  - `size.ground.front` / `size.ground.back`
  - `size.air.front` / `size.air.back`
  - `size.height`
  - `size.xscale` / `size.yscale`
  - `data.defence`
  - `data.sparkno` / `data.guard.sparkno`
  - `data.liedown.time`
  - `data.airjuggle`
  - `data.life`
  - `data.power`
  - `movement.airjump.num` / `movement.airjump.height`

### guard.pausetime silently dropped
- `mugen_cns.c:668-670` — empty block with comment `/* reuse pausetime for guard if needed */`
- Guard pausetime is a separate pair of values in MUGEN
- Currently uses the regular pausetime for guard too

### Corner push values parsed but not evaluated
- `Mugen_HitDef_Params` has `ground_cornerpush`, `air_cornerpush`, `guard_cornerpush`
- These are never read in `exec_controller`'s HitDef case
- Corner push determines extra knockback when victim is at stage edge

### ChangeAnim doesn't reset anim timing
- `mugen_cns.exec.c:171-178` — only sets `state->anim` and `state->pending_anim`
- Should reset `animtime`, `animelem`, `animelemtime` on anim change
- Stale timing values persist until `sync_animtime` runs next frame

### GHV yaccel always uses victim gravity
- `combat.c:42` — `ghv->yaccel = victim->gravity`
- Attacker's HitDef can specify a custom `yaccel` for the victim's fall
- `Mugen_HitDef_Params` doesn't have a `yaccel` field

### Statedef -2/-3 only checked in character CNS
- `fighter.c:308-318` — only looks up from `f->cns`
- MUGEN: statedef -2/-3 from common.cns should also run if character doesn't define them

## Low (cosmetic / future features)

### Helper/Projectile/DestroySelf system
- All mapped to `MUGEN_SC_NULL`
- `Helper` — spawns helper entities. Used by poi-son.cns
- `Projectile` — spawns projectiles. Used by poi-son.cns
- `DestroySelf` — destroys helper entities. Used by poi-son.cns
- Needed for poi-son to function, not needed for KFM basic moves

### Visual effect controllers (all no-op, fine for now)
- `Explod` — spark/dust visual effects
- `MakeDust` — dust particles on landing
- `PalFX` / `RemapPal` — palette effects (flash on hit)
- `EnvShake` — screen shake on big hits
- `GameMakeAnim` — game-level animation

### Other no-op controllers
- `HitOverride` — custom hit response (intercept specific hit types)
- `ReversalDef` — counter-hit / reversal system
- `ScreenBound` — controls if character is bounded to screen edges
- `Pause` — freezes game for N ticks
- `VarRangeSet` — sets a range of vars to a value (used in state 5900 round init)
- `Width` — parsed with front/back values, but execution is `break`. Should modify collision box

### SuperPause parsed but not executed
- Full params parsed (time, anim, sound, pos, poweradd)
- Execution is just `break`
- Should freeze game with visual overlay

### DefenceMulSet parsed but not executed
- Value parsed, execution is `break`
- Should multiply incoming damage by a factor

### PlaySnd parsed but not executed
- Group/index/channel parsed, execution is `break`
- Needs sound system integration

## Code quality

### extern declaration inside function body
- `mugen_expr.c:675-676` — `command_list_active` declared via inline `extern` in the evaluator
- Should be a proper header include

### Duplicate reset in enter_state
- `mugen_cns.exec.c:30-32` and `53-55` — `movecontact`/`movehit`/`moveguarded` reset twice
- Copy-paste artifact, delete one

### Magic numbers (all correct per MUGEN spec, but undocumented)
- `mugen_cns.exec.c:286` — `fall_vel_y` default `-4.5f`
- `mugen_cns.exec.c:284` — `fall_recovertime` default `4`
- `mugen_cns.exec.c:287` — `priority` default `4`
- `mugen_cns.exec.c:283` — `fall_recover` default `1` (true)
- `fighter.c:349` — ground plane at `y = 0.0f`
- `fighter.c:406` — default hurtbox height `60.0f`
- `combat.c:80-83` — guard states 150/152/154
- `combat.c:55-63` — hit states 5000+/5010+/5020+ animtype
- `mugen_expr.c:801` — random range 0-999
