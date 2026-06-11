# pcm

Realtime PCM plumbing shared by every producer and consumer of audio frames:
a wait-free SPSC frame ring, the resampler contract plus a linear kernel, and
interleave/format conversions. Pure computation and one opaque ring; no
platform code, no threads spawned, no OS calls.

Supersedes the private ring copies in `audio` and `audiocapture` and `audio`'s
private resampler. See `spec.md` for the full contract.

Dependencies: `core`, `allocator`.
