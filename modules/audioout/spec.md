# audioout — specification

Audio output device identity: enumerate outputs (speakers, headphones, HDMI,
virtual sinks) across providers, describe them, name the system default, own
OS volume/mute, publish app-created outputs — local always, OS-wide where the
platform allows. No audio is mixed here: `audio` binds its engine to a
`Mel_AudioOut`; a settings panel links this module alone. Twin of `audioin`
with two deliberate differences: no consent surface (nothing gates output;
ceremony that always answers granted would lie — MEL-ENGINE-V), and a pull
stream plane (output devices ask for frames on their clock).

## Headers

- `<audioout/audioout.h>` — identity: handles, kinds, descriptors, registry,
  default, volume/mute.
- `<audioout/events.h>` — hotplug events and subscriptions.
- `<audioout/os.h>` — OS integration: publish plane, native handles.
- `<audioout/provider.h>` — the provider plugin contract.

## Identity

Identical contract to `audioin`: generational `Mel_AudioOut` handle for
session liveness; provider-scoped `stable_id : str8` in the descriptor for
persistence; `mel_audioout_find(stable_id)` resolves at startup
(`MEL_AUDIOOUT_NULL` when gone — explicit fallback, never silent).

`mel_audioout_kind` open descriptors: builtin / hdmi / usb / bluetooth /
virtual / unknown. Descriptor: `name`, `stable_id`, `kind`, `channels`,
`samplerate`, `samplerates` (dynamic, MEL-CODE-002), `caps { volume }`,
`alloc`; caller-allocator describe with one destructor (MEL-CODE-003).

`MEL_AUDIOOUT_NULL` doubles as `audio`'s named follow-system-default binding;
this module assigns it no meaning beyond "no specific device".

## Registry, hotplug

As `audioin`: `init(alloc, deliver)`, `refresh` reconciliation keyed by
`(provider, stable_id)` with handle stability, `default_()`, hotplug events
(`added` / `removed` / `changed` / `default_changed`) marshaled to the
subscriber's executor, `mel_audioout_provider_notify` from providers.
`default_changed` is load-bearing: it is how `audio`'s follow-default mode
learns to migrate on platforms whose OS doesn't move streams itself.

## Volume / mute

Caps-gated OS endpoint volume — the settings-panel/kiosk knob, distinct from
the engine's `master_volume` (which scales the mix, not the device):
`volume(dev)` / `set_volume(dev, v)` in `[0, 1]`, `muted(dev)` /
`set_muted(dev, b)`. `!caps.volume` fails `ERROR | UNSUPPORTED`. External
volume changes surface as hotplug `changed` events: the provider's raw
descriptor carries `volume`/`muted` shadows solely so reconciliation can
detect them; the live getters always ask the provider.

## Provider plugin

Host OS is provider 0; virtual sinks register the same way (MEL-ENGINE-IX).

```
Mel_AudioOut_Provider_Desc {
    name, user,
    enumerate(fn, fn_user)           // calls fn per device; provider-interned str8s, valid
                                     // until next enumerate/shutdown; fn false = stop
    default_id() -> str8
    open(stable_id, req format, granted format*, pull, token)   // negotiate, hold
    start(stable_id, token)                                     // provider begins pulling
    stop(stable_id, token)
    close(stable_id, token)
    volume / set_volume / muted / set_muted (stable_id)
    native(stable_id)
    shutdown(alloc)
}
Mel_AudioOut_Pull_Fn: (token, interleaved_dst, frames) -> frames written
```

The stream plane is pull on the provider's clock: the host provider pulls
from its device callback; a file-writer provider pulls as fast as it likes; a
network sink pulls on its own timer. The core side being pulled (the `audio`
engine's ring) must satisfy the pull wait-free; a short fill is the puller's
to pad — underruns are the engine's `WARN_RING_UNDERRUN`, never a provider
guess. `open` negotiates: the provider answers the granted format honestly
(`granted` may differ from `req`; the caller resamples/remixes — `audio`
already does). Multiple opens per device are token-distinguished.

## Publish

```
Mel_AudioOut_Publish_Result r = mel_audioout_publish(alloc, opt);  // name, channels, samplerate, ring
u32 got = mel_audioout_publish_read(r.published, dst, frames);     // what others played into it
bool os = mel_audioout_publish_os_visible(r.published);
mel_audioout_unpublish(r.published);
```

A published output is a device *others play into*; the publishing app reads
what arrives (wait-free) — the receiving half of game streaming, recording
another app, an in-app virtual cable. Local registry always; OS-wide where
possible, `WARNED | WARN_LOCAL_ONLY` where not (honest, MEL-ENGINE-VIII).

## Concurrency

As `audioin`: caller-driven public API, provider threads on the stream plane,
executor-marshaled hotplug, wait-free publish reads.

## Failure

Stale handle: assert + `ERROR | LOST`. Caps violations `ERROR | UNSUPPORTED`.
Zero devices honest with loud log. `init` twice / `shutdown` without: asserts.

## Platform story (host provider + publish reach)

- macOS — CoreAudio HAL output-scope devices + listeners; endpoint volume via
  HAL properties. Publish: OS-visible via the installable HAL plug-in
  component (shared with `audioin`); local-only without it.
- iOS — AVAudioSession current route outputs (the honest, short list);
  hardware volume is read-only there: `caps.volume = false`. Publish:
  local-only.
- win32 — IMMDeviceEnumerator (eRender) + IMMNotificationClient;
  IAudioEndpointVolume. Publish: local-only until a signed-driver story.
- linux — PipeWire (Pulse fallback) sinks; node volume. Publish: OS-visible
  natively (virtual sink nodes).
- android — AudioManager output devices via `platform` JNI; stream volume.
  Publish: local-only.
- wasm — enumerateDevices(audiooutput) where `setSinkId` exists;
  honest-absent otherwise; no volume caps. Publish: local-only.

## Dependencies

- `core` — types, asserts.
- `allocator` — descriptors, registry, publish ring via caller allocators.
- `string` — names and stable ids.
- `collection` — slotmap registries, dynamic arrays.
- `executor` — hotplug delivery.
- `pcm` — the publish ring.
- `log` — diagnostics.
- `platform` (android only) — JNI bridge.

No `future`: nothing here is async.

## Test contract

Mock provider proves: reconciliation/handle stability, find round-trip,
default tracking + `default_changed`, volume/mute caps gating and external
change events, open format negotiation honesty, pull-plane token
multiplexing, publish read round-trip + `WARN_LOCAL_ONLY`. No hardware.
