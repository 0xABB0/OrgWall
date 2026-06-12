# 2026-06-12 — audioin form rework + audioin/audioout/audiopolicy platforms

## Work done

Per Gabbo's review of the audioin core:

- **Header split.** `audioin.h` now carries identity only; events moved to
  `<audioin/events.h>`, consent to `<audioin/permission.h>`, publish +
  native to `<audioin/os.h>`; `provider.h` includes permission (sinks carry
  auth callbacks). spec.md gained a Headers section.
- **Callback enumeration.** `enumerate(out, cap)` replaced by
  `enumerate(fn, fn_user)` — the provider calls `fn` per device, `false`
  stops. Core refresh reconciles per-callback (`Refresh_Ctx`); publish
  provider and tests reworked. No more externally sized buffers.
- **Notify marshals.** `mel_audioin_provider_notify` now posts the refresh
  onto the init `deliver` executor via `mel_executor_call` — OS listener
  threads (CoreAudio property listeners, IMMNotificationClient, route
  changes, devicechange) can call it safely. Inline executor preserves the
  old synchronous behavior.
- **Host providers for all six platforms** (the loud-log stub was judged
  deceptive and deleted — correctly so):
  - **macos** — CoreAudio HAL enumeration (UID stable ids, transport→kind,
    deduped rate ranges), default-input + device-set listeners, VolumeScalar
    gain, AUHAL per-device capture (Float32 interleaved, allocation-free IO
    proc, sink-snapshot multiplexing), `DeviceIsAlive` → on_lost, AVCapture
    consent, and **system-audio loopback via process taps** (macOS 14.2+,
    `AudioHardwareCreateProcessTap` + private aggregate device; never
    enumerated on older OSes).
  - **ios** — AVAudioSession ports, route-change hotplug, current-route
    inputGain caps, single AVAudioEngine capture with tap interleaving;
    second concurrent device open fails loudly (iOS has one input route);
    record-permission consent (AVAudioApplication on 17+).
  - **win32** — IMMDeviceEnumerator eCapture + eRender-as-loopback
    enumeration, IMMNotificationClient hotplug, IAudioEndpointVolume gain,
    event-driven shared-mode capture threads (loopback flag for render
    endpoints, silent-flag zero-fill, s16→f32), consent always granted.
  - **linux** — ALSA: ctl-walk enumeration with cached one-shot hw probing,
    honest `alsa:default` device, capture-volume mixer gain, FLOAT/S16/S32
    capture threads with xrun recovery. **PipeWire is not vendored in
    third-party/ — monitors-as-loopback and OS-visible publish wait on that
    dependency decision.** No hotplug events without udev; no hidden
    polling thread (MEL-ENGINE-III) — refresh-driven only.
  - **android** — AudioManager.getDevices over the platform JNI bridge
    (type+address stable ids where available), AudioDeviceCallback hotplug
    via a Java helper (camera's mechanism, request code 0x4D41), AAudio
    PCM_FLOAT capture with disconnect→on_lost and off-callback-thread
    closer, RECORD_AUDIO consent through the activity; manifest fragment
    ships with the module. Gain absent (no Android API) — caps false.
  - **wasm** — async enumerate-cache bridge (sync enumerate serves the
    cache; refresh promises diff by (id,label) and notify), devicechange
    hotplug, getUserMedia + emscripten AudioWorklet capture (planar→
    interleaved, per-device context), permissions.query consent snapshot +
    getUserMedia-probe authorize, blank pre-consent labels passed through.
- **Build wiring** per axis with each platform's frameworks/libs;
  `thread` dep added (win32/linux capture threads, android teardown);
  `platform` dep on android; manifest + java wired.
- **Verification:** macos lib builds clean (0 warnings) and all 14
  audioin-core tests pass (49 tests across pcm/spectrum/audioin all green);
  ios, android, linux, wasm libs cross-compile clean from the macOS host.
  **win32 is committed and pushed but NOT compiled — the win-pilot box is
  unreachable (ssh timeout)**; the file mirrors `audio/wasapi/wasapi.c`
  COM idioms exactly. Branch `worktree-audio-v2-wireframes` pushed to
  origin for that build.

## audioout (same session, after the form rework)

Implemented `audioout` against its wireframe, form-aligned with the
reworked audioin from the start:

- **Core** — registry/hotplug/default skeleton shared with audioin (no
  consent, no `future` dep — deliberate); volume/mute caps-gated quartet;
  `Mel_AudioOut_Raw` gained `volume`/`muted` shadow fields so external
  volume changes diff into `changed` events through normal reconciliation
  (the live getters still ask the provider; the shadows exist only for
  change detection — recorded in spec.md).
- **Pull plane** — `mel_audioout__open(req, granted*, pull, token)` +
  `__start/__stop/__close` internal bridges (audio engine's door);
  providers pull from openers on their own clock.
- **Publish** — a published output is a device others play into:
  `publish_read` is the pull clock; it zeroes the destination, pulls every
  STARTED opener into scratch, sums, returns the longest fill. Format
  negotiation answers the published format honestly. The `pcm` ring is NOT
  used here (the read IS the clock; nothing to buffer) — spec's pcm
  dependency line no longer applies to audioout and was dropped from
  build.c; flagged here.
- **Tests** — 9 mock-provider tests: reconciliation, default_changed,
  volume/mute caps gating, external-volume `changed` events,
  publish negotiation honesty + pull multiplexing + stop/close semantics,
  dead-handle LOST, silent no-change refresh. All pass.
- **All six host providers** (parallel agents, audioin twins as
  precedent): CoreAudio HAL with per-device VolumeScalar/Mute listeners
  feeding `changed` (macos); AVAudioSession route outputs +
  AVAudioSourceNode render, caps.volume=false (iOS volume is read-only)
  (ios); WASAPI render with IAudioEndpointVolumeCallback per device, mix
  into the device buffer, s16 fallback (win32, unverified — box offline);
  ALSA playback with silence-paced idle clock and Master-element volume
  (linux); AudioManager JNI + AAudio output streams, caps.volume=false
  (STREAM_MUSIC is global, not per-device — audiopolicy's home) (android);
  mediaDevices + AudioContext.setSinkId gate with an honest single
  `web:default` device where setSinkId is absent, AudioWorklet sink (wasm).
- **Verified**: macos clean build + 9/9 tests; ios/android/linux/wasm
  cross-compile clean. win32 committed/pushed, awaiting the build box.

Cross-cutting contract questions the backends raised (for the
audioplayback/audio-delta steps):
- The pull plane has no `on_lost` twin — openers learn device death only
  via hotplug `removed`. wasm/ios/win32 all flagged it. Candidate for a
  spec amendment when `audio` binds.
- `granted.block_frames` semantics vary (render quantum vs period vs
  buffer bound); audio's ring sizing should treat it as a hint, not a cap.
- iOS read-only volume is observable but caps.volume=false suppresses
  `changed` for it — a `caps.volume_observe` split is a wireframe question.
- linux asked for a `caps.mute` split (cards with volume but no switch)
  and a BUSY status bit (both twins).

## audiopolicy (same session)

No provider plugin here — the OS session is singular, so platforms live
behind an internal backend vtable (`mel_audiopolicy__backend()`), with
`mel_audiopolicy__emit()` as the thread-safe door OS notifications push
through. Events split into `<audiopolicy/events.h>` per the form rules.
Core normalizes NULL mode to `mode_default` (the struct's zero value, not
an invented default — noted since spec only names NULL category as the
violation), backends return bare warn bits and write `in_force` honestly,
core attaches WARNED severity and owns `focus_held` (abandon-without-grant
asserts; shutdown abandons).

Backends: **ios** — AVAudioSession 1:1 with progressive lowering on
setCategory failure and `in_force`/warn bits derived from *readback*, never
the request; interruption (began/ended+should_resume), route-change
(reason-mapped), and secondary-audio-hint→should_duck events. **android** —
AudioFocusRequest via a java proxy (focus-change mapping: LOSS→focus_lost,
LOSS_TRANSIENT→interruption_began+focus_lost, CAN_DUCK→should_duck,
GAIN→focus_gained+interruption_ended+should_resume), comms mode drives
MODE_IN_COMMUNICATION + setCommunicationDevice (API 31+), duck_others
shapes the next focus request rather than warning. **wasm** — one probe
AudioContext whose statechange surfaces autoplay interruptions
(suspended→began, running→ended+should_resume); no gesture handlers
installed. **macos** — honest lowering + HAL default-device listeners
emitting route_changed (done directly, not via audioout as the spec
sketched — avoids cross-module init-order coupling; deviation flagged).
**win32** — voice/video-chat mode + duck_others acknowledged (OS comms-role
ducking engages when streams open with the role — the audio delta owns
wiring that role), rest named-ignored. **linux** — fully honest-absent.

9 mock-backend tests (apply lowering/readback, NULL-mode normalization,
override round-trip, focus sequencing with injected events, interruption
payloads, unsubscribe, focus-failure not holding). macos/ios/android/wasm/
linux compile-verified; win32 backend is pure C over core (no COM) and
near-certain, still queued for the build box.

Contract questions raised: a duck-end signal is missing from the event
struct (iOS secondary-audio End and Android GAIN both have one to give);
should permanent focus loss clear `focus_held`; wasm's initial
interruption_began fires before any subscriber exists (readback surface?).

## Kludges

- win32 backend unverified by a compiler (box offline). First action when
  win-pilot returns: `git pull` + `nob build audioin` there.
- linux backend is ALSA, not the spec's PipeWire — blocked on vendoring
  pipewire headers into third-party/ (a dependency decision, not code).
  Loopback/monitors and OS-visible publish on linux wait with it.
- Runtime behavior of the platform backends is compile-verified only; no
  hardware smoke tests ran (mic capture, hotplug, consent prompts). The
  audiocapture step should drive a hello-style smoke app per platform.
- Four foreign modified files (collection/mpmc.h, mpsc.h, core/platform.h,
  log/src/log.c — a cache-line-alignment refactor from a concurrent
  session) were deliberately left uncommitted on the branch.
- Per-backend judgment calls are recorded in the module (and below), the
  notable ones: iOS HeadsetMic→builtin; win32 FormFactor→kind maps only
  Microphone/Headset→builtin, everything else unknown (no USB/BT guessing);
  android default = first builtin mic (no OS default-input API); wasm kind
  always unknown (labels are the only hint; not sniffed); win32/wasm/alsa
  `native()` lifetimes documented in their reports.
- audioin spec deps amended again: `thread` added (capture threads).
- Sink-snapshot pattern accepted race (as in publish.c): a token closed
  concurrently with an in-flight callback can see one final
  on_frames/on_lost.

## CLAUDE.md suggestions (recommendations only)

- None new.

## Suggestions

- Decide PipeWire vendoring (third-party/pipewire) — unblocks linux
  loopback/monitors, OS-visible publish, and the better enumeration plane.
- The macOS publish OS-visibility path (HAL plug-in component) remains the
  separate design doc the wireframe writeup called for.
- Open contract questions from the backends, for the audiocapture step:
  should `default_id` ever name a loopback twin (win32); should `open`
  ever auto-prompt instead of failing `DENIED` pre-consent (ios/wasm chose
  fail-loud, matching "only authorize prompts"); BUSY status bit for
  `-EBUSY`-style opens (linux asked; status set has no BUSY).
