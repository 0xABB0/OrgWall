# Widget Creation API and Capability Callbacks

## Problem Statement

The current widget creation API uses positional arguments — `mel_gui_create_button(parent, text, id, x, y, w, h, proc, user)` — which is unreadable, fragile against reordering, and hostile to discoverability. Widgets also currently have a single `proc` callback that handles every event via a message-bus dispatch; this lacks type safety, leaks platform-flavored vocabulary (WPARAM/LPARAM), and forces apps to write switch statements over message enums when all they wanted was to handle a click.

I want widget creation to be readable, type-safe, and extensible without growing one giant flat option struct. Widgets share many events — every interactive widget has focus, pointer, keyboard concerns — and listing every callback as a top-level field per widget creates enormous and repetitive structs.

I also want runtime mutability: a widget's click handler should be settable after creation, not only at creation time, and the same path that wired the callback at creation should also work for runtime changes.

## Solution

Widget creation uses the **`_opt` pattern**: an options struct passed by value, designated-initializer fields, a thin macro for ergonomics. Every field of the option struct has a meaningful default (typically zero); users provide only the fields they care about.

Events that widgets receive are grouped into **capability callback structs**, one per orthogonal capability. Lifecycle, focus, pointer, keyboard — each is its own small struct of related function pointers. A widget's option struct nests these capability structs by composition. Designated initializers with dot-path navigation make filling them in concise:

```c
Mel_Gui_Handle ok = mel_gui_button_create(
    .parent = stage,
    .text   = S8("OK"),
    .x = 10, .y = 10, .w = 100, .h = 30,
    .pointer.on_click   = on_ok_clicked,
    .focus.on_focus_in  = on_focused,
    .user = &state,
);
```

For every callback installable at creation time, a setter exists for runtime mutation. The setter and the create-time field are functionally equivalent.

Internally, callbacks are stored per-instance in a lazily-allocated structure: instances with no callbacks installed pay no memory cost; the first call to set any callback allocates the structure.

## Implementation Decisions

The `_opt` pattern shape is fixed and applies to every configurable object in the codebase (widgets, layouts, eventually GPU pipelines, eventually anything else). The shape:

```c
typedef struct {
    /* all configurable fields, no required/optional distinction in the type */
} Mel_Foo_Opt;

Mel_Gui_Handle mel_foo_create_opt(Mel_Foo_Opt opt);
#define mel_foo_create(...) mel_foo_create_opt((Mel_Foo_Opt){__VA_ARGS__})
```

Pass-by-value enables the compound-literal macro to work with C99 designated initializers. The macro is the user-facing API; the `_opt` function is the implementation.

Capability callback structs are defined once in the `gui/` module and reused across all widgets. The set:

```c
typedef struct {
    void (*on_show)         (Mel_Gui_Handle, void* user);
    void (*on_hide)         (Mel_Gui_Handle, void* user);
    void (*on_enable)       (Mel_Gui_Handle, bool enabled, void* user);
    void (*on_destroy)      (Mel_Gui_Handle, void* user);
    void (*on_resize)       (Mel_Gui_Handle, i32 w, i32 h, void* user);
} Mel_Gui_Lifecycle_Cb;

typedef struct {
    void (*on_focus_in)     (Mel_Gui_Handle, void* user);
    void (*on_focus_out)    (Mel_Gui_Handle, void* user);
} Mel_Gui_Focus_Cb;

typedef struct {
    void (*on_pointer_down) (Mel_Gui_Handle, i32 x, i32 y, void* user);
    void (*on_pointer_up)   (Mel_Gui_Handle, i32 x, i32 y, void* user);
    void (*on_pointer_move) (Mel_Gui_Handle, i32 x, i32 y, void* user);
    void (*on_pointer_enter)(Mel_Gui_Handle, void* user);
    void (*on_pointer_leave)(Mel_Gui_Handle, void* user);
    void (*on_click)        (Mel_Gui_Handle, void* user);
    void (*on_double_click) (Mel_Gui_Handle, void* user);
} Mel_Gui_Pointer_Cb;

typedef struct {
    void (*on_key_down)     (Mel_Gui_Handle, Mel_Key, void* user);
    void (*on_key_up)       (Mel_Gui_Handle, Mel_Key, void* user);
    void (*on_char)         (Mel_Gui_Handle, u32 codepoint, void* user);
} Mel_Gui_Keyboard_Cb;
```

A widget's `_opt` struct embeds each capability struct it supports as a named field (`lifecycle`, `focus`, `pointer`, `keyboard`). Widget-specific events (`Counter_Callbacks::on_count_changed`, `Slider_Callbacks::on_value_changed`) are grouped under a control-specific struct, typically named `.on_` in the opt for brevity.

Per-callback setters mirror the create-time fields. Bulk setters per capability also exist:

- `mel_gui_set_pointer_cb(handle, const Mel_Gui_Pointer_Cb*)` replaces the whole capability
- `mel_gui_set_pointer_on_click(handle, fn, user)` replaces one slot

Setting any slot to NULL removes that callback.

Storage of callbacks is lazy:

- A widget with no callbacks installed has no callback struct allocated.
- The first callback installation allocates the callback struct (one allocation per widget instance).
- The wrapper proc (described below) reads from this struct.

Composition between a fully-custom event handler and the capability callbacks:

- If only capability callbacks are set: a framework-generated wrapper dispatches the relevant ones from the native event handler.
- If a fully-custom handler is set (e.g., the widget's per-framework implementation provides a way to install one), the custom handler runs; if it explicitly delegates to the framework wrapper, the capability callbacks fire next.
- Not delegating is a valid choice (intercept-and-consume). Documented; not enforced.

## Testing Decisions

A good test of the callback system verifies that installed callbacks fire when their event occurs, that NULL slots are skipped, that setters at runtime change which function is called, that capability structs are applied wholesale by the bulk setter, that lazy allocation does not fail under stress.

Modules under test: `gui/` for callback storage, dispatch, setters. Per-widget tests verify that each widget's native impl correctly routes the right events to the right capability struct slots.

Prior art: there is no precedent in the repo for callback-routing tests. New tests follow a standard pattern: create widget with known callbacks, simulate an event via the input.replay module (PRD 09), assert the callback fired with expected arguments.

## Out of Scope

- The implementation of `Mel_Key`, `Mel_Mouse_Button`, and other input types. Those are owned by PRD 09 (Input).
- Drag-and-drop callbacks. Future PRD.
- Gesture-derived callbacks (tap, swipe, pinch). Future PRD, possibly inside `gesture/` module.
- Per-widget specific callback contents (Slider's `on_value_changed`, Edit's `on_text_changed`, etc.). Those are defined per widget in their respective headers; this PRD locks only the pattern they follow.

## Further Notes

The capability callback grouping is intended to be stable across widgets. Adding a new event to a capability (e.g., adding `on_pointer_wheel` to `Mel_Gui_Pointer_Cb`) makes the event available on every widget that includes that capability, with no per-widget edits beyond updating the per-framework dispatch to call it.

The `void* user` field on each callback receives the `user` pointer stored on the widget (set at create time via the opt struct, or via `mel_gui_set_user`). Each widget has one user pointer; callbacks share it. Apps with multiple state contexts per widget use a struct containing all of them and pass its address.

The pattern resists drift: every widget looks like every other widget. Reading one widget's header tells you how to read every widget's header.
