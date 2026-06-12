# audioin — specification

Audio input device identity: enumerate inputs (microphones, line-in, loopback,
virtual feeds) across providers, describe them, name the system default, own
input consent and input gain, publish app-created inputs — to the local
registry always, to the whole OS where the platform allows. No audio is read
here: `audiocapture` turns a `Mel_AudioIn` into a stream; a settings panel
links this module alone. Twin of `audioout` (which adds volume/mute and drops
consent).

## Headers

- `<audioin/audioin.h>` — identity: handles, kinds, descriptors, registry,
  default, gain.
- `<audioin/events.h>` — hotplug events and subscriptions.
- `<audioin/permission.h>` — consent: auth states, the authorize future.
- `<audioin/os.h>` — OS integration: publish plane, native handles.
- `<audioin/provider.h>` — the provider plugin contract.

## Identity

```
typedef struct { Mel_SlotMap_Handle h; } Mel_AudioIn;     // session identity
#define MEL_AUDIOIN_NULL ((Mel_AudioIn){ 0 })

descriptor.stable_id : str8                               // persistent identity
Mel_AudioIn mel_audioin_find(str8 stable_id);             // resolve at startup
```

Two identities, two jobs: the generational handle is session-local liveness —
unplug the device and the handle dies, every use fails loud (MEL-ENGINE-VIII);
the provider-scoped `stable_id` is serializable — a settings file remembers
the chosen device, `find` resolves it next launch (`MEL_AUDIOIN_NULL` when
gone; the app falls back explicitly, never silently).

`mel_audioin_kind` is an open descriptor (const singletons): builtin / usb /
bluetooth / virtual / loopback / unknown — classifications grow
(MEL-CODE-001). `loopback` is system-audio capture surfaced honestly as an
input device (WASAPI loopback endpoints, macOS process taps, PipeWire
monitors) — a visualizer of other apps' audio opens it like any microphone.

Descriptor: `name`, `stable_id`, `kind`, `channels`, `samplerate`,
`samplerates` (dynamic array, MEL-CODE-002), `caps { gain }`, `alloc`.
`describe` allocates from the caller's allocator; `describe_free` is the one
destructor (MEL-CODE-003).

## Registry, hotplug

`init(alloc, deliver)` builds the registry over all registered providers;
`refresh` re-enumerates and reconciles keyed by `(provider, stable_id)` —
surviving devices keep their handles, vanished ones die. `default_()` is the
host provider's default input. Hotplug events carry `added` / `removed` /
`changed` / `default_changed` booleans plus the handle; callbacks marshal to
the subscriber's executor. Providers signal set-changes via
`mel_audioin_provider_notify`; the core re-enumerates and diffs.

## Consent

Camera's contract exactly: `authorization()` snapshot;
`authorize(alloc)` → `Mel_Future*` → `const mel_audioin_auth*` singleton
(granted / denied / not_determined / restricted); `future_auth` reads,
`future_free` destroys. Only `authorize` ever triggers the OS prompt
(MEL-CODE-007); `audiocapture` and `stt` gate on the answer. Consent is the
host provider's; virtual providers answer granted. The module-level answer is
the most restrictive across providers that have devices.

## Gain

Caps-gated OS input gain: `gain(dev)` / `set_gain(dev, v)` with `v` in
`[0, 1]`. `!caps.gain` fails `ERROR | UNSUPPORTED` — never a silent no-op.
Level metering is not here: it falls out of reading the capture stream.

## Provider plugin

The host OS is provider 0; anyone registers more (a test-fixture input, a
network voice feed) and their devices list identically (MEL-ENGINE-IX).

```
Mel_AudioIn_Provider_Desc {
    name, user,
    enumerate(fn, fn_user)           // calls fn per device; provider-interned str8s, valid
                                     // until next enumerate/shutdown; fn false = stop
    default_id() -> str8
    open(stable_id, sink, opt, granted*)   // begin pushing; negotiate processing/exclusive
    close(stable_id, token)
    gain / set_gain(stable_id)
    authorization / authorize(sink)
    native(stable_id)
    shutdown(alloc)
}
Mel_AudioIn_Sink { on_frames(token, interleaved, frames, samplerate, channels, timestamp_ns),
                   on_lost(token), on_auth(token, auth), token }
Mel_AudioIn_Open_Opt { processing { echo_cancellation, noise_suppression, auto_gain }, exclusive }
Mel_AudioIn_Granted  { processing, exclusive, os_timestamps }
```

`open` negotiates: the provider answers what is actually in effect in
`granted` — never the request echoed back. `timestamp_ns` is the OS-monotonic
stamp of the batch's first frame, 0 when unknown; `granted.os_timestamps`
says whether the provider stamps. The first open's options configure a shared
device stream; later opens receive the actuals. The consumer bridge
(`mel_audioin__open`/`__close` in `<audioin/provider.h>`) is how
`audiocapture` reaches this plane by handle.

The stream plane is push: the provider delivers native-format frames from its
own thread/clock into the sink; the consumer core (`audiocapture`) owns the
one ring and the one conversion (`pcm`) for every provider. Multiple opens of
one device are distinguished by token; the provider multiplexes. Each
terminal callback fires at most once; any thread.

## Publish

```
Mel_AudioIn_Publish_Result r = mel_audioin_publish(alloc, opt);  // name, channels, samplerate
u32 fed = mel_audioin_publish_feed(r.published, interleaved, frames);
bool os  = mel_audioin_publish_os_visible(r.published);
mel_audioin_unpublish(r.published);
```

Publishing creates a device other consumers record from, fed by the app. It
always appears in the local registry (it is a virtual-provider device,
`r.device` is its handle); where the OS allows, it also becomes system-wide —
other applications see and record it. OS reach is reported honestly:
`os_visible` false + `WARNED | WARN_LOCAL_ONLY` on platforms that refuse
(never pretended). `feed` is wait-free.

## Concurrency

Public API caller-driven single-threaded (camera's model). Provider sinks
fire on provider threads; hotplug marshals to the deliver executor;
`publish_feed` is wait-free and callable from any one producer thread.

## Failure

Stale handle: assert + `ERROR | LOST`. Unknown `find`: `MEL_AUDIOIN_NULL`.
Zero devices enumerate as zero with a loud log — never a fabricated default.
Caps violations: `ERROR | UNSUPPORTED`. A device held exclusively elsewhere:
`ERROR | BUSY`. `init` twice / `shutdown` without: asserts.

## Platform story (host provider + publish reach)

- macOS — CoreAudio HAL input-scope devices, HAL property listeners,
  `kAudioHardwarePropertyDefaultInputDevice`; process-tap loopback devices;
  consent via AVCaptureDevice. Publish: OS-visible via an installable
  HAL plug-in component bridged over shared memory; local-only without it.
- iOS — AVAudioSession available inputs + route changes; record-permission
  consent. Publish: local-only (OS forbids).
- win32 — IMMDeviceEnumerator (eCapture) + IMMNotificationClient; loopback
  via eRender endpoints in loopback mode (surfaced as `loopback` inputs);
  consent always granted. Publish: local-only until a signed-driver story.
- linux — PipeWire (Pulse fallback) nodes; monitors as `loopback`; consent
  granted. Publish: OS-visible natively (virtual source nodes).
- android — AudioManager via `platform` JNI; RECORD_AUDIO consent. Publish:
  local-only.
- wasm — enumerateDevices + devicechange; blank labels pre-consent (honest);
  authorize via a getUserMedia probe. Publish: local-only.

## Dependencies

- `core` — types, asserts.
- `allocator` — descriptors, registry, publish ring via caller allocators.
- `string` — names and stable ids as `str8`.
- `collection` — slotmap registries, dynamic arrays.
- `future` — the consent future.
- `executor` — hotplug delivery.
- `event` — the hotplug channel that marshals to subscriber executors.
- `pcm` — the publish feed ring.
- `thread` — provider-owned capture threads (win32/linux) and teardown
  helpers (android).
- `log` — enumeration/consent/publish diagnostics.
- `platform` (android only) — JNI bridge.

## Test contract

Mock provider proves: reconciliation and handle stability, stable-id find
round-trip, default tracking, hotplug payloads, consent future grant/deny,
gain caps gating, publish appearing in the registry + feed/read through a
mock consumer + honest `WARN_LOCAL_ONLY`, provider-notify re-enumeration,
multi-provider unified listing. No hardware.
