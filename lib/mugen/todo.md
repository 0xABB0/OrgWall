# MUGEN Library TODO

Gap analysis vs Ikemen-GO reference implementation.

## Big Missing Systems

### Projectile System
- Projectile state controller with its own HitDef
- Velocity, acceleration, friction
- Hit/miss/remove animations
- Priority-based projectile-vs-projectile collision
- Stage bound removal
- Platform behavior
- NumProjID trigger (currently returns 0)

### Explod System
- Explod controller: spawn independent animated objects
- ModifyExplod: modify active explod
- RemoveExplod: remove by ID
- ExplodBindTime: bind duration
- ExplodInterpolate: smooth interpolation
- Features: bind to characters, own transforms, palette effects, afterimages, window clipping, layer rendering
- Used everywhere: hit sparks, dust, super VFX, custom UI

### Sound System
- .snd file parser (Elecbyte binary format, group/index)
- PlaySnd implementation (currently stub)
- StopSnd
- SndPan (panning)
- PlayBgm / ModifyBgm
- ModifySnd

### Palette Effects (PalFX)
- PalFX: per-character color shift
- BGPalFX: background palette
- AllPalFX: global palette
- RemapPal: palette swap
- RemapSprite: sprite remap
- Used for: super freeze flash, hit flash, poison, power-up glow

### AfterImage System
- Full afterimage rendering (trailing ghost copies with fade)
- AfterImage controller (currently stub)
- AfterImageTime controller (currently stub)
- Used in: dashes, teleports, super attacks

### Stage System
- BGDef: background definitions
- Multiple background layers with parallax
- Camera control
- Shadow/reflection system
- Z-offset / depth
- Music integration
- Stage constants
- Partner spacing
- Currently stage bounds are just hardcoded floats in round context

### Environment Effects
- EnvShake: camera shake (amplitude, frequency, direction, phase, multiplier)
- EnvColor: screen color flash
- FallEnvShake: shake on fall impact
- All currently stubbed

### Guard Crush / Dizzy
- guardpoints / guardpointsmax / guardbreak
- dizzypoints / dizzypointsmax / dizzy
- Guard meter and guard break mechanics
- Dizzy state

### Red Life (Recoverable Damage)
- redLife / redLifeSet / redLifeAdd
- Recoverable health bar

### Score System
- ScoreAdd
- Consecutive wins tracking
- Clutch/perfect/special win tracking

### AI System
- AI input generation for CPU-controlled characters
- Configurable difficulty levels
- Needed for beat-em-up enemy AI

---

## Missing State Controllers

### Combat
- [x] HitAdd: add to hit count
- [x] HitBy: positive version (we have NotHitBy but not HitBy)
- [x] AttackMulSet: attack damage multiplier (we have DefenceMulSet)
- [x] MoveHitReset: reset movecontact/movehit/moveguarded flags
- ModifyHitDef: modify active hitdef without restarting
- AttackDist: override attack distance
- HitOverride: override normal hit response (parry, armor)
- ReversalDef + ModifyReversalDef: auto-counter on being hit
- GetHitVarSet: manually set GetHitVar values

### Sprite Transform
- AngleSet / AngleAdd / AngleMul / AngleDraw: sprite rotation
- Trans: transparency/blending (alpha, add, sub)
- Offset: sprite position offset
- OverrideClsn / TransformClsn: runtime collision box modification
- TransformSprite: runtime sprite transform

### Binding
- BindToParent: helper binds to parent position
- BindToRoot: helper binds to root position
- BindToTarget: bind self to target (inverse of TargetBind)

### Physics
- PlayerPush: push collision between characters

### Misc
- [x] LifeAdd: add to life
- [x] PowerSet: set power to value
- Text / ModifyText / RemoveText: on-screen text display
- MatchRestart: restart match
- RoundTimeSet / RoundTimeAdd: modify round timer

---

## Missing Triggers / Queries

### State History
- [x] physics: current physics type (S/C/A/N)
- [x] prevstatetype / prevmovetype: previous frame state (saved at start of mugen_cns_tick)
- prevanim: previous animation number

### Animation
- [x] animlength: total animation duration in ticks
- AnimElemVar: per-frame data (group, image, xoffset, yscale, hflip, vflip, alpha, angle, numclsn1, numclsn2)

### Combat
- combocount: current combo counter
- firstattack: who attacked first
- receiveddamage / receivedhits: total damage/hits taken this round
- movecountered: was attack countered
- hitoverridden: is hit being overridden
- clsnoverlap: collision box overlap check as trigger
- MoveHitVar: attacker-side hit info (frame, power, playerid, playerno, spark_x, spark_y, cornerpush_veloff)

### Round / Match
- roundtime / timeremaining / timeelapsed: round timer queries
- matchtime / matchno: match-level state
- consecutivewins: win streak

### Guard / Dizzy
- dizzy / dizzypoints / dizzypointsmax
- guardbreak / guardpoints / guardpointsmax

### Input
- inputtime_X: per-button input hold duration (a, b, c, x, y, z, F, B, U, D)
- selfcommand: explicit input command check

### Helper / Team
- helperindexexist: helper exists by index
- helpername: helper name query
- incustomanim / incustomstate: is in custom state (from p2stateno)
- [x] selfstatenoexist: does a state exist in this char's CNS
- teamleader / teamsize / memberno: team queries

### Visual
- angle: current sprite angle
- alpha_s / alpha_d: current alpha blend values
- scale_x / scale_y / scale_z: current scale

### Misc
- ailevelf: AI difficulty level
- gamemode: current game mode
- numplayer: player count
- const240p / const480p / const720p / const1080p: resolution scaling constants
- localcoord_x / localcoord_y: character's local coordinate system
- pos z / vel z: Z-axis (Ikemen extension, low priority)
- groundangle: ground surface angle

---

## Missing Expression Features

All done:
- [x] `:=` assignment operator
- [x] `Cond()` function (short-circuit)
- [x] Interval operators =[a,b], =(a,b), etc.  (was already implemented)
- [x] Bitwise NOT (`~`)
- [x] `**` power operator (was already implemented)
- [x] Bitwise XOR `^` (single caret, was missing between `&` and `|`)
- [x] IfElse fixed to evaluate all branches per MUGEN spec

---

## Ikemen-Specific Extensions (Low Priority)

These are Ikemen extensions, not standard MUGEN. Implement only if needed.

- Map variables (MapSet/MapAdd/ParentMapSet/RootMapSet/TeamMapSet): string-keyed variable maps
- Lua scripting integration
- Tag team mode (TagIn/TagOut)
- Dialogue / VictoryQuote / Storyboard controllers
- SaveFile / LoadFile / LoadState / SaveState
- Network/rollback (GGPO-based)
- Replay system (input serialization)
- 3D positioning (pos_z, vel_z, scale_z throughout)
- Lifebar / Screenpack / Motif system



---

mugen stage is sync. no good.
