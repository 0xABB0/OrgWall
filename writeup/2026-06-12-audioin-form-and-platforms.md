# 2026-06-12 — audioin form rework + all six host providers

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
