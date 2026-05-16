# Input Subsystem

## Problem Statement

The current code has no input module. Pointer and keyboard events are routed through the GUI message bus; gamepad and XR are not handled at all; MIDI input lives in `music.midi`; programmatic input for testing does not exist as a primitive. Lumping these together into a top-level `input/` module was tempting but wrong — they are wildly different concepts with different dependency graphs:

- Mouse and keyboard arrive through window-bound OS messages (WM_KEYDOWN, NSEvent on a key window, MotionEvent on a View, DOM event on a canvas). They cannot exist without GUI surface infrastructure feeding them.
- Touch and pen are similar — surface-bound.
- Gamepad and joystick arrive through dedicated free-standing OS APIs (XInput, GameController framework, evdev, Gamepad API). They have nothing to do with GUI.
- XR controllers, hand tracking, and gaze come from OpenXR or the platform's XR framework — also free-standing and platform-specific.
- MIDI input comes from MIDI devices and is a musical concept, not a generic input one.
- Programmatic input (replay) is the inverse of all the above: it writes to whichever input modules the test or scripted cutscene needs to drive.

Game apps want polling: "is W held right now?" Form apps want edge events: "the user just pressed Enter." Both shapes need to coexist, fed by the same underlying event stream.

## Solution

Per-device modules at the top level. No umbrella `input/` directory. Each module is self-contained, has its own platform code where relevant, and exposes a consistent triplet of APIs: state polling, dispatch entry point, frame-advance hook.

The modules:

- **`kbm/`** — keyboard + mouse. Surface-bound; fed by `gui.platform.*` translating native window-attached events into `kbm`'s dispatch.
- **`touch/`** — touch + stylus. Surface-bound; fed by `gui.platform.*` the same way.
- **`gamepad/`** — gamepad + joystick. Free-standing; owns per-platform backends for XInput, GameController, evdev, the Android `InputDevice` API, and the web Gamepad API.
- **`xr/`** — XR controllers, hand tracking, gaze. Free-standing; OpenXR-based on all platforms that support it.
- **`replay/`** — programmatic event injection. Writes into the dispatch entry points of the other input modules. For tests and scripted cutscenes.

Each module exposes:

- **State polling** — "is key W down right now? what is the mouse position?" Used by games and any code asking "what is currently true?"
- **Dispatch entry point** — "key W just went down with these modifiers." Called by whoever ingests events (the GUI platform layer, the gamepad backend, the replay module).
- **Frame-advance hook** — rolls edge bits at frame boundaries (transitions `pressed_this_frame` to cleared, etc.). Called by the GUI's frame dispatcher just before broadcasting `on_frame` callbacks, or by headless apps that drive their own frame cadence.

Crucially: **input modules do not depend on `gui`**. The dependency arrow points the other way. `gui.platform.*` translates OS events into both (1) input module dispatches that update state, and (2) GUI widget callback fires that deliver edge events. Two separate calls from the same OS event handler; no circular dependency.

## Implementation Decisions

- Each input module has its own top-level directory. No shared parent.
- `music.midi/` remains where it is (musical input, not generic).
- Each module exposes three API layers cleanly separated:
  - **State** layer (poll): `mel_kbm_key_down(Mel_Key)`, `mel_kbm_mouse_position(...)`, `mel_gamepad_state(slot, out)`, etc.
  - **Dispatch** layer (write): `mel_kbm_dispatch_key(key, down, modifiers)`, `mel_touch_dispatch(...)`, called by ingesters.
  - **Advance** layer (frame boundary): `mel_kbm_advance_frame()`, etc., called once per frame by the frame dispatcher.

- Surface-bound modules (`kbm/`, `touch/`) have no per-platform code of their own. They are fed by `gui.platform.<backend>` modules which translate OS events.
- Free-standing modules (`gamepad/`, `xr/`) own their per-platform backends, structured the same way as widget per-backend implementations.
- The `replay/` module writes to any input module's dispatch entry point. It is the test/scripting tool of choice.
- Edge state (pressed_this_frame, released_this_frame) is maintained by each module. The advance-frame hook clears it.
- Pointer events on widgets (the `Mel_Gui_Pointer_Cb` capability struct from PRD 05) are delivered by the GUI platform layer alongside the input module dispatches; both happen from the same OS event handler.
- Gamepad enumeration and disconnection events fire via callbacks installable on the gamepad module: `mel_gamepad_on_connect(fn, user)`, `mel_gamepad_on_disconnect(fn, user)`.

## Testing Decisions

A good test of an input module fires synthetic events via the module's dispatch entry point, then asserts the state-polling API returns the expected values. For edge state, the test fires an event, calls `advance_frame`, fires it again, and asserts the edge transitions.

The `replay/` module is the testing tool for higher-level integration: drive a sequence of events through the system, observe widget callbacks firing, assert app state matches expectations.

Modules under test: each input module independently. Integration tests use `replay/` to drive scenarios.

Prior art: none in the repo for input. The `midi-monitor` app has a polling thread reading from a MIDI port that resembles the pattern free-standing modules will follow.

## Out of Scope

- High-level gestures (tap, swipe, pinch, long-press). A `gesture/` module derives these from kbm/touch dispatches; that module is its own future PRD.
- IME (input method editor) integration for text input. Falls under keyboard but has enough complexity for its own treatment.
- Force feedback / haptic output. Output direction; separate concern from input modules.
- Accessibility input (screen reader, switch control). Important but its own scope.
- Network input as a generic concept. If needed, it is just a `Mel_Channel` over a socket; not an "input module."
- TUI input (`tui/` or similar). Defer until a TUI use case actually exists.

## Further Notes

The dependency inversion — `gui` depends on input, not the reverse — makes headless and TUI apps possible without the GUI infrastructure. A pure gamepad-driven test runner links `gamepad/` and `replay/` and runs without `gui/` at all. A scripted cutscene in a game replays predetermined inputs through `replay/` while the rest of the game proceeds normally.

Frame-advance hook ordering is fixed: input advance happens before any `on_frame` callback fires, so game code polling state during `on_frame` sees correctly-rolled edge bits.
