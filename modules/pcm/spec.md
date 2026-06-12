# pcm — specification

Realtime PCM plumbing shared by every producer and consumer of audio frames: a
wait-free SPSC frame ring, the resampler contract plus a linear implementation,
and interleave/format conversions. Pure computation and one opaque ring; no
platform code, no threads spawned, no OS calls. Supersedes the private ring
copies in `audiomixer` and `audiocapture` and `audiomixer`'s private resampler.

## Headers

- `<pcm/ring.h>` — SPSC frame ring.
- `<pcm/resample.h>` — resampler contract + linear kernel.
- `<pcm/convert.h>` — interleave/deinterleave, i16↔f32.

## Ring

Opaque, heap-allocated from the caller's `Mel_Alloc` (MEL-CODE-003); destroy
frees through the allocator captured at create.

- Frame-granular: every count is frames; one frame is `channels` interleaved f32
  samples. A frame is never split — partial progress is whole frames only, so a
  multi-channel reader can never desynchronize channels.
- Wait-free SPSC: exactly one producer thread, one consumer thread;
  acquire/release atomics, no locks, no syscalls — legal on OS realtime threads
  (MEL-ENGINE-VI).
- `write` returns frames accepted (`0..frames`); a full ring rejects loudly via
  the return value, never drops silently (MEL-ENGINE-VIII). `read` returns
  frames delivered.
- `read_available`/`write_available` are exact from the calling side of the
  stream and a lower bound from the other; `channels`/`capacity` are constants.
- `create` with `channels == 0` or `capacity_frames == 0` is a contract
  violation: debug assert, no defaulted size (MEL-CODE-007).

## Resampler

```
typedef u32 (*Mel_Pcm_Resampler)(const f32* src, u32 src_frames,
                                 f32* dst, u32 dst_frames,
                                 f64 ratio, f64* cursor);
```

Single-channel f32; `ratio` is `src_rate / dst_rate`; `cursor` carries the
fractional read position across calls (caller-owned, zero-initialized). Returns
frames produced. Multi-channel callers run one cursor per channel over planar
data. `mel_pcm_resample_linear` is the provided kernel; higher-quality kernels
plug in by pointer — the contract is open, never a closed set (MEL-ENGINE-IV).

## Convert

Planar↔interleaved f32 and i16↔f32 sample conversion. Pure loops; `dst` and
`src` must not alias; counts of zero are no-ops; NULL pointers assert in debug.

## Concurrency

The ring is the only concurrent object and only in its SPSC roles; everything
else is thread-free pure code. The ring embeds no wake primitive — a consumer
that sleeps layers its own semaphore beside the ring (as `audiomixer`'s mix thread
does), keeping `thread` out of this module's dependencies (MEL-ENGINE-III).

## Failure

Debug asserts on every contract violation (NULL, zero sizes, aliasing where
detectable); no error codes — misuse is a bug, not a runtime condition
(MEL-ENGINE-VIII).

## Dependencies

- `core` — types, asserts.
- `allocator` — ring storage flows through the caller's `Mel_Alloc`.

## Consumers

`audiomixer` (device ring, mixer resampling), `audiocapture` (capture ring, rate
conversion). This module knows neither.
