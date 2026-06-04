# Melody Audio — Mixer-Core Spec

This document specifies `modules/audio`: a SoLoud-shaped PCM mixing and playback engine. Scope here
is the **mixer core** — the engine, the voice-handle model, the source producer, raw-PCM playback,
per-voice gain/pan/resample, and faders — across a **native backend per platform** plus an always-on
`null` backend. Filters, sub-mixer buses, 3D spatialization, visualization, and file decoders are
**out of scope** for this spec; each lands as a separate, purely-additive spec (they extend the
source/voice surface, they do not reshape it).

Bound by the Ten Commandments (`docs/commandments.md`); decisions turning on one cite it `MEL-ENGINE-N`.
Bound by the coding guidelines (`docs/coding-guidelines.md`); `MEL-CODE-NNN`.

---

## 1. Design principles

**P1 — The simple path is the powerful path.** `mel_audio_play(eng, source)` returns a voice handle
and makes sound; the same machinery serves master gain, per-voice pan, relative play-speed, and timed
faders without a second API (MEL-ENGINE-II).

**P2 — Every cost is visible and traceable.** All memory flows through a caller-supplied `Mel_Alloc`
(MEL-CODE-003); the mix thread is spawned through `thread`, named, and never "in shadow"
(MEL-ENGINE-III). The device callback steals no cycles beyond a bounded `memcpy`.

**P3 — Request and grant, never silently default.** `Mel_Audio_Opt` *requests* sample-rate, channel
count, block size, ring depth, and resampler; `Mel_Audio_Caps` *reports* what the device granted. A
mismatch is observable, never swallowed (MEL-CODE-007, MEL-ENGINE-VIII).

**P4 — Open sets, not enums.** Backends are build-axis source selection; the source set is an open
**producer interface** (one hot callback + optional lifecycle hooks, §4.2); resamplers and faders are
function-pointers; voice flags are named booleans. No closed enum gates an extensible axis
(MEL-CODE-001). Dynamic storage throughout — no `[MEL_MAX_*]` (MEL-CODE-002).

---

## 2. Backend targets

The backend is **chosen at compile time, not runtime** — exactly one native backend is linked per
platform, selected by `build.c` source gating, as the gpu module selects its backend by build variant
rather than dispatching through pointers. There is no backend handle and no `create`-time backend
argument (§6). There is **no SDL or other portable middle layer**: each backend is native and
idiomatic, ceiling-first, degrading only where the platform forces it (MEL-ENGINE-VII).

- **CoreAudio** (macOS / iOS) — `AudioUnit` (`kAudioUnitSubType_HALOutput` / `RemoteIO`) render callback.
- **WASAPI** (win32) — shared-mode, event-driven `IAudioClient` render.
- **ALSA** floor / **PipeWire** ceiling (linux) — one ABI per link, so the two are a build-time choice
  (ALSA is the portable floor compiled by default; PipeWire the age-forward ceiling, selected by a
  future `audio` build-axis).
- **AAudio** ceiling / **OpenSL ES** floor (android).
- **AudioWorklet** over Web Audio (wasm / emscripten).

Each is a native §6 ring consumer, platform-gated in `build.c` — none is a roadmap stub. `mel_unavailable`
is reserved for a platform that genuinely has no backend (none of the above).

**Offline (no backend).** `nob test` and app-clock-driven headless do not select a `null` backend —
they construct an *offline engine* (`mel_audio_create_offline`, §4.1) that owns no device, no device
thread, and no ring, and pump it synchronously with `mel_audio_render(eng, dst, frames)`. Hermetic,
deterministic, the produced PCM returned to the caller for assertion (MEL-ENGINE-VIII). Offline is an
engine mode, not a member of the backend set.

---

## 3. Threading & the ring (the load-bearing design)

The timeline splits into two threads of differing hardness:

**The device thread (hard real-time).** The backend's device callback does exactly one thing: copy
`block` frames of interleaved float out of the audio ring (`Mel_Audio_Ring`, §3.1) into the device
buffer, advancing the read cursor. No lock, no allocation, no source code runs here. On an empty ring
it copies silence and bumps a profiled **underrun** counter (MEL-CODE-006); in debug the first underrun
also asserts (MEL-ENGINE-VIII). This is the only code with hard-RT obligations, and it is a `memcpy` —
dignity on the weakest device falls out for free (MEL-ENGINE-VI).

**The mix thread (soft real-time).** A `thread`-spawned loop owned by `Mel_Audio`. Each iteration: (a)
drain the API→mix command queue; (b) while the ring has room for a block, mix one block of every live
voice into a **planar** float scratch (channel-major, for the resample/pan inner loops — MEL-CODE-005),
apply master gain, interleave into the ring; (c) park on a `thread/cond` / `thread/sem` until the
device thread signals headroom. The mix thread may briefly lock the voice table (§5); it carries no
hard-RT deadline because the ring decouples it from the device callback — that decoupling is the whole
reason this model was chosen over mixing inside the callback. Events the mix thread observes it
publishes via `future`/`event` (§4.6) — `mel_event_fire` is callable from the mix thread, and delivery
lands on a subscriber's executor, never re-entrantly here.

**Latency** ≈ `ring_depth_blocks × block / samplerate`, reported verbatim in `Mel_Audio_Caps`. The mix
thread keeps the ring at least one block ahead.

### 3.1 The audio ring — `Mel_Audio_Ring`

A **module-owned, lock-free, single-producer/single-consumer** float sample ring with `_Atomic` read
and write cursors (acquire/release ordering) and **bulk** `memcpy` read/write of whole blocks. It is
its own type, *not* `collection.ring`: that primitive is non-atomic and **overwrites the oldest entry
on overflow** (`ring.h:33`), which would tear and silently drop audio across the device↔mix boundary —
unfit here (MEL-ENGINE-VIII). A per-element queue is also wrong shape: audio wants contiguous-block
copies, not per-sample push. The producer is the mix thread, the consumer is the device thread; no
lock crosses between them.

---

## 4. Object model

### 4.1 Engine — `Mel_Audio` (U1)

Owns, in **online** mode, the linked platform backend, the resolved format, the mix thread, the planar
scratch buffers, the SPSC ring, the API→mix command queue, the master gain, the voice table (§5), and
the `Mel_Reactor` + `Mel_Executor` over which control-plane futures and events resolve (§4.6).
**Offline** mode (the test / headless path) owns the scratch, command queue, gain, and voice table only
— no backend, no ring, no threads. Created against an explicit allocator, reactor, and executor —
`clipboard`'s substrate shape (`mel_clip_init` + `Mel_Clip_Opt.exec`).

```c
typedef struct {
    u32 samplerate;      u32 channels;       u32 block_frames;
    u32 ring_blocks;     f32 master_volume;  Mel_Audio_Resampler resampler;
    Mel_Executor* exec;  /* control-plane future/event target — the Mel_Clip_Opt.exec shape */
} Mel_Audio_Opt;

typedef struct {
    u32 samplerate;  u32 channels;  u32 block_frames;
    u32 ring_blocks; u32 latency_frames;
} Mel_Audio_Caps;

Mel_Audio* mel_audio_create(const Mel_Alloc* a, Mel_Reactor* reactor, Mel_Audio_Opt opt);
Mel_Audio* mel_audio_create_offline(const Mel_Alloc* a, Mel_Reactor* reactor, Mel_Audio_Opt opt);
u32        mel_audio_render(Mel_Audio* eng, f32* interleaved_dst, u32 frames);
void       mel_audio_destroy(Mel_Audio* eng);
Mel_Audio_Caps mel_audio_caps(const Mel_Audio* eng);
void       mel_audio_set_master_volume(Mel_Audio* eng, f32 v);
u32        mel_audio_active_voice_count(const Mel_Audio* eng);
```

`create` opens the linked backend (loud on denial, with the reason — the backend identity is fixed by
the build, so this is selection-by-configuration, not a silent runtime default), allocates ring +
scratch, spawns the mix thread, starts the device. `create_offline` allocates scratch + voice table
only; `render` drains the command queue and mixes `frames` synchronously on the caller's thread,
returning frames produced — no device, no ring, no threads. `destroy` stops the backend (online), joins
the mix thread, drains, frees.

### 4.2 Source & instance — `Mel_Audio_Source` (U2)

SoLoud's two-level model, minimized: a **shared, immutable source** (sample data or stream config, plus
attributes) and a **per-voice instance** (the playhead / decoder state). The producer is one hot
callback at the instance level; everything else is cold lifecycle. Not enum-tagged — the open source
set (MEL-CODE-001, MEL-ENGINE-IX).

```c
typedef struct Mel_Audio_Source {
    u32   channels;
    f64   base_samplerate;
    bool  single_instance;
    usize instance_size;
    void  (*instance_init)(struct Mel_Audio_Source* src, void* inst, const Mel_Alloc* a);
    u32   (*get_audio)    (struct Mel_Audio_Source* src, void* inst, f32* planar_dst, u32 frames);
    void  (*seek)         (struct Mel_Audio_Source* src, void* inst, f64 seconds);
    void  (*instance_free)(struct Mel_Audio_Source* src, void* inst, const Mel_Alloc* a);
    void  (*source_free)  (struct Mel_Audio_Source* src, const Mel_Alloc* a);
} Mel_Audio_Source;
```

`get_audio` is the **only** hot, required slot — the sole indirect call on the mix path, once per voice
per block (~once per `block_frames`); the resample/gain/accumulate inner loop then runs fully inlined,
no pointer in sight. It writes up to `frames` at the source's own `base_samplerate` into the planar
scratch; the mixer resamples to the device rate per voice. `has_ended` is not a slot — a short return
without looping ends the voice. `instance_init`/`instance_free`/`seek`/`source_free` are cold and
optional (NULL for a stateless source). The engine allocates `instance_size` bytes per voice and runs
`instance_init` **on the API thread at `play`**, then hands the instance to the mix thread via the
create command (§5) — the mix thread never allocates (MEL-ENGINE-III). A decoder, stream, synth, or bus
is exactly this interface with a fuller `instance_size` and real lifecycle hooks; engine code does not
change (the §9 additivity test).

### 4.3 Raw-PCM source — `Mel_Audio_Pcm` (U3)

The one concrete source here, and stateless-shared: the **source** holds the float buffer (owned or
borrowed — explicit `Mel_Audio_Ownership`, mirroring `gpu/handle.h`), its channel count, native rate,
and optional loop point; the **instance** holds only an integer playhead. `instance_init`/
`instance_free` are NULL (zero-init suffices, no per-voice resources); `get_audio` reads the shared
buffer at the playhead, advancing and looping; `seek` sets the playhead; `source_free` releases the
buffer when `Owned`.

```c
Mel_Audio_Source* mel_audio_pcm_from_float(const Mel_Alloc* a, const f32* interleaved,
                                           u32 frames, u32 channels, u32 samplerate,
                                           Mel_Audio_Ownership own);
void mel_audio_pcm_set_loop(Mel_Audio_Source* s, bool loop, f64 loop_start_seconds);
```

### 4.4 Voice — `Mel_Audio_Voice` (U4)

`{ Mel_SlotMap_Handle slot; }` — the SoLoud voice handle, re-expressed as the repo's handle-over-slotmap
idiom (`gpu/handle.h:19`). The slotmap `generation` is the stale-handle guard: every mutator below
no-ops on a dead handle (debug-logs), never touching freed state (MEL-ENGINE-VIII).

```c
Mel_Audio_Voice mel_audio_play(Mel_Audio* eng, Mel_Audio_Source* src);
Mel_Audio_Voice mel_audio_play_ex(Mel_Audio* eng, Mel_Audio_Source* src,
                                  f32 volume, f32 pan, bool start_paused);

bool mel_audio_voice_valid(const Mel_Audio* eng, Mel_Audio_Voice v);
void mel_audio_set_volume     (Mel_Audio* eng, Mel_Audio_Voice v, f32 volume);
void mel_audio_set_pan        (Mel_Audio* eng, Mel_Audio_Voice v, f32 pan);
void mel_audio_set_play_speed (Mel_Audio* eng, Mel_Audio_Voice v, f64 ratio);
void mel_audio_set_paused     (Mel_Audio* eng, Mel_Audio_Voice v, bool paused);
void mel_audio_set_looping    (Mel_Audio* eng, Mel_Audio_Voice v, bool loop);
void mel_audio_seek           (Mel_Audio* eng, Mel_Audio_Voice v, f64 seconds);
void mel_audio_stop           (Mel_Audio* eng, Mel_Audio_Voice v);
void mel_audio_stop_all       (Mel_Audio* eng);
```

`play_ex` with `start_paused` is the set-params-then-unpause pattern, avoiding a one-block window at the
wrong volume.

Dense voice payload (mix-thread-owned, in the slotmap data array): the shared `source` pointer plus a
pointer to its engine-allocated per-voice **instance** (`instance_size` bytes), the resampler's
fractional `cursor`, target `volume`/`pan`, resolved per-channel gains, `play_speed`, resampler state,
and a `u32 flags` of named booleans (`paused`, `looping`, `protected`, `inaudible_keep_ticking`) —
never an enum (MEL-CODE-001). No `[N]` anywhere (MEL-CODE-002).

### 4.5 Fader — `Mel_Audio_Fader` (U5)

Smooths one scalar from `a` to `b` over `t` seconds against the stream clock; the timed forms of the
voice mutators and master volume. Oscillating and fade-to-pause/stop included.

```c
void mel_audio_fade_volume    (Mel_Audio* eng, Mel_Audio_Voice v, f32 to, f64 seconds);
void mel_audio_fade_pan       (Mel_Audio* eng, Mel_Audio_Voice v, f32 to, f64 seconds);
void mel_audio_fade_play_speed(Mel_Audio* eng, Mel_Audio_Voice v, f64 to, f64 seconds);
void mel_audio_oscillate_volume(Mel_Audio* eng, Mel_Audio_Voice v, f32 lo, f32 hi, f64 period);
void mel_audio_schedule_pause (Mel_Audio* eng, Mel_Audio_Voice v, f64 seconds);
void mel_audio_schedule_stop  (Mel_Audio* eng, Mel_Audio_Voice v, f64 seconds);
void mel_audio_fade_master_volume(Mel_Audio* eng, f32 to, f64 seconds);
```

Faders advance on the mix thread against the frame-counted stream clock — sample-accurate, immune to
API-thread jitter.

### 4.6 Events & completions — the async substrate (U7)

The control plane rides Melody's coordination trio — `future` (1→1), `event` (1→N), `channel` (M→N) —
over the executor waist, exactly as `clipboard` migrated ("ops → future, watch → event"); it is **not**
a bespoke callback. The §3 data plane touches none of it. Construction carries a `Mel_Reactor` (the
backend registers its device IO there) and a `Mel_Executor` (`Mel_Audio_Opt.exec`, e.g.
`mel_reactor_executor(reactor)`) on which completions resolve — the `Mel_Clip_Opt.exec` shape.

- **One-shot → `Mel_Future`** (write-once, zero-alloc resolve, single continuation):
  `mel_audio_voice_end_future(eng, v)` resolves once when voice `v` ends or is stopped; a device-started
  future covers async-open platforms (CoreAudio opens synchronously and pre-resolves it). Offline
  `render` is synchronous and needs none.
- **1→N broadcast → `Mel_Event`** (fire-from-any-thread, pushed to each subscriber's executor):
  `mel_audio_device_events(eng)` carries device-changed / hotplug / format-change, fired by the backend.
- **API→mix transport → `channel` `try` / the executor's intrusive MPSC** (§5): the mix thread drains
  it non-blocking at each block boundary, never parking — the hard-RT discipline.

`Mel_Audio_Status` is a `u32` severity + warning bitset, never an enum — the `Mel_Clip_Status` idiom
(MEL-CODE-001). The mix thread may call `mel_event_fire` directly (sanctioned cross-thread); delivery
lands on a subscriber's executor, never re-entrantly. The `todo.org` "reactor-driven device callback"
is thus honored as the control-plane *substrate*; the per-block pull stays the raw native callback +
ring (MEL-ENGINE-VI).

---

## 5. The concurrency hazard — voice handle reservation

`play` must return a usable handle synchronously, yet the voice table is mutated only by the mix
thread (so the mix loop iterates it lock-free). The reconciliation:

1. **Reserve on the API thread.** A slot index + rolled generation is claimed under a *brief*
   `thread/spinlock` guarding only the voice table's structural free-list — held for the reservation,
   never across mixing. The handle is formed and returned immediately.
2. **Activate on the mix thread.** A `create-voice` command carrying the reserved handle, the source,
   and the per-voice instance (allocated and `instance_init`-run on the API thread, §4.2) is pushed to
   the API→mix transport (a `channel` `try` surface, or the same intrusive `collection.mpsc` the executor
   waist uses) and drained at the top of the next mix iteration, which
   populates the dense payload. The instance is constructed API-side precisely so the mix thread never
   allocates (MEL-ENGINE-III).

Between reserve and activate the handle is valid but silent (it produces no audio yet) — the slotmap's
deferred-removal discipline (`slotmap.h:49`) models exactly this two-phase visibility. The device
thread never touches the voice table at all; it reads only the ring. Thus the **only** lock in the
system is brief, on the soft-RT mix thread and the API thread — never on the hard-RT device thread
(MEL-ENGINE-III). This is the single delicate point of the whole module; everything else is
straight-line.

All other mutators (`set_volume`, `seek`, `fade*`, `stop`) are likewise commands on that transport,
applied at block boundaries — so a mutation never tears a half-mixed block, and `play` is the only path
needing the reservation dance.

---

## 6. Backend contract — compile/link-time (U6)

The backend is **not a runtime object**: no handle, no vtable, no `create`-time selection. The audio
backend set is closed and known at build time — exactly one native backend is compiled per platform
(CoreAudio on macOS), put in by `build.c` source gating, precisely as the gpu module commits to its
backend at compile time instead of dispatching through pointers. Runtime polymorphism over a
build-time-closed axis is machinery nobody asked for (MEL-CODE-001, MEL-ENGINE-III).

The seam is a **fixed-name ABI the engine calls directly**; the platform's gated translation unit
supplies the one definition:

```c
bool mel_audio_backend_open (Mel_Audio_Opt req, Mel_Audio_Caps* granted, const Mel_Alloc* a);
void mel_audio_backend_start(Mel_Audio_Ring* ring);   /* device thread copies blocks out of the ring */
void mel_audio_backend_stop (void);
void mel_audio_backend_close(const Mel_Alloc* a);
```

`open` negotiates the real device format and fills `granted` (loud, with cause, on failure — P3). Given
§3 the backend is a **ring consumer**, never a mixer driver — it never sees a voice; its whole job is
the device thread's `memcpy` out of the ring. An out-of-tree backend (a JACK or alternate PipeWire
variant) supplies these same four symbols and is gated into the build in place of the default —
extensibility is link-time, the architecture bends without a runtime vtable (MEL-ENGINE-IV). The
offline engine (§4.1) calls none of this.

---

## 7. Failure modes (iterated per the design workflow)

- **Device open denied / format ungranted** — `mel_audio_create` returns NULL after logging the device
  error and the requested-vs-available format (P3, MEL-ENGINE-VIII).
- **Ring underrun** — device thread emits silence, increments a profiled counter; debug asserts on
  first occurrence. Never garbage, never a stale block (MEL-ENGINE-VIII).
- **Stale voice handle** — every mutator no-ops via the slotmap generation check; debug-logs the dead
  handle.
- **Source rate ≠ device rate** — per-voice resample (the core path, not an error).
- **Voice budget reached** — audibility-ordered culling over the growable slotmap; `protected` voices
  survive; the cull is a policy, not a fixed `[N]` ceiling (MEL-CODE-002, MEL-ENGINE-I). What is dropped
  is logged, never silently truncated.
- **`play` during `destroy`** — rejected, returns the null handle.
- **Source / instance lifetime** — a source is shared and outlives its voices; the caller owns the
  `Mel_Audio_Source` and frees it (`source_free`) after its voices end. Each voice owns its instance and
  frees it (`instance_free`) when it ends or is stopped. The PCM `Mel_Audio_Ownership` governs only the
  sample buffer — `Owned` ⇒ `source_free` releases it, `Borrowed` ⇒ the caller's. Freeing a source while
  its voices are live, or destroying the engine while voices hold instances, is a contract violation
  asserted in debug (MEL-ENGINE-VIII).
- **Mono source → stereo device** (and inverse) — channel up/down-mix in the pan stage, explicit, no
  silent default count.

---

## 8. Module layout & build

```
modules/audio/
  readme.md  build.c
  include/audio/
    audio.h     engine.h   source.h   pcm.h
    voice.h     fader.h     backend.h  ownership.h
    event.h     status.h
  src/
    engine.c    mixer.c     resample.c  pan.c       offline.c
    fader.c     voice.c     command.c   pcm.c       ring_io.c
    coreaudio/coreaudio.m   wasapi/wasapi.c   alsa/alsa.c
    aaudio/aaudio.c         web/web.c
  test/
    test_voice_handle.c  test_render_offline.c  test_fader.c  test_ring_spsc.c
```

`build.c` (`mel_add_library "audio"`): public `include`; engine / mixer / offline `src/*.c` ALWAYS; one
backend per platform, each gated and supplying the §6 ABI: `src/coreaudio/*.m`
(`-framework AudioToolbox -framework CoreAudio -framework AudioUnit`) on `MEL_ON(MACOS) | MEL_ON(IOS)`;
`src/wasapi/*.c` (`-lole32 -lksuser`) on `MEL_ON(WIN32)`; `src/alsa/*.c` (`-lasound`) on `MEL_ON(LINUX)`;
`src/aaudio/*.c` (`-laaudio`) on `MEL_ON(ANDROID)`; `src/web/*.c` (emscripten Web Audio) on `MEL_ON(WASM)`.
Depends: `core allocator collection math thread time reactor executor future event channel log debug
string`. Tests are `mel_add_test` targets that construct an **offline** engine and assert on `render`
output — hermetic, no device, no thread (the platform's backend TU is linked but never entered).

Backend selection is **platform-gated source selection**, not a CLI axis. If a second backend per
platform later earns its keep (e.g. CoreAudio vs a JACK backend on macOS), promote to an `audio` axis
in `resolve.c` mirroring `--gpu`; until then the axis would be cost without benefit.

---

## 9. Out of scope (separate, purely-additive specs)

Filters (DSP vtable + per-voice/engine chain), sub-mixer buses, 3D spatialization (listener +
function-pointer attenuator + doppler), FFT/wave visualization, file decoders (wav/ogg/mp3/flac as
third-party amalgamations) and streaming sources, voice groups, and a MIDI-driven synth source
composing `modules/midi`. Each extends the §4 source/voice surface without reshaping it
(MEL-ENGINE-IX) — that additivity is the test for whether this core is shaped right.
