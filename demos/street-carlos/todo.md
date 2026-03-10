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

### ~~hitpause double-decrement~~ DONE
- Removed decrement from `combat_resolve`, kept only in `mugen_cns_tick`

### ~~hitflag never checked in check_hit~~ DONE
- `check_hit` now inspects `hitdef.hitflag` for statetype match (H/L/A/D), `-`/`+` modifiers
- `parse_hitflag` handles `P`, `-`, `+` modifiers

### ~~movecontact/movehit/moveguarded return bool instead of tick count~~ DONE
- Changed from `bool` to `i32` counters (`mctime`, `movehit`, `moveguarded`)
- Set to 1 on hit/guard, incremented each tick in `mugen_cns_tick`
- Expression evaluator returns tick count

### ~~HitOver checks wrong condition~~ DONE
- `ghv.hittime` now decrements each tick, `HitOver` checks `<= 0`

### ~~HitShakeOver checks wrong field~~ DONE
- Now checks `ghv.hitshaketime <= 0`, decremented each tick in `mugen_cns_tick`

### ~~Statedef defaults wrong for type and physics~~ DONE
- Sentinel values `MUGEN_STATETYPE_U`/`MUGEN_PHYSICS_U`/`MUGEN_MOVETYPE_U` (0xFE) for explicit "unchanged"
- Omitted type defaults to S, omitted physics to N, omitted movetype to I (0xFF sentinel)

### ~~animelem is fundamentally wrong~~ DONE
- `AnimElem = N` rewrites at parse time to `AnimElemTime(N) = 0` (first tick of element N)
- `AnimElem = N, op val` rewrites to `AnimElemTime(N) op val` (compound comparison)
- Removed fractional hack from ANIMELEM query evaluator

### ~~Range operators `[x,y]` / `[x,y)` completely missing~~ DONE
- `MUGEN_EXPR_RANGE` node type with value, lo, hi, inclusive flags
- Parsed in `parse_eq` when `=`/`!=` is followed by `[` or `(`
- Supports all four variants: `[x,y]`, `(x,y]`, `[x,y)`, `(x,y)`
- `!= [x,y]` wraps range in NOT

### ~~MUGEN_PHYSICS_L value mismatch~~ DONE
- `parse_statetype_char` now returns `MUGEN_PHYSICS_L` (5) for 'L'
- Expression evaluator statetype comparison also fixed (was using hardcoded 4)

### ~~Corner push is instant snap, should be decaying velocity~~ DONE
- `cornerpush_vel` field on `Mugen_Char_State`, set on hit instead of instant snap
- Applied each frame: `pos_x += cornerpush_vel * facing`, decay `*= 0.7`, zero below 0.1
- Gated by hitpause (no push during freeze)
- `MUGEN_ASSERT_NOCORNERPUSH` flag added and checked
- Still missing: `down_cornerpush_veloff`, `airguard_cornerpush_veloff`

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
- Fixed: `movetype = I` (value 0) never applied due to `if (def->movetype)` being falsy -- now uses 0xFF sentinel

### ~~Missing const() entries~~ DONE
- All standard const() entries implemented: `size.*`, `data.*`, `velocity.*`, `movement.*`

### ~~guard.pausetime silently dropped~~ DONE
- `guard_pausetime_p1/p2` parsed and evaluated, fallback to regular pausetime

### ~~Corner push values parsed but not evaluated~~ DONE
- `ground_cornerpush_veloff`, `air_cornerpush_veloff`, `guard_cornerpush_veloff` on `Mugen_HitDef_Result`
- Defaults: ground = `guard_velocity * -1.3`, air = ground, guard = ground (per MUGEN spec)
- Applied in `combat.c` -- when victim is at stage edge, pushes attacker away instead
- NOTE: application model is wrong (instant snap vs decaying velocity) -- see Critical section

### ~~ChangeAnim doesn't reset anim timing~~ DONE
- Now resets `animelem`, `animtime`, `animelemtime`

### ~~GHV yaccel always uses victim gravity~~ DONE
- `yaccel` + `has_yaccel` on `Mugen_HitDef_Result`, parsed from HitDef params
- `populate_ghv` uses HitDef's yaccel when specified, falls back to victim's gravity

### ~~Statedef -2/-3 only checked in character CNS~~ DONE
- Falls back to `f->common_cns` if not in `f->cns`

### ~~hitonce parsed but never used~~ DONE
- `check_hit` now deactivates hitdef after first connect when `hitonce` is set

### numhits not used for combo counter
- `numhits` should increment combo counter by N, we always count 1

### ~~hitcountpersist / movehitpersist / hitdefpersist not implemented~~ DONE
- Parsed in statedef, stored as bools on `Mugen_Statedef`
- `enter_state` conditionally skips resetting hitdef/mctime/movehit/moveguarded/hitcount

### Single target pointer instead of target list
- Mugen_Char_State has a single `target` pointer
- Ikemen uses a dynamic array of target IDs with dedup, filtering by hitdef ID
- Breaks helpers, multi-hit moves, and target-filtered controllers (TargetBind -1, etc.)
- Victim side needs `targetedBy` list tracking [attackerID, jugglePoints] pairs

### No trade/priority resolution for simultaneous hits
- Both check_hit calls fire independently -- both sides always connect
- Ikemen compares priority values, supports Hit/Dodge/Miss trade types
- Higher priority wins, lower priority's hitdef is consumed
- A jab trades with a super in our engine

### Juggle tracked globally instead of per-attacker
- Single `juggle_points_remaining` counter on victim
- Ikemen tracks remaining points per attacker ID via ghv.targetedBy
- Helpers with inheritJuggle share parent's pool
- Points only subtracted during falling states, not unconditionally

### ~~Gravity controller sign bug~~ DONE
- Both Gravity controller and Air physics now use `vel_y += gravity`

### Guard logic missing key checks
- No `ASF_unguardable` flag check (attacker assert)
- No autoguard support (`ASF_autoguard`)
- No guard-KO fallthrough (guarding that would kill should become a real hit)
- `ghv.hittime` during guard uses `guard_slidetime` instead of `guard_hittime`

### ~~movetype = U (explicitly unchanged) not handled~~ DONE
- `MUGEN_MOVETYPE_U` (0xFE) sentinel, `parse_movetype_char` returns it for 'U'
- `mugen_cns_enter_state` skips assignment when value is U

### hitdefattr trigger missing
- Needs special parsing: `hitdefattr = SCA, NA, SA` is a bitmask comparison
- Checks: attacker must be in movetype A, AND hitdef attr matches bitmask
- Currently unknown identifier returns 0, so `hitdefattr = SC, NA` evaluates as `0 == 0` = true
- Super move combo triggers are always true because of this

### p2bodydist X semantics differ from Ikemen
- We clamp negative distances to 0 -- Ikemen does not
- We don't select opponent front vs back width based on relative facing
- We don't apply integer truncation for WinMugen compatibility

### GetHitVar population incomplete
- Missing: guardflag, playerid/playerno/hitid/projid, p2getp1state, xaccel, fall_envshake_*, fall_animtype, down_recover/down_recovertime, power/hitpower/guardpower, facing, kill, fallcount
- No selective reset before populating (should preserve stacking fields like hitcount/damage while resetting per-hit fields)
- hitcount always increments -- should be set to 1 if not already in combo

### canrecover uses state time instead of fall time
- We check `state->time >= ghv.fall_recovertime`
- Ikemen uses separate `fallTime` counter tracking time in falling state
- These diverge when entering fall state at non-zero state time

### Body push too simplistic
- Missing weight-based push factors (heavier chars pushed less)
- Missing push priority (higher priority chars don't get pushed)
- Missing per-character push disable flag (CSF_playerpush)
- No Y/Z overlap check

### ~~Trigger redirections (partial)~~ DONE
- `helper(id),X`, `root,X`, `parent,X` redirect parsing and evaluation implemented
- `MUGEN_EXPR_REDIRECT` type with `MUGEN_REDIRECT_HELPER/ROOT/PARENT`
- Still missing: `enemy,X`, `target(id),X`, `partner,X`, `playerid(N),X`

### Missing triggers (significant ones)
- p2bodydist Y, p2stateno, p2life, physics
- prevstatetype, prevmovetype, prevanim
- hitdefattr, hitbyattr, movereversed, uniqhitcount
- hitpausetime, hitvel x/y
- numtarget (with optional hitID arg)
- animelemno, animexist (state owner's anims vs selfanimexist)
- win/lose/drawgame/matchover
- screenpos x/y, screenwidth/height
- timemod (special syntax: `timemod = N, value`)
- isasserted (test assert flags)
- math functions: exp, ln, log, cos, sin, tan, acos, asin, atan
- utility functions: max, min, round, clamp, sign, cond

### Missing HitDef parameters
- id/chainid/nochainid -- hit ID chaining system
- kill/guard.kill/fall.kill -- whether hit can KO
- affectteam -- which team the hit affects
- priority.type -- Hit/Miss/Dodge (we parse priority value but not type)
- p1sprpriority/p2sprpriority
- forcecrouch/forcenofall
- down.hittime/down.bounce/down.velocity/down.cornerpush.veloff/down.recover/down.recovertime
- mindist/maxdist/snap -- positioning constraints
- guard.dist/guard.hittime
- airguard.ctrltime/airguard.cornerpush.veloff
- xaccel/zaccel (we only have yaccel)
- envshake.*/fall.envshake.* -- screen shake on hit
- givepower (we have getpower but not givepower)
- unhittabletime
- stand.friction/crouch.friction

### NotHitBy missing slot/stack system
- Ikemen supports multiple hitby slots indexed by `slot` parameter
- We store a single nothitby_attr/nothitby_time pair
- Missing `value2`, `stack`, `playerno`/`playerid` params

### Width missing edge parameter
- Ikemen Width controller has: `player` (front, back), `edge` (front, back), `value` (sets both)
- We only parse `player` (as front/back)

### ~~poweradd parsed but never applied~~ DONE
- `mugen_cns_enter_state` now applies `poweradd` on state entry

### sprpriority parsed but never applied
- Statedef `sprpriority` field parsed but `mugen_cns_enter_state` never reads it
- Need a `sprpriority` field on `Mugen_Char_State` and apply on state entry

### ~~numhelper always returns 0~~ DONE
- Uses `query_num_helper` callback to count helpers, optionally filtered by ID

### Unknown triggers silently return 0
- mugen_expr.c line 492: unknown identifiers fall through to `make_int(p, 0)`
- Should produce errors or warnings -- makes bugs invisible
- Typos in character files are silently accepted

### Persistent counter edge cases during hitpause
- Ikemen backs up persistent counters from old state during hitpause transitions
- Special correction logic for hitpause-aware state changes
- We always zero all persistent counters unconditionally

### ChangeState missing `continue` parameter
- Ikemen supports `continue = 1` to prevent stopping controller execution after state change
- Our tick_controllers always breaks on state_changed

### Missing controllers (gameplay-relevant)
- `HitBy` -- we only have NotHitBy, HitBy is the inverse
- `HitOverride` -- custom hit response (intercept specific hit types)
- `ReversalDef` / `ModifyReversalDef` / `ModifyHitDef` -- counter/reversal system
- `BindToTarget` / `BindToParent` / `BindToRoot` -- position binding
- `PlayerPush` -- enable/disable push for character
- `AttackDist` -- override attack distance for proximity guard
- `AttackMulSet` -- attack power multiplier
- `LifeAdd` -- add/subtract life (different from LifeSet)
- `PowerSet` -- set power to specific value
- `TargetDrop` -- remove targets from target list
- `TargetVelSet` / `TargetVelAdd` -- set/add velocity on target
- `MoveHitReset` -- reset movecontact/movehit state
- `ParentVarSet` / `ParentVarAdd` / `RootVarSet` / `RootVarAdd` -- cross-entity var access
- `Trans` -- transparency/blending
- `Angle*` -- rotation controls
- `EnvColor` -- screen color overlay
- `Offset` -- drawing offset

## Low (cosmetic / future features)

### ~~Helper/DestroySelf system~~ DONE
- `MUGEN_SC_HELPER` and `MUGEN_SC_DESTROYSELF` implemented
- Helper spawning via `helper_spawn_pending` fields, destruction via `destroy_self_pending`
- Query callbacks: `query_num_helper`, `query_helper_state`, `query_root_state`
- `MUGEN_EXPR_REDIRECT` with helper/root/parent target types

### Projectile system
- Projectiles still mapped to `MUGEN_SC_NULL`
- Used by poi-son.cns

### Visual effect controllers (all no-op, fine for now)
- `Explod` / `ModifyExplod` / `RemoveExplod` -- spark/dust visual effects
- `MakeDust` -- dust particles on landing
- `PalFX` / `AllPalFX` / `BgPalFX` / `RemapPal` -- palette effects (flash on hit)
- `EnvShake` -- screen shake on big hits
- `GameMakeAnim` -- game-level animation

### SuperPause parsed but not executed
- Full params parsed (time, anim, sound, pos, poweradd)
- Execution is just `break`
- Should freeze game with visual overlay

### PlaySnd parsed but not executed
- Group/index/channel parsed, execution is `break`
- Needs sound system integration

### Clipboard controllers
- `DisplayToClipboard` / `AppendToClipboard` / `ClearClipboard`
- Debug/display feature, low priority

## Code quality

### ~~extern declaration inside function body~~ DONE
- `mugen_expr.c` now includes `command.h`

### ~~Duplicate reset in enter_state~~ DONE
- Single reset location now, guarded by persist flags

### Magic numbers (all correct per MUGEN spec, but undocumented)
- `mugen_cns.exec.c:286` -- `fall_vel_y` default `-4.5f`
- `mugen_cns.exec.c:284` -- `fall_recovertime` default `4`
- `mugen_cns.exec.c:287` -- `priority` default `4`
- `mugen_cns.exec.c:283` -- `fall_recover` default `1` (true)
- `fighter.c:349` -- ground plane at `y = 0.0f`
- `fighter.c:406` -- default hurtbox height `60.0f`
- `combat.c:80-83` -- guard states 150/152/154
- `combat.c:55-63` -- hit states 5000+/5010+/5020+ animtype

### Both fighters share same Mugen_Cns data
- main.c line 352-353: both p1 and p2 use `s_mugen_char.cns`, `common_cns`, `cmd_cns`
- persistent_counter for persistent=0 controllers is on the controller struct, shared between fighters
- If fighter 1 triggers a persistent=0 controller, fighter 2 can never trigger it
- Each fighter needs its own copy of the statedef controller arrays (or move persistent_counter to per-fighter state)
