# Custom Widget Definition Model

## Problem Statement

The current widget infrastructure forces every event through a message bus: a single `Mel_Gui_Proc` per widget, dispatch via a thread-local frame stack, class-chain walking for super calls, WPARAM/LPARAM blobs carrying typed data through untyped channels. On Win32, this is roughly zero-cost because Win32 itself is a message bus. On every other platform — Cocoa, Android, web, X11/Wayland, Quest — the framework pays translation: native event handler synthesizes a message, pushes a frame, walks the class chain, calls a proc, the proc calls super, walks complete, pop frame. Multiple function-pointer indirections for what should be one direct call.

I have already accepted that the build system can compile different `.c` files per target. If implementations are already per-platform, there is no reason to invent a universal shape that requires translation everywhere except on the one platform whose shape we copied. The universal shape isn't pulling its weight; per-platform implementations are. The framework should provide handles, callbacks, and primitives, and let each platform implementation use whatever idiom is natural for that platform.

I want defining a widget — first-party or app-defined — to look like writing the native code I would have written if the framework didn't exist, with the framework providing only the glue that makes the result a `Mel_Gui_Handle`.

## Solution

A widget is defined by two pieces:

- **Behaviour** — shared C: a state struct, helper functions that act on the state, and the public API (`_opt`, `create`, setters, accessors).
- **Per-framework implementation** — one file per backend, defining the widget's `create_<widget>_opt` function for that backend. Each implementation decides for itself whether to wrap a native widget, create a canvas and draw, or call into other widgets to compose.

The framework does not impose a visual-mode taxonomy. There is no enum naming "composition vs paint vs native," no vtable selecting between them, no runtime resolver picking one. The build system compiles exactly one per-framework implementation file per target based on the target's chosen backend, and that file's `create_<widget>_opt` is what runs. Different implementations across backends — that is the abstraction.

When no per-framework implementation exists for the chosen backend, the build system falls back to a conventional `canvas/` implementation if the widget ships one, which uses the Canvas primitive (PRD 07) to draw. If no fallback exists, the build fails with a clear error naming the missing backend.

Composition is just code: a widget that is composed of other widgets has a `create_*` function that calls those widgets' own `create_*` functions. No framework feature needed.

## Implementation Decisions

- Each widget lives in its own module or sub-module. The convention for source layout:
  - One shared file with the behaviour (state-mutating helpers, public-API setters/accessors).
  - One per-backend directory containing the `create_<widget>_opt` for that backend.
  - Optionally a `canvas/` directory containing a fallback implementation using the Canvas primitive.

- The build system picks the per-backend file based on the target's backend (set in `build.c`). If no backend-specific file exists and a `canvas/` implementation does, the canvas one is used. If neither exists, build fails.

- The framework provides an internal API (private header, used only by per-framework implementations) for:
  - Registering a native handle as a `Mel_Gui_Handle`
  - Allocating per-instance state
  - Reading user-installed callbacks (the capability structs from PRD 05)
  - Reading the `user` pointer
  - Reading/setting the native parent (for child attachment)

- No vtable per widget. No enum tagging visual modes. No runtime resolver. Per-framework files own creation; the build system selects which file participates.

- For widgets that are compositions of other widgets, no special framework feature is required. The compose function calls other widgets' create functions, passes itself as parent, retains child handles in its state struct for later access. This works identically across all backends because each child widget's per-backend file is selected independently.

- For widgets with no native counterpart on a given backend, the `canvas/` implementation creates an instance of the Canvas widget (PRD 07), installs paint and pointer callbacks, draws the widget's visual into the canvas, and routes pointer events to the behaviour functions. This is one widget calling another — same mechanism as composition.

- Custom widgets defined in app code follow exactly the same model: app-defined widgets place per-backend implementations in `apps/<name>/<backend>/` directories (PRD 10), the build system picks them up automatically.

- The behaviour functions (helpers like `counter_increment(state)`) are typically internal helpers used by the per-framework implementations. A subset may be promoted to public API as setters (`counter_set_count(handle, value)`), in which case the public function looks up the state from the handle and calls the internal helper.

- Calling between widgets across different backends in the same target is impossible by construction: a target has one backend; all widgets in it use that backend's implementations.

## Testing Decisions

A good test of a widget verifies that its public API produces the expected state changes and fires the expected callbacks: creating a counter at initial value 5, calling increment, observing on_count_changed firing with new value 6. Tests run against the behaviour shared file directly when possible; widget-level tests that involve event dispatch from the native layer require a backend-specific test environment.

Modules under test: each widget independently. The framework's per-widget integration (handle registration, callback routing) is tested as part of the GUI core (PRD 05).

Prior art: none in the repo for per-framework widget testing. New tests target the behaviour file directly (pure C, easy to test in isolation) and use input.replay (PRD 09) to drive higher-level integration tests.

## Out of Scope

- A documented list of supported backends in the codebase. PRDs lock the *model*; the actual set of backends grows over time and is documented elsewhere as it is built.
- The Canvas primitive's API surface. That is PRD 07.
- Migration of existing widgets (button, label, edit, slider, checkbox, canvas, panel, window) from the proc-based model to the per-framework-implementation model. The PRD documents the target shape.
- Hot-swapping backends at runtime. A target has one backend chosen at build time.

## Further Notes

The model is closest to SWT (Java's native widget toolkit) and wxWidgets, which similarly use per-platform native implementations under a uniform public API. The naming "backend" rather than "platform" matters: a single platform may have multiple backends (Win32-native vs. WinUI 3 vs. Qt on Windows; Android Views vs. Jetpack Compose on Android; DOM vs. Canvas-only on web). The build system selects per-target-backend, not per-OS.

Composition being "just code" is the load-bearing simplification. Most non-trivial application widgets are compositions of base controls; they do not require platform code. Only widgets that need backend-specific tricks pay the per-backend implementation cost.

The Canvas fallback is the universal escape hatch: when no native widget exists for a backend and the widget ships a canvas implementation, the widget works on that backend transparently. Widgets that cannot or will not ship a canvas implementation fail to build on backends they did not target — which is the correct outcome.
