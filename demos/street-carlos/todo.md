# Street Carlos -- VM TODO

## Critical (game logic breaks)

### ~~rand() not deterministic~~ DONE
- Per-state xorshift64 PRNG via `Mugen_Char_State.rng_state`
- Used in both `MUGEN_QUERY_RANDOM` and `MUGEN_SC_VARRANDOM`

### ~~Throw system entirely non-functional~~ DONE
- `TargetBind` -- binds victim to attacker position with offset, clears velocity, sets `ghv.isbound`
- `TargetState` -- puts victim into attacker's CNS state (cross-CNS execution via `state_owner_cns`)
- `TargetLifeAdd` -- damages victim with kill/absolute flags
- `TargetFacing` -- forces victim facing relative to attacker
- `TargetPowerAdd` -- adds power to victim
- `ChangeAnim2` -- changes victim's animation, resets anim timing
- `SelfState` -- clears `state_owner_cns` to return to own CNS
- Target pointer set in `combat.c` on hit connection
- Cross-CNS: `fighter.c` resolves statedefs via `state_owner_cns` when set
- 13 tests covering all throw controllers

### ~~ignorehitpause never checked~~ DONE
- `tick_controllers` now skips non-ignorehitpause controllers when `hitpause_time > 0`
- Physics and time gated behind hitpause check

### ~~Animation doesn't freeze during hitpause~~ DONE
- `fighter.c` -- `mel_anim_player_update` skipped when `st->hitpause_time > 0`

### ~~AssertSpecial is a no-op~~ DONE
- Implemented: `nostandguard`, `nocrouchguard`, `noairguard`, `nowalk`, `noautoturn`, `nojugglecheck`
- Assert flags cleared per tick, controllers OR flags in

### ~~AnimElemTime semantics wrong~~ DONE
- Per-element start ticks precomputed from clip keyframe times in `sync_elem_ticks`
- `AnimElemTime(N)` = `time - elem_start_ticks[N-1]` (negative = not started, 0 = just started, positive = elapsed)
- `AnimElem = N` only true on first tick of element N (returns fractional on subsequent ticks)
- `animelemtime` tracked per-element transition in `sync_animtime`

## Medium (incorrect behavior)

### ~~P2BodyDist X == P2Dist X~~ DONE
- Now subtracts `ground_front + p2_width` from center distance

### ~~Guard damage can kill~~ DONE
- Guard damage clamps life to 1

### ~~Liedown physics conflated with MUGEN_PHYSICS_N~~ DONE
- `MUGEN_PHYSICS_L = 5` separated from `MUGEN_PHYSICS_N = 4`

### ~~No juggle point tracking~~ DONE
- `juggle_points_remaining` on `Mugen_Char_State`, initialized from `data.airjuggle` (default 15)
- `current_juggle` set from statedef's `juggle` field on state entry, copied to `Mugen_HitDef_Result`
- Air hits check `juggle_points_remaining >= hitdef.juggle` before connecting (respects `nojugglecheck`)
- Points consumed on air hit, reset when movetype transitions from H to non-H
- Fixed: `movetype = I` (value 0) never applied due to `if (def->movetype)` being falsy — now uses 0xFF sentinel

### ~~Missing const() entries~~ DONE
- All standard const() entries implemented: `size.*`, `data.*`, `velocity.*`, `movement.*`

### ~~guard.pausetime silently dropped~~ DONE
- `guard_pausetime_p1/p2` parsed and evaluated, fallback to regular pausetime

### ~~Corner push values parsed but not evaluated~~ DONE
- `ground_cornerpush_veloff`, `air_cornerpush_veloff`, `guard_cornerpush_veloff` on `Mugen_HitDef_Result`
- Defaults: ground = `guard_velocity * -1.3`, air = ground, guard = ground (per MUGEN spec)
- Applied in `combat.c` — when victim is at stage edge, pushes attacker away instead

### ~~ChangeAnim doesn't reset anim timing~~ DONE
- Now resets `animelem`, `animtime`, `animelemtime`

### ~~GHV yaccel always uses victim gravity~~ DONE
- `yaccel` + `has_yaccel` on `Mugen_HitDef_Result`, parsed from HitDef params
- `populate_ghv` uses HitDef's yaccel when specified, falls back to victim's gravity

### ~~Statedef -2/-3 only checked in character CNS~~ DONE
- Falls back to `f->common_cns` if not in `f->cns`

## Low (cosmetic / future features)

### Helper/Projectile/DestroySelf system
- All mapped to `MUGEN_SC_NULL`
- `Helper` -- spawns helper entities. Used by poi-son.cns
- `Projectile` -- spawns projectiles. Used by poi-son.cns
- `DestroySelf` -- destroys helper entities. Used by poi-son.cns
- Needed for poi-son to function, not needed for KFM basic moves

### Visual effect controllers (all no-op, fine for now)
- `Explod` -- spark/dust visual effects
- `MakeDust` -- dust particles on landing
- `PalFX` / `RemapPal` -- palette effects (flash on hit)
- `EnvShake` -- screen shake on big hits
- `GameMakeAnim` -- game-level animation

### Other no-op controllers
- `HitOverride` -- custom hit response (intercept specific hit types)
- `ReversalDef` -- counter-hit / reversal system
- `ScreenBound` -- controls if character is bounded to screen edges
- `Pause` -- freezes game for N ticks
- ~~`VarRangeSet`~~ DONE
- ~~`Width`~~ DONE

### SuperPause parsed but not executed
- Full params parsed (time, anim, sound, pos, poweradd)
- Execution is just `break`
- Should freeze game with visual overlay

### ~~DefenceMulSet parsed but not executed~~ DONE
- Sets `state->defence_mul`, applied to hit and guard damage

### PlaySnd parsed but not executed
- Group/index/channel parsed, execution is `break`
- Needs sound system integration

## Code quality

### ~~extern declaration inside function body~~ DONE
- `mugen_expr.c` now includes `command.h`

### ~~Duplicate reset in enter_state~~ (still present, minor)
- `mugen_cns.exec.c:30-32` and `53-55` -- `movecontact`/`movehit`/`moveguarded` reset twice
- Copy-paste artifact, delete one

### Magic numbers (all correct per MUGEN spec, but undocumented)
- `mugen_cns.exec.c:286` -- `fall_vel_y` default `-4.5f`
- `mugen_cns.exec.c:284` -- `fall_recovertime` default `4`
- `mugen_cns.exec.c:287` -- `priority` default `4`
- `mugen_cns.exec.c:283` -- `fall_recover` default `1` (true)
- `fighter.c:349` -- ground plane at `y = 0.0f`
- `fighter.c:406` -- default hurtbox height `60.0f`
- `combat.c:80-83` -- guard states 150/152/154
- `combat.c:55-63` -- hit states 5000+/5010+/5020+ animtype
