# audiocapture

Microphone input as a pull stream. The OS capture thread writes f32 mono frames into a
lock-free SPSC ring; the app reads with `mel_audiocapture_read` at its own cadence.
Sibling of `audio` (which is playback-only) — deliberately tiny so consumers like a
tuner depend on capture alone, not the mixer engine.

- Enumeration returns device ids; names come per-id as `str8` from a caller allocator.
- `mel_audiocapture_open(alloc, device_id, opt)` — explicit sample rate and ring
  capacity, mono f32. Returns NULL on failure (device gone, permission denied at the
  OS level).
- Permission: `mel_audiocapture_authorized()` / `mel_audiocapture_auth_determined()`;
  opening an input triggers the system prompt when undetermined.

Platforms: macOS (CoreAudio HAL enumeration + AudioQueue input). Other platforms are
owed — see todo.md.

Dependencies: core, allocator, string.
