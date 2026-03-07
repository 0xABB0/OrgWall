# Input Command System — MUGEN-Compatible

Replaces the current backward-scanning `input_history_check_motion` with a forward-scanning per-step FSM modeled after MUGEN/Ikemen-GO. Lives in `demos/street-carlos/`.

Reference implementation: `/Users/gabbo/repo/suck/Ikemen-GO/src/input.go`

## Why Replace What We Have

The current system is a backward scan through a ring buffer of direction snapshots. It works for simple motions (QCF, DP) but cannot express:

- Charge moves (hold back 60 frames, then forward+punch)
- Negative edge (release detection)
- Hold requirements (`/B` = must be holding back)
- Strict sequences (`>` = no other inputs allowed between steps)
- Simultaneous button+direction on the same step
- Per-step timing (individual step expiry vs global window)

MUGEN's system handles all of these with a single unified architecture.

## Architecture Overview

Four layers, bottom to top:

```
Command_List          — all commands for a character, ticked each frame, queries by name
  Command             — one named command (e.g. "Hadouken"), a live FSM
    Command_Step      — one step in the sequence (e.g. "D" or "DF" or "~60$B")
      Command_Key     — one key requirement within a step (direction or button, with modifiers)

Input_Buffer          — per-direction and per-button hold counters, updated from raw input each frame
```

## Layer 1: Input_Buffer

Replaces `Input_History`. Instead of storing a ring buffer of direction snapshots, tracks **per-input hold counters**.

### Hold Counter Semantics

Each counter is a signed i32:
- `> 0` → held for N frames (1 = just pressed this frame)
- `< 0` → released for N frames (-1 = just released this frame)
- The sign flips on state change, magnitude always starts at 1

Per-frame update rule:
```
if held AND counter <= 0: counter = 1      (transition: released → pressed)
if held AND counter > 0:  counter += 1     (continuing hold)
if !held AND counter > 0: counter = -1     (transition: pressed → released)
if !held AND counter <= 0: counter -= 1    (continuing release)
```

### SOCD Resolution

Simultaneous Opposing Cardinal Directions must be resolved **before** updating hold counters. The resolution transforms the raw `U, D, L, R` booleans.

```c
typedef u8 Socd_Mode;
#define SOCD_NEUTRAL     0
#define SOCD_UP_PRIORITY 1
#define SOCD_FIRST_WINS  2
```

**SOCD_NEUTRAL** (default): opposing directions cancel. `L+R → neither`. `U+D → neither`.

**SOCD_UP_PRIORITY**: `U+D → U` (up wins). `L+R → neither`. Standard for Hit Box / leverless controllers.

**SOCD_FIRST_WINS**: whichever direction was pressed first stays active. The second opposing direction is suppressed. Requires tracking which direction was pressed first (per axis).

Resolution is applied independently to two axes: `U/D` and `L/R`. B/F are derived from L/R after resolution.

For the `SOCD_FIRST_WINS` mode, the buffer tracks first-press state:

```c
bool socd_first[4];   // [0]=U, [1]=D, [2]=L, [3]=R — which was pressed first per axis
```

Update logic (per axis, e.g. U/D):
```
if U or D changed:
    if neither was first: mark whichever is now held as first
    if both released: clear both
if U AND D:
    suppress whichever is NOT first
```

### Tracked Counters

Directions (6 independent axes + neutral):
- `Ub, Db` — up, down (post-SOCD)
- `Lb, Rb` — absolute left, absolute right (post-SOCD)
- `Bb, Fb` — relative back, relative forward (derived from L/R using facing direction)
- `Nb` — neutral (true when no direction is held)

Previous-frame copies for release edge detection:
- `Up, Dp, Lp, Rp, Bp, Fp, Np`

Buttons (10):
- `ab, bb, cb, xb, yb, zb, sb, db, wb, mb`
- Previous: `ap, bp, cp, xp, yp, zp, sp, dp, wp, mp`

### B/F Derivation

B (back) and F (forward) are **not** raw inputs — they're computed from L/R based on facing direction:

```
if facing_right:
    F = R, B = L
else:
    F = L, B = R
```

This happens inside `input_buffer_update`, after SOCD resolution but before counter updates. Commands use B/F for facing-relative motions (QCF is `D, DF, F` regardless of which side you're on) and L/R for absolute motions (rare, but supported).

### `Input_Buffer` struct

```c
typedef struct {
    i32 Ub, Db, Lb, Rb, Bb, Fb, Nb;
    i32 ab, bb, cb, xb, yb, zb, sb, db, wb, mb;
    i32 Up, Dp, Lp, Rp, Bp, Fp, Np;
    i32 ap, bp, cp, xp, yp, zp, sp, dp, wp, mp;
    Socd_Mode socd_mode;
    bool socd_first[4];
    bool facing_right;
} Input_Buffer;
```

### `Input_Buffer` functions

```c
void input_buffer_init(Input_Buffer* buf, bool facing_right);
void input_buffer_update(Input_Buffer* buf, bool U, bool D, bool L, bool R, bool a, bool b, bool c,
                         bool x, bool y, bool z, bool s, bool d, bool w, bool m);
i32  input_buffer_state(Input_Buffer* buf, Command_Key key);
i32  input_buffer_state_charge(Input_Buffer* buf, Command_Key key);
```

`input_buffer_update`:
1. Resolve SOCD on U/D and L/R
2. Compute B/F from resolved L/R using `facing_right`
3. Compute neutral: `!(U || D || L || R || B || F)`
4. Save all current counters to previous (`Up = Ub`, etc.)
5. Update all counters using the hold-counter rule

### State Query: `input_buffer_state(buf, key) → i32`

This function is the heart of the command system. It returns a value indicating whether a key requirement is satisfied. The return semantics depend on the modifier combination. There are 8 cases total (4 modifier combos × {direction, button}).

**Convention**: positive return = condition met. For press queries, `== 1` means "just pressed this frame." For slash queries, `> 0` means "currently held." The caller interprets the return value.

#### Case 1: Hold direction, no modifiers (`!tilde && !dollar`)

Applies conflict resolution. A cardinal direction is suppressed if any non-adjacent direction was pressed more recently.

For cardinals — each direction conflicts with all others in its group minus itself:

```
State(U): conflict = -max(Bb, max(Db, Fb)),  intended = Ub
State(D): conflict = -max(Bb, max(Ub, Fb)),  intended = Db
State(B): conflict = -max(Db, max(Ub, Fb)),  intended = Bb
State(F): conflict = -max(Db, max(Ub, Bb)),  intended = Fb
State(L): conflict = -max(Db, max(Ub, Rb)),  intended = Lb
State(R): conflict = -max(Db, max(Ub, Lb)),  intended = Rb
State(N): return Nb  (no conflict resolution)

return min(conflict, intended)
```

B/F group: {U, D, B, F}. L/R group: {U, D, L, R}. U and D appear in both groups.

For diagonals — both components must be held, conflicts from their opposites:

```
State(UB): conflict = -max(Db, Fb),  intended = min(Ub, Bb)
State(UF): conflict = -max(Db, Bb),  intended = min(Ub, Fb)
State(DB): conflict = -max(Ub, Fb),  intended = min(Db, Bb)
State(DF): conflict = -max(Ub, Bb),  intended = min(Db, Fb)
State(UL): conflict = -max(Db, Rb),  intended = min(Ub, Lb)
State(UR): conflict = -max(Db, Lb),  intended = min(Ub, Rb)
State(DL): conflict = -max(Ub, Rb),  intended = min(Db, Lb)
State(DR): conflict = -max(Ub, Lb),  intended = min(Db, Rb)

return min(conflict, intended)
```

Pattern: diagonal XY conflicts with {opposite_of_X, opposite_of_Y}.

The result can be negative (meaning "not satisfied, and here's how long it's been unsatisfied"). The caller checks `== 1` for just-pressed or `> 0` for held.

#### Case 2: Hold direction with dollar (`!tilde && dollar`)

No conflict resolution. `$D` is true as long as D is held, even if F is also held. This is the "4-way fuzzy" match.

For cardinals:
```
State($F):
    if Fb > 0:
        return min(abs(Ub), abs(Db), abs(Bb), abs(Fb))
    return 0
```
Returns the minimum absolute time across ALL direction counters (ensuring the direction has been stable). Returns 0 if the direction isn't held.

For `$` diagonals: both components must be held.
```
State($DF):
    if Db > 0 AND Fb > 0:
        return min(abs(Ub), abs(Db), abs(Bb), abs(Fb))
    return 0
```

For `$N` and `~$N` (both behave identically — catch-all, ignores tilde):
```
State($N) = State(~$N) = min(abs(Ub), abs(Db), abs(Bb), abs(Fb),
                              abs(ab), abs(bb), abs(cb), abs(xb), abs(yb), abs(zb),
                              abs(sb), abs(db), abs(wb))
```
Returns "time since any input last changed." Rarely used, but exists in MUGEN.

#### Case 3: Release direction, no dollar (`tilde && !dollar`)

Detects when a direction was just released. The check ensures the direction was actually held last frame and is now released (prevents false triggers from diagonal transitions, e.g. releasing UF shouldn't trigger ~U if U was never independently held).

Guard: `counter < 0 || prev > 0`. If neither is true, return 0.

The return value uses the SAME conflict resolution as hold, but negated:
```
State(~F):
    if Fb < 0 || Fp > 0:
        conflict = -max(Db, max(Ub, Bb))
        intended = Fb
        return -min(conflict, intended)
    return 0
```

The negation makes the result positive when the release is "clean" (no conflicting directions).

For diagonal releases, BOTH components must be in a release transition:
```
State(~DF):
    if (Db < 0 || Dp > 0) AND (Fb < 0 || Fp > 0):
        conflict = -max(Ub, Bb)
        intended = min(Db, Fb)
        return -min(conflict, intended)
    return 0
```

For `~N`: returns `-Nb`.

#### Case 4: Release direction with dollar (`tilde && dollar`)

Release detection without conflict resolution.

For cardinals:
```
State(~$F):
    if Fb < 0 || Fp > 0:
        if Fb < 0:
            return -Fb
        return min(abs(Ub), abs(Db), abs(Bb))
    return 0
```

When the direction is actually released (`Fb < 0`): return `-Fb` (positive, how many frames since release).
When the direction is still held but was held last frame too (`Fp > 0`): return the minimum of all *other* direction counters' absolute values.

For `~$` diagonals: both components must be in release transition.
```
State(~$DF):
    if (Db < 0 || Dp > 0) AND (Fb < 0 || Fp > 0):
        if Db < 0 || Fb < 0:
            return -min(Db, Fb)
        return min(abs(Ub), abs(Bb))
    return 0
```

#### Case 5: Hold button (`!tilde`, key >= CK_a)

No conflict resolution. Just returns the counter directly.

```
State(a) = ab
State(b) = bb
... etc
```

#### Case 6: Release button (`tilde`, key >= CK_a)

Guard: `counter < 0 || prev > 0`. Returns negated counter (positive on release).

```
State(~a):
    if ab < 0 || ap > 0:
        return -ab
    return 0
```

### Charge Query: `input_buffer_state_charge(buf, key) → i32`

A **separate function** from `State()` with different semantics. Used exclusively for charge time verification (`chargetime > 1`). Returns the number of consecutive frames a direction has been held (or was held before release), clamped to `>= 0`.

The `ignoreRecent` helper: if a counter is exactly 1 (just pressed THIS frame), treat it as `MIN_I32` instead. This prevents a newly-pressed direction from satisfying a charge requirement on the same frame it was pressed.

```
ignoreRecent(counter):
    if counter == 1: return MIN_I32
    return counter
```

#### Case 1: Hold with dollar (`!tilde && dollar`)

Returns the raw counter. No conflict resolution.

```
StateCharge($B) = Bb
StateCharge($F) = Fb
... etc
```

For `$` diagonals: `min(component_a, component_b)`.
```
StateCharge($DF) = min(Db, Fb)
StateCharge($DB) = min(Db, Bb)
... etc
```

#### Case 2: Release with dollar (`tilde && dollar`)

Returns the **previous frame's** counter. This is critical — on the frame you release B, `Bb` is already -1, but `Bp` still holds the charge duration.

```
StateCharge(~$B) = Bp
StateCharge(~$F) = Fp
... etc
```

For `~$` diagonals: `min(prev_a, prev_b)`.
```
StateCharge(~$DF) = min(Dp, Fp)
StateCharge(~$DB) = min(Dp, Bp)
... etc
```

#### Case 3: Hold without dollar (`!tilde && !dollar`)

Applies conflict resolution AND clamps to `>= 0`. Same conflict groups as `State()`.

Cardinals:
```
StateCharge(U): conflict = -max(Bb, max(Db, Fb)),  strict = min(conflict, Ub),  return max(0, strict)
StateCharge(D): conflict = -max(Bb, max(Ub, Fb)),  strict = min(conflict, Db),  return max(0, strict)
StateCharge(B): conflict = -max(Ub, max(Db, Fb)),  strict = min(conflict, Bb),  return max(0, strict)
StateCharge(F): conflict = -max(Ub, max(Db, Bb)),  strict = min(conflict, Fb),  return max(0, strict)
StateCharge(L): conflict = -max(Ub, max(Db, Rb)),  strict = min(conflict, Lb),  return max(0, strict)
StateCharge(R): conflict = -max(Ub, max(Db, Lb)),  strict = min(conflict, Rb),  return max(0, strict)
StateCharge(N): return Nb
```

Diagonals (same pattern as `State()` — XY conflicts with {opposite_of_X, opposite_of_Y}):
```
StateCharge(DF): conflict = -max(Ub, Bb),  strict = min(conflict, min(Db, Fb)),  return max(0, strict)
StateCharge(DB): conflict = -max(Ub, Fb),  strict = min(conflict, min(Db, Bb)),  return max(0, strict)
StateCharge(UF): conflict = -max(Db, Bb),  strict = min(conflict, min(Ub, Fb)),  return max(0, strict)
StateCharge(UB): conflict = -max(Db, Fb),  strict = min(conflict, min(Ub, Bb)),  return max(0, strict)
StateCharge(UL): conflict = -max(Db, Rb),  strict = min(conflict, min(Ub, Lb)),  return max(0, strict)
StateCharge(UR): conflict = -max(Db, Lb),  strict = min(conflict, min(Ub, Rb)),  return max(0, strict)
StateCharge(DL): conflict = -max(Ub, Rb),  strict = min(conflict, min(Db, Lb)),  return max(0, strict)
StateCharge(DR): conflict = -max(Ub, Lb),  strict = min(conflict, min(Db, Rb)),  return max(0, strict)
```

#### Case 4: Release without dollar (`tilde && !dollar`)

Applies conflict resolution using **previous frame** counters for the intended direction, and `ignoreRecent` on conflicting directions. Clamped to `>= 0`.

Cardinals (B/F group — same conflict groups, `ignoreRecent` on opposing, previous-frame on intended):
```
StateCharge(~U): conflict = -max(ignoreRecent(Bb), max(ignoreRecent(Db), ignoreRecent(Fb)))
                 strict = min(conflict, Up),  return max(0, strict)
StateCharge(~D): conflict = -max(ignoreRecent(Ub), max(ignoreRecent(Bb), ignoreRecent(Fb)))
                 strict = min(conflict, Dp),  return max(0, strict)
StateCharge(~B): conflict = -max(ignoreRecent(Ub), max(ignoreRecent(Db), ignoreRecent(Fb)))
                 strict = min(conflict, Bp),  return max(0, strict)
StateCharge(~F): conflict = -max(ignoreRecent(Ub), max(ignoreRecent(Db), ignoreRecent(Bb)))
                 strict = min(conflict, Fp),  return max(0, strict)
StateCharge(~L): conflict = -max(ignoreRecent(Ub), max(ignoreRecent(Db), ignoreRecent(Rb)))
                 strict = min(conflict, Lp),  return max(0, strict)
StateCharge(~R): conflict = -max(ignoreRecent(Ub), max(ignoreRecent(Db), ignoreRecent(Lb)))
                 strict = min(conflict, Rp),  return max(0, strict)
StateCharge(~N): return Np
```

The `ignoreRecent` prevents a direction that was JUST pressed this frame from suppressing the charge value. Without this, pressing F immediately after releasing B (normal for charge moves) would make `Fb == 1`, which would dominate the conflict calculation and zero out the charge.

Diagonals (both components use previous-frame counters, `ignoreRecent` on opposing):
```
StateCharge(~UB): conflict = -max(ignoreRecent(Db), ignoreRecent(Fb))
                  strict = min(conflict, min(Up, Bp)),  return max(0, strict)
StateCharge(~UF): conflict = -max(ignoreRecent(Db), ignoreRecent(Bb))
                  strict = min(conflict, min(Up, Fp)),  return max(0, strict)
StateCharge(~DB): conflict = -max(ignoreRecent(Ub), ignoreRecent(Fb))
                  strict = min(conflict, min(Dp, Bp)),  return max(0, strict)
StateCharge(~DF): conflict = -max(ignoreRecent(Ub), ignoreRecent(Bb))
                  strict = min(conflict, min(Dp, Fp)),  return max(0, strict)
StateCharge(~UL): conflict = -max(ignoreRecent(Db), ignoreRecent(Rb))
                  strict = min(conflict, min(Up, Lp)),  return max(0, strict)
StateCharge(~UR): conflict = -max(ignoreRecent(Db), ignoreRecent(Lb))
                  strict = min(conflict, min(Up, Rp)),  return max(0, strict)
StateCharge(~DL): conflict = -max(ignoreRecent(Ub), ignoreRecent(Rb))
                  strict = min(conflict, min(Dp, Lp)),  return max(0, strict)
StateCharge(~DR): conflict = -max(ignoreRecent(Ub), ignoreRecent(Lb))
                  strict = min(conflict, min(Dp, Rp)),  return max(0, strict)
```

#### Case 5: Hold button (`!tilde`, key >= CK_a)

Returns counter directly.
```
StateCharge(a) = ab
```

#### Case 6: Release button (`tilde`, key >= CK_a)

Returns previous frame's counter.
```
StateCharge(~a) = ap
```

### How State and StateCharge Work Together

When the FSM checks a step key, two queries happen:

```
t = input_buffer_state(buf, key)

if key.slash:  match = (t > 0)       // held (any duration)
else:          match = (t == 1)       // just pressed/released this frame

if match AND key.chargetime > 1:
    match = input_buffer_state_charge(buf, key) >= key.chargetime
```

Example: `~60$B` (charge back 60 frames, then release)
1. `State(~$B)` → checks if B was just released. Returns positive if release edge detected.
2. If `State` returned positive (== 1, since no slash): `StateCharge(~$B)` → returns `Bp` (previous frame's B counter). If `Bp >= 60`, the charge is sufficient.

Example: `$F` (just pressed forward, fuzzy)
1. `State($F)` → returns positive if F is held. Since no slash, caller checks `== 1`.
2. No chargetime, so no `StateCharge` call.

Example: `/B` (holding back)
1. `State(B)` → returns conflict-resolved B counter.
2. Since slash, caller checks `> 0`.

## Layer 2: Command_Key and Command_Step

### Command_Key

One key requirement with modifiers:

```c
typedef u8 Cmd_Key_Id;
#define CK_U   0
#define CK_D   1
#define CK_B   2
#define CK_F   3
#define CK_L   4
#define CK_R   5
#define CK_UB  6
#define CK_UF  7
#define CK_DB  8
#define CK_DF  9
#define CK_UL  10
#define CK_UR  11
#define CK_DL  12
#define CK_DR  13
#define CK_N   14
#define CK_a   15
#define CK_b   16
#define CK_c   17
#define CK_x   18
#define CK_y   19
#define CK_z   20
#define CK_s   21
#define CK_d   22
#define CK_w   23
#define CK_m   24

typedef struct {
    Cmd_Key_Id key;
    bool slash;        // '/' = must be held (state > 0), not just-pressed
    bool tilde;        // '~' = release detection (state must transition from held to released)
    bool dollar;       // '$' = 4-way fuzzy (no conflict resolution on directions)
    i32 chargetime;    // charge duration requirement (0 = none)
} Command_Key;
```

Classification helpers:

```c
bool cmd_key_is_direction_press(key)   → !tilde AND key <= CK_N
bool cmd_key_is_direction_release(key) → tilde AND key <= CK_N
bool cmd_key_is_button_press(key)      → !tilde AND key >= CK_a
bool cmd_key_is_button_release(key)    → tilde AND key >= CK_a
```

### Command_Step

One step in a command sequence. Contains one or more keys that must be satisfied simultaneously.

```c
typedef struct {
    Command_Key* keys;     // dynamic array
    u32 key_count;
    bool greater;          // '>' = strict sequence (no other inputs allowed since previous step)
    bool or_logic;         // true = any key matches (OR), false = all keys must match (AND)
} Command_Step;
```

Within a step, keys are combined with either AND (`+`) or OR (`|`), never both. Mixing `+` and `|` in the same step is a parse error.

Step matching logic:
```
if step.or_logic:
    matched = false
    for each key in step.keys:
        if key_matches(key, buf):
            matched = true
            break
else:
    matched = true
    for each key in step.keys:
        if !key_matches(key, buf):
            matched = false
            break
```

Where `key_matches`:
```
t = input_buffer_state(buf, key)
if key.slash:  ok = (t > 0)
else:          ok = (t == 1)
if ok AND key.chargetime > 1:
    ok = input_buffer_state_charge(buf, key) >= key.chargetime
return ok
```

Examples:
- `D` → 1 key: `{CK_D, slash=false, tilde=false, dollar=false}`
- `DF` → 1 key: `{CK_DF}`
- `/B+a` → 2 keys AND: `{CK_B, slash=true}` + `{CK_a}` — "press A while holding back"
- `~60$B` → 1 key: `{CK_B, tilde=true, dollar=true, chargetime=60}` — "release back after 60f charge"
- `a|b|c` → 3 keys OR: any punch button

### IsDirToButton Detection

A pair of consecutive steps `(step_i, step_i+1)` is a "direction to button" transition when:
- step_i contains only direction keys (no button press or release)
- step_i+1 contains at least one button press
- step_i+1 has no `/` (hold) modifier on any key
- the two steps share no common key IDs
- OR: step_i has a direction release, step_i+1 has a non-direction-release key

This detection drives the loop order exception (see Layer 3).

## Layer 3: Command

A named command — one complete input sequence. This is the FSM.

```c
typedef struct {
    str8 name;
    Command_Step* steps;       // dynamic array
    u32 step_count;

    i32 max_time;              // global window (frames allowed to complete all steps)
    i32 cur_time;              // current global timer

    i32 max_buf_time;          // frames the command stays "active" after completion
    i32 cur_buf_time;          // current buffer timer (counts down)

    i32 max_step_time;         // per-step expiry (-1 = use max_time, 0 = no expiry)
    i32* step_timers;          // per-step frame counters

    bool* completed;           // per-step completion flags
    i32* loop_order;           // evaluation order (reverse, except dir→button pairs)
    u32 loop_order_count;

    bool complete_frame;       // true on the frame the command completes

    bool buffer_hitpause;      // buffer during hitstop
    bool buffer_pauseend;      // buffer during super pause
    bool buffer_shared;        // if true, ClearName can reset this command
    bool autogreater;          // auto-expand consecutive identical direction steps
} Command;
```

### FSM Tick: `command_tick(cmd, buf, hpbuf, pausebuf, extratime)`

Called once per frame for each command. This is the core algorithm.

Parameters beyond `cmd` and `buf`:
- `hpbuf` (bool): true if the character is in hitstop this frame
- `pausebuf` (bool): true if the game is in super pause this frame
- `extratime` (i32): extra buffer frames granted during hitpause/pause

**0. Skip buffering if not applicable**
```
if !cmd.buffer_hitpause: hpbuf = false, extratime = 0
if !cmd.buffer_pauseend: pausebuf = false, extratime = 0
```

**1. Buffer countdown** (skip during hitpause/pause)
```
if cmd.cur_buf_time > 0 AND !hpbuf AND !pausebuf:
    cmd.cur_buf_time--
```

**2. Skip blank commands** (steps == 0 means intentionally blank, used by common.cmd)
```
if step_count == 0: return
```

**3. Step timer updates**
```
any_done = false
for each step i:
    if completed[i]:
        step_timers[i]++
        if max_step_time > 0 AND step_timers[i] > max_step_time:
            completed[i] = false
            step_timers[i] = 0
            continue
        any_done = true
```

**4. Global timer**
```
if any_done:
    cur_time++
else if cur_time > 0:
    command_clear(cmd, false)   // reset all completed flags, keep buffer time
```

**5. Match inputs (in loop_order)**
```
for i in loop_order:
    if i > 0 AND not completed[i-1]:
        continue    // previous step not done yet

    matched = check_step_keys(steps[i], buf)

    if steps[i].greater AND i > 0 AND completed[i-1] AND not completed[i]:
        if greater_check_fail(cmd, i, buf):
            matched = false
            completed[i-1] = false    // rollback previous step
            step_timers[i-1] = 0

    if matched:
        completed[i] = true
        step_timers[i] = 0
        if i > 0:
            completed[i-1] = false    // prevent re-triggering
            step_timers[i-1] = 0
        if i == 0:
            cur_time = 0              // restart global timer
```

**6. Completion check**
```
complete_frame = completed[last_step]
if not complete_frame:
    if cur_time < max_time:
        return    // still within window
    // else: expired, fall through to clear
command_clear(cmd, false)
if complete_frame:
    cur_buf_time = max(cur_buf_time, max_buf_time + extratime)
```

### `command_clear(cmd, bufreset)`

```
cmd.cur_time = 0
if bufreset: cmd.cur_buf_time = 0
for each step: completed[i] = false, step_timers[i] = 0
```

### Loop Order Construction

Built once at parse time. Steps are evaluated in **reverse order** by default. This prevents a single input from satisfying two consecutive steps in the same frame.

Exception: **direction→button sequences** are evaluated in forward order so the final direction and button press can match in the same frame (critical for motions ending with a button press).

Algorithm:
```
loop_order = []
i = len(steps) - 1
while i >= 0:
    if i > 0 AND IsDirToButton(steps[i-1], steps[i]):
        find start of contiguous dir→button chain
        append [start..end] in forward order
        i = start - 1
    else:
        append i
        i--
```

### Greater Check (`>` modifier)

When step `i` has `>`, any input that happened this frame (press or release, `State == 1`) that isn't part of step `i`'s key list causes the previous step to be rolled back.

The check determines which directional group to scan based on the step's keys:
- If step uses L/R keys (`CK_L`, `CK_R`, `CK_UL`, `CK_UR`, `CK_DL`, `CK_DR`): scan L/R group
- Otherwise: scan B/F group (`CK_B`, `CK_F`, `CK_UF`, `CK_UB`, `CK_DF`, `CK_DB`)
- Always scan: `CK_U`, `CK_D`
- Always scan all buttons: `CK_a` through `CK_m`

For each checked key `k`:
1. If `input_buffer_state(buf, {k, tilde=false}) == 1` (just pressed) AND `k` with `tilde=false` is NOT in the step's key list → **fail**
2. If `input_buffer_state(buf, {k, tilde=true}) == 1` (just released) AND `k` with `tilde=true` is NOT in the step's key list → **fail**

### AutoGreater Expansion

When `autogreater` is true and two consecutive steps are identical single directions (e.g. `F, F` for a dash), they're expanded to `F, >~F, >F` — meaning "press F, then release F (strict), then press F again (strict)". This catches double-taps properly.

A "single direction" step is one with exactly one key that is a direction press or release (no buttons).

Applied automatically after parsing. The expansion can produce chains: `F, F, F` → `F, >~F, >F, >~F, >F`.

### `command_is_active(cmd) → bool`

Returns `cmd->cur_buf_time > 0`. This is what the game checks to see if a command was recently completed.

## Layer 4: Command_List

All commands for one character. Multiple commands can share the same name.

```c
typedef struct {
    Input_Buffer buffer;
    Command* commands;         // dynamic array (flat, all commands)
    u32 command_count;

    i32 default_time;          // default max_time for new commands (15)
    i32 default_step_time;     // default max_step_time (-1 = same as time)
    i32 default_buf_time;      // default max_buf_time (1)
    bool default_autogreater;  // default autogreater (true)
    bool default_buffer_hitpause;  // (true)
    bool default_buffer_pauseend;  // (true)
    bool default_buffer_shared;    // (true)
} Command_List;
```

### Multiple Commands Per Name

A single name (e.g. `"Hadouken"`) can have multiple `Command` entries, each with different command strings:

```
command_list_add(cl, "Hadouken", "~D, DF, F, a");   // QCF+A
command_list_add(cl, "Hadouken", "~D, DF, F, b");   // QCF+B
command_list_add(cl, "Hadouken", "~D, DF, F, c");   // QCF+C
```

Any one completing makes `command_list_active(cl, "Hadouken")` return true. This is how MUGEN handles "same move, any button" without needing OR logic across entire commands.

### ClearName — Piano Input Prevention

When a command completes, all other **incomplete** commands with the same name are cleared (their `completed[]` flags and `step_timers[]` are reset, but their `cur_buf_time` is preserved). This prevents "piano inputs" — mashing A, B, C in sequence to trigger three separate Hadoukens.

Only affects commands with `buffer_shared = true`.

```c
void command_list_clear_name(Command_List* cl, str8 name);
```

Called automatically during `command_list_step` after detecting completions.

### Per-Frame Update

```c
void command_list_step(Command_List* cl, bool U, bool D, bool L, bool R,
                       bool a, bool b, bool c, bool x, bool y, bool z,
                       bool s, bool d, bool w, bool m,
                       bool hpbuf, bool pausebuf, i32 extratime);
```

1. `input_buffer_update(&cl->buffer, U, D, L, R, a, b, c, x, y, z, s, d, w, m)`
2. For each command: `command_tick(cmd, &cl->buffer, hpbuf, pausebuf, extratime)`
3. Track which names had `complete_frame == true` this frame
4. For each completed name: `command_list_clear_name(cl, name)`

### Queries

```c
bool command_list_active(Command_List* cl, str8 name);
```

Returns true if **any** command with matching name has `cur_buf_time > 0`.

```c
void command_list_assert(Command_List* cl, str8 name, i32 time);
```

Force-sets `cur_buf_time = time` on all commands with matching name. Used by game logic to externally assert a command (e.g. AI triggering moves, scripted sequences).

### Adding Commands

```c
typedef struct {
    i32 time;
    i32 buf_time;
    i32 step_time;
    bool buffer_hitpause;
    bool buffer_pauseend;
    bool buffer_shared;
    bool autogreater;
} Command_Add_Opt;

void command_list_add_opt(Command_List* cl, str8 name, str8 cmd_string, Command_Add_Opt opt);
#define command_list_add(cl, name, cmd, ...) \
    command_list_add_opt((cl), (name), (cmd), (Command_Add_Opt){__VA_ARGS__})
```

Fields not specified in the opt struct pick up the Command_List's defaults.

## MUGEN CMD Syntax Reference

```
[Command]
name = "Hadouken"
command = ~D, DF, F, a        ; QCF+A
time = 15                      ; global window (frames)
buffer.time = 1                ; frames command stays active after completion
buffer.hitpause = 1            ; buffer during hitstop
buffer.pauseend = 1            ; buffer during super pause

[Command]
name = "Hadouken"
command = ~D, DF, F, b         ; same name, different button (piano-safe)

[Command]
name = "Shoryuken"
command = ~F, D, DF, a         ; DP+A (623+A)
time = 15

[Command]
name = "Sonic Boom"
command = ~60$B, F, a          ; charge back 60f, then forward+A
time = 10

[Command]
name = "holdfwd"
command = /$F                  ; holding forward (used by state controllers)
time = 1
buffer.time = 1

[Command]
name = "recovery"
command =                      ; blank command (intentionally empty)
time = 1
```

### Prefix symbols per key

`/` (slash): Key must be held (state > 0), not just-pressed (state == 1). Can also have a number `/30B` for minimum hold duration.

`~` (tilde): Release detection. Without number: negative edge (key was just released). With number `~60`: charge time requirement (key must have been held for that many frames before release).

`$` (dollar): 4-way fuzzy. Disables conflict resolution on directions. `$D` matches even if F is also held. Primary use is in charge moves (`~60$B`) where you don't want diagonal inputs to break the charge.

`>` (greater): Strict sequence. No other inputs (press or release) allowed between this step and the previous one. Applied to the entire step, not individual keys.

### Combinators within a step

- `+` = AND. All keys must match. `F+a` = forward AND punch pressed same frame
- `|` = OR. Any key matches. `a|b|c` = any punch. Cannot mix `+` and `|` in the same step

## Integration with Fighter/Move_Def

### What changes in Move_Def

Replace `Motion_Def motion` and `u8 button_mask` with:

```c
typedef struct {
    str8 name;
    // ...existing fields...
    str8 command_name;    // name of the command to check (e.g. "Hadouken")
    u32 priority;
} Move_Def;
```

The move no longer carries motion/button data directly. Instead it references a command by name.

### What changes in Fighter

Replace `Input_History history` with:

```c
typedef struct {
    // ...existing fields...
    Command_List commands;
    // ...
} Fighter;
```

### `fighter_init` changes

After creating the fighter, register commands for each move:

```c
command_list_add(&f->commands, S8("Light Punch"), S8("a"), .time = 1, .buf_time = 1);
command_list_add(&f->commands, S8("Hadouken"),     S8("~D, DF, F, a"), .time = 15, .buf_time = 1);
command_list_add(&f->commands, S8("Hadouken"),     S8("~D, DF, F, b"), .time = 15, .buf_time = 1);
command_list_add(&f->commands, S8("Hadouken"),     S8("~D, DF, F, c"), .time = 15, .buf_time = 1);
command_list_add(&f->commands, S8("Shoryuken"),    S8("~F, D, DF, a"), .time = 15, .buf_time = 1);
command_list_add(&f->commands, S8("Shoryuken"),    S8("~F, D, DF, b"), .time = 15, .buf_time = 1);
command_list_add(&f->commands, S8("Shoryuken"),    S8("~F, D, DF, c"), .time = 15, .buf_time = 1);
```

### `try_moves` changes

```c
for each move (sorted by priority):
    if !move_requirements_met(f, move): continue
    if !command_list_active(&f->commands, move->command_name): continue
    enter_move(f, move)
    return
```

No more `check_move_input`. The command system has already determined what's active.

### `fighter_on_input` / `fighter_tick` changes

```c
void fighter_tick(Fighter* f, f32 dt, ...)
{
    bool U = f->input_up;
    bool D = f->input_down;
    bool L = f->input_left;
    bool R = f->input_right;
    bool a = f->buttons_held & BTN_LP;
    bool b = f->buttons_held & BTN_MP;
    bool c = f->buttons_held & BTN_HP;
    bool x = f->buttons_held & BTN_LK;
    bool y = f->buttons_held & BTN_MK;
    bool z = f->buttons_held & BTN_HK;
    // s, d, w, m: start, d-pad shortcuts, etc.

    command_list_step(&f->commands, U, D, L, R, a, b, c, x, y, z, s, d, w, m,
                      false, false, 0);  // no hitpause/pause for now

    // ... rest of tick (locomotion, moves, combat)
}
```

## What Happens to input_history.h/c

Deleted entirely. `Input_Buffer` replaces it.

The numpad direction notation (`DIR_1` through `DIR_9`) is no longer used by the command system — MUGEN uses `U/D/B/F/L/R` and composites. The `input_dir_matches` function and the backward scan are gone.

Locomotion still needs to know "what direction is the player holding" — this can be derived from the input buffer counters:

```c
bool holding_forward = buf->Fb > 0;
bool holding_back    = buf->Bb > 0;
bool holding_up      = buf->Ub > 0;
bool holding_down    = buf->Db > 0;
```

## File Structure After

```
demos/street-carlos/
  main.c
  command.h          — Command_Key, Command_Step, Command, Command_List structs
  command.c          — FSM tick, step matching, loop order, greater check, clear_name, command string parser
  input_buffer.h     — Input_Buffer, Socd_Mode structs
  input_buffer.c     — SOCD resolution, hold counter update, state query, charge query
  fighter.h          — Fighter struct (now has Command_List instead of Input_History)
  fighter.c          — data-driven tick (now queries command_list_active)
  move.h             — Move_Def (command_name instead of Motion_Def)
  character.h        — unchanged
  carlos.h           — Carlos definition (moves reference command names)
  combat.h/c         — unchanged
```

## Command String Parser

`command_parse(str8 cmd_string, Command* out)` — parses a MUGEN-format command string into a Command.

Steps are separated by `,`. Within each step, keys are separated by `+` (AND) or `|` (OR). Mixing `+` and `|` in the same step is a parse error.

Per-key prefix parsing order: `>` (per-step, only once), then any combination of `~`, `/`, `$` (per-key). After `~`, an optional numeric literal is parsed as `chargetime`. After `/`, an optional numeric literal is also parsed as `chargetime` (minimum hold duration).

Key names: `U`, `D`, `B`, `F`, `L`, `R`, `N` for directions. `UB`, `UF`, `DB`, `DF`, `UL`, `UR`, `DL`, `DR` for diagonals. `a`, `b`, `c`, `x`, `y`, `z`, `s`, `d`, `w`, `m` for buttons.

After parsing all steps:
1. Apply AutoGreater expansion (if enabled)
2. Allocate `completed[]` and `step_timers[]` arrays
3. Build `loop_order[]`

The parser runs at init time, not per-frame. The parsed Command structs are what gets ticked.

## Future Extensions (not blocking implementation)

These are features MUGEN/Ikemen supports that we'll add when the game needs them:

- **Button remapping** (`CommandKeyRemap`): lets characters remap button letters at parse time. Add when we have character-specific button layouts.
- **AI command bypass**: AI can ignore command timing and directly assert commands. Add when we have story mode enemies.
- **Hitpause/pause buffering**: the fields exist in Command, but the fighter tick doesn't pass `hpbuf`/`pausebuf` yet. Wire it up when we add hitstop.

## Verification

1. `./nob demo street-carlos` — builds clean
2. QCF+A triggers Hadouken, not Shoryuken
3. DP (F, D, DF+A) triggers Shoryuken
4. Simple button press (A) triggers Light Punch
5. Commands don't trigger without the button press
6. Holding a direction doesn't repeatedly trigger commands (state == 1 check)
7. Multiple commands per name: QCF+B also triggers Hadouken
8. Piano prevention: mashing A, B, C during QCF doesn't produce three fireballs
9. Charge move (if defined): hold back 60f, forward+A triggers Sonic Boom
10. SOCD: pressing L+R simultaneously produces neutral (no direction)
11. Debug logging shows command completions with step progression
