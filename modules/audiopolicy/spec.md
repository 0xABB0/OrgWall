# audiopolicy — specification

OS audio routing and arbitration policy: session category and mode, output
override (earpiece↔speaker), bluetooth routing options, mixing/ducking with
other apps, audio focus, interruptions, route-change events. One module
because the OS object is one thing spanning input and output at once (a
duplex category configures both directions in one call); `audiomixer`,
`audiocapture`, `tts`/`stt` honor the applied policy, none of them owns it. A
recorder app gets focus and interruptions without linking the mixer.

## Headers

- `<audiopolicy/audiopolicy.h>` — policy: apply/current, override, focus.
- `<audiopolicy/events.h>` — the policy event stream and subscriptions.

## Open descriptors (never enums)

- `mel_audiopolicy_category`: playback / record / duplex / ambient —
  classifications grow (MEL-CODE-001).
- `mel_audiopolicy_mode`: default_ / voice_chat / video_chat / measurement /
  media.
- `mel_audiopolicy_output`: default_ / speaker (the iOS receiver→speaker
  override).
- `mel_audiopolicy_route_reason`: device_added / device_removed /
  category_changed / override / unknown.

## Apply

```
Mel_AudioPolicy_Status s = mel_audiopolicy_apply((Mel_AudioPolicy){
    .category = mel_audiopolicy_duplex,
    .mode = mel_audiopolicy_voice_chat,
    .mix_with_others = true, .duck_others = false,
    .allow_bluetooth = true, .allow_bluetooth_a2dp = true,
    .default_to_speaker = true,
});
```

Synchronous; lowering names every loss in warning bits — `WARN_MODE_IGNORED`,
`WARN_MIX_IGNORED`, `WARN_DUCK_IGNORED`, `WARN_BLUETOOTH_IGNORED`,
`WARN_OVERRIDE_IGNORED`, `WARN_CATEGORY_LOWERED` — a knob the platform lacks
is reported, never silently dropped (MEL-CODE-007, MEL-ENGINE-VIII). A NULL
category is a loud contract violation: applying policy means saying what you
are. `mel_audiopolicy_current()` returns the policy as actually in force
(post-lowering, the honest readback). `mel_audiopolicy_override_output(port)`
switches live (the in-call speakerphone toggle).

Apps that never call `apply` get the OS's own default session behavior —
that is the OS's default, not a Melody-invented one; the spec names it.

## Focus

```
Mel_AudioPolicy_Status s = mel_audiopolicy_focus_request((Mel_AudioPolicy_Focus_Opt){
    .may_duck_me = true,
});
mel_audiopolicy_focus_abandon();
```

Android audio focus first-class; on platforms without an arbitration model
(desktop, wasm) request answers `OK` and loss events simply never fire —
honest-absent, documented per platform. Focus loss/gain, duck requests, and
interruptions all arrive on the one event stream.

## Events

```
Mel_AudioPolicy_Event {
    bool interruption_began;     // phone call, Siri, another app took the hardware
    bool interruption_ended;
    bool should_resume;          // OS says resuming is appropriate
    bool should_duck;            // lower volume, keep playing
    bool duck_ended;             // stop ducking
    bool focus_lost;
    bool focus_gained;
    bool route_changed;
    const mel_audiopolicy_route_reason* reason;
}
```

`subscribe(exec, cb, user)` marshals to the subscriber's executor; booleans
compose (never an enum). The module observes its own stream: permanent
`focus_lost` (without `interruption_began`) releases the held focus grant,
`focus_gained` re-arms it — `focus_abandon` after a final loss is a no-op,
not a misuse. Consumers react at their level: the `audiomixer` engine
observes interruptions itself (its device goes away — engine semantics live
in `audiomixer`'s spec); the *app* decides policy reactions (pause music on
interruption, resume on `should_resume`, lower volume on `should_duck`).

## Concurrency

Caller-driven public API; OS notifications marshal to the deliver executor.
No threads spawned, no timers (MEL-ENGINE-III).

## Failure

NULL category in `apply`: loud error. `focus_abandon` without a grant:
debug assert. `init` twice / `shutdown` without: asserts. Every lowering
named in status bits.

## Platform lowering

- iOS — AVAudioSession: category/mode/options map 1:1; interruption and
  route-change notifications; `override_output` = port override. The fullest
  platform.
- android — audio focus (`requestAudioFocus`, duck/pause signals),
  `setCommunicationDevice` for voice_chat routing, AudioAttributes derived
  from category+mode via the `platform` JNI bridge.
- macOS — no session object: category/mode lowered to nothing with named
  warnings; interruptions honest-absent; route events synthesized from
  default-device changes (via `audioout`).
- win32 — communications-role ducking (the OS ducks others for voice_chat
  mode); everything else named-ignored.
- linux — honest-absent: apply warns per knob; events never fire.
- wasm — autoplay-policy interruptions surfaced as interruption events
  (AudioContext suspended/resumed); the rest named-ignored.

## Dependencies

- `core` — types, asserts.
- `allocator` — subscription registry.
- `collection` — subscription slotmap.
- `executor` — event delivery.
- `event` — the policy event channel that marshals to subscriber executors.
- `log` — lowering and event diagnostics.
- `platform` (android only) — JNI bridge.

## Test contract

Mock backend proves: apply lowering warnings per knob, current() honest
readback, override round-trip, focus request/abandon/loss-event sequencing,
interruption event payloads (began/ended/should_resume/should_duck),
subscription lifecycle. No hardware.
