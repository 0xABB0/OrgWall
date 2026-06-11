# audio — DAW-enablement campaign

What Melody still owes before "a fully featured DAW on Melody" is true. The
device plane (audioin/audioout/audiopolicy/audiocapture/audioplayback/pcm/
spectrum + the audio mixer deltas) is wireframed and is the foundation; the
tiers below build strictly on it. Each tier item is its own module and its
own wireframe interview — nothing here is designed, only named and bounded
(MEL-ENGINE-I: the domain is acknowledged in full).

## Tier 2 — engine capabilities

- **`audiograph`** — the processing graph a console is: nodes (sources,
  inserts, sends/returns, buses, master), arbitrary routing, per-edge gain,
  plugin-delay compensation, block-accurate parameter automation. Realtime
  thread executes a compiled schedule; mutation is command-queued (the
  `audio` engine's transport pattern generalized). Consumes
  `audioplayback` (pull mode) for the device, `pcm` throughout. The flat
  mixer (`audio`) stays — games don't pay for a console (MEL-ENGINE-III);
  a DAW skips `audio` entirely.
- **`transport`** — musical timeline: sample-accurate event scheduling,
  tempo/meter map, position/loop/locate, punch ranges, clock export. Pure
  data + scheduling contract consumed by `audiograph` and `midi`; owns no
  audio.
- **Duplex sync** (lands inside `audiocapture`/`audioplayback`, possibly a
  tiny `audiosync` helper) — shared-clock alignment of an input and an
  output stream: drift estimation over the two timestamp streams,
  round-trip-latency measurement (loopback ping), the offset a DAW applies
  to recorded material. The timestamps and latency queries already
  wireframed are the primitives.

## Tier 3 — domains

- **`pluginhost`** — VST3/AU/CLAP hosting: discovery/scan (out-of-process
  for crash isolation), parameter model, state save/restore, editor-window
  embedding (via `window`), realtime processing adapter exposing each plugin
  as an `audiograph` node. The largest single item in the campaign.
- **`codec`** — decode (wav/flac/ogg/mp3/aac) feeding pull sources /
  `audiograph` clips; encode eating taps/graph renders for bounce and
  recording. Container + codec split per the camera/image precedent
  (pixels:image :: samples:pcm, codecs own the wire formats).
- **`diskstream`** — realtime-safe streaming sources over `io`: read-ahead,
  ring-buffered multitrack playback and record-to-disk, cache budgets
  honest and visible (MEL-ENGINE-III).
- **`musicsync`** — inter-app/inter-device sync protocols: MIDI clock/MTC
  (over `midi`), Ableton Link (over `net`). Consumes `transport`'s clock,
  never owns one.
- **ASIO/JACK providers** — pro-audio backends as plain `audioout`/`audioin`
  providers (win32 ASIO needs the vendor SDK question answered; JACK on
  linux beside PipeWire). No consumer changes by construction.

## Already answered by the wireframed plane

Device choice/pinning/hotplug/stable-ids, exclusive mode, input processing
toggles, capture timestamps, output latency query, loopback capture,
virtual devices and OS-publish, offline render (`mel_audio_create_offline`),
mix/voice taps, FFT (`spectrum`), MIDI/musictheory/musictuning/musicnotation
modules.

## Sequencing

Tier 2 before tier 3 (`audiograph` is the spine: plugins, codecs, and
streaming all express themselves as its nodes). `transport` can be
wireframed in parallel with `audiograph`; duplex sync needs both stream
modules implemented first to validate against hardware.

Per MEL-SPEC-002 this file dissolves into the respective modules as each
wireframe lands.
