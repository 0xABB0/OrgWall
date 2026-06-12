# 2026-06-11 — audio stack implementation (pcm, spectrum, audioin core)

## Work done

First steps of the audio-stack implementation order (pcm → spectrum →
audioin → …), each against its frozen wireframe trio, on
`worktree-audio-v2-wireframes`.

### pcm

- `src/ring.c` — wait-free SPSC frame ring. Follows `audio`'s `ring_io.c`
  shape (memcpy with wraparound split, allocator captured at create) but
  counts in frames and uses free-running `u64` head/tail. The private rings
  it supersedes use `u32` counters with `% capacity`, which silently corrupts
  on non-power-of-two capacities once the counter wraps (~25 h of mono audio
  at 48 kHz); `u64` makes wrap unreachable for any realistic stream. No
  zero-fill on short reads — `read` returns frames delivered, per spec.
- `src/resample.c` — `mel_pcm_resample_linear`, semantics identical to the
  private `mel_audio_resample_linear` (clamp-to-last past the end, silence
  for empty source, caller-rebased cursor, as the mixer expects), plus an
  `assert(ratio > 0.0)` the old kernel lacked.
- `src/convert.c` — interleave/deinterleave, i16↔f32 with symmetric 32768
  scaling and clamp, so i16→f32→i16 is exact identity over the full range
  (test sweeps all 65536 values).
- `build.c` — library (`core` + `allocator` only, per spec) and three test
  targets: `pcm-ring` (7 tests incl. threaded SPSC stress of 200k frames and
  a non-power-of-two-capacity churn), `pcm-resample` (7), `pcm-convert` (6).
  All 20 pass on macos-debug.

### spectrum

- `src/spectrum.c` — real-input FFT as a half-size complex radix-2
  Cooley-Tukey plus real-unpack: `create` precomputes bit-reversal, FFT
  twiddles, and unpack twiddles into one slab from the caller's allocator;
  `analyze`/`analyze_complex` allocate nothing (MEL-ENGINE-VI). Magnitudes
  are unnormalized DFT values (full-scale sinusoid at bin k reads `window/2`),
  recorded in the readme. Windows (`hann`, `hamming`, `blackman`) use the
  symmetric textbook definitions; `bin_hz` is the pure `bin·rate/window`
  mapping.
- `build.c` — library (`core` + `allocator`), one test target
  `spectrum-test` (11 tests: DC/sinusoid/Nyquist placement, phase sign,
  smallest window, window shapes, bin↔Hz, and a full cross-check of
  `analyze`/`analyze_complex` against a naive O(N²) DFT in f64). All pass on
  macos-debug.

### audioin (core + publish; host providers pending)

- `src/audioin.c` — registry core on camera's exact structure (slotmap of
  device slots, provider array, reconciliation keyed by
  `(provider, stable_id)`, hotplug over an `event` channel, auth future
  jobs), adapted for audioin's contract: `str8` stable ids (slot-owned
  copies, since provider strings are only interned until the next
  enumerate), `changed` events fired only on actual field changes, default
  tracking with `default_changed`, module-level authorization as the most
  restrictive across providers that have devices, multi-prompter authorize
  with an atomic countdown, caps-gated gain. Internal bridge
  `mel_audioin__open`/`__close` (in `audioin_internal.h`, not the frozen
  public headers) forwards to the owning provider — this is the hook
  `audiocapture` will consume; whether it should be promoted into
  `provider.h` is a question for that step.
- `src/publish.c` — published inputs as a built-in virtual provider:
  `Pub_Slot` heap-allocated and held by pointer (the feeder thread reads the
  slot while the control thread may grow the slotmap; by-value slots would
  move under it), pcm SPSC ring between `feed` and sink fan-out (wait-free;
  backlog buffers until a consumer opens, then drains on the next feed),
  sink lists swapped atomically as immutable snapshots with retired lists
  parked on a garbage list until unpublish (feeder may still be reading
  them). `os_visible` is false and publish returns
  `WARNED | WARN_LOCAL_ONLY` everywhere — no platform has an OS-publish
  backend yet, reported honestly.
- `src/host_none.c` — host provider registration is a loud-log no-op on all
  platforms for now; per-platform backends (CoreAudio, WASAPI, PipeWire,
  AAudio, getUserMedia) are the next chunk and will replace this file under
  axis gating.
- `build.c` — lib + `audioin-core` test target compiling the core sources
  directly with a silent host stub (camera's test pattern). 14 tests cover
  the spec's test contract: reconciliation/handle stability, find
  round-trip, default tracking, hotplug payloads, silent no-change refresh,
  consent grant/deny/deferred, most-restrictive combine, gain caps gating,
  multi-provider listing, publish lifecycle (registry appearance, feed →
  mock consumer with backlog drain, `WARN_LOCAL_ONLY`, on_lost, removal),
  overflow rejection, token multiplexing. All pass on macos-debug.

### All

- `readme.md` added to each; pcm and spectrum wireframe `usage.c` gained the
  missing `<allocator/heap.h>` include (both called `mel_alloc_heap` without
  it); all three usage files now compile-check against the real headers.

## Kludges

- pcm: the aliasing asserts in `convert.c` only catch exact pointer equality
  between a plane and the interleaved buffer, not partial overlap. The spec
  allows this ("aliasing where detectable"); full overlap detection would
  need buffer extents the API does not take.
- pcm: `audio` and `audiocapture` still carry their private rings/resampler;
  migration is the planned "audio delta" step, not done here. Until then the
  duplicate logic stands.
- pcm: ring head/tail share a cache line (no padding), matching the repo's
  existing rings; a false-sharing pass awaits profiling (MEL-CODE-006).
- spectrum: magnitude normalization and the symmetric (vs periodic) window
  flavor are conventions the spec left open; both choices are documented in
  the readme rather than parameterized. If a consumer needs periodic windows
  for overlap-add, that is a new function per the open-set rule, not a flag.
- spectrum: the FFT butterfly is scalar; no SIMD. Correct and O(n log n),
  but a NEON/SSE pass is plausible debt if visualizer profiles demand it.
- audioin: no host platform providers yet — the spec's platform story
  (CoreAudio + process taps, WASAPI + loopback endpoints, PipeWire, AAudio,
  enumerateDevices) is entirely unimplemented; `host_none.c` logs loudly.
  This is the sequenced next step, not a quiet omission.
- audioin: the spec's dependency list omitted `event`; the hotplug channel
  uses it (camera's exact pattern). spec.md amended — flagging the deviation
  from the frozen wireframe here.
- audioin: `mel_audioin__open`/`__close` live in `audioin_internal.h` and
  the test includes it via a relative path; if `audiocapture` consumes them
  cross-module, they likely belong in `provider.h` — deferred to that step
  with Gabbo's call.
- audioin: publish's sink-list garbage is only reclaimed at unpublish; an
  open/close-churning consumer accumulates retired snapshot arrays until
  then. Bounded by churn count, but real.
- audioin: `unpublish`/`shutdown` while a producer thread is mid-`feed` is
  unguarded (sink lists are safe; the ring teardown is not). The concurrency
  model says the producer stops first; nothing enforces it.

## CLAUDE.md suggestions (recommendations only)

- None new; the wireframe-skill notes from the wireframe session's writeup
  still stand.

## Suggestions

- Next per the implementation order: `audioin`, then `audioout`,
  `audiopolicy`, `audiocapture`, the `audio` delta, `tts`, `stt`.
- When the audio delta lands, delete `audio/src/ring_io.c`,
  `audio/src/resample.c` and `audiocapture/src/ring.c` in the same change —
  the `u32`-counter wrap bug documented above lives in all three.
- The wireframe skill could compile-check each `usage.c` at emit time; both
  trios shipped with a missing include that a syntax-only pass would have
  caught.
