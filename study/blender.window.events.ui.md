# Blender Window Manager, Events & UI Study

Source: `~/repo/suck/blender/source/blender/windowmanager/` and `source/blender/editors/interface/`

## Window Management

### Object hierarchy

```
wmWindowManager (one per process, DNA ID -- stored in .blend!)
  -> ListBase<wmWindow>
       -> wmWindow.parent (child windows / dialogs)
       -> wmWindow.global_areas (top-bar, status-bar)
       -> wmWindow.workspace_hook -> WorkSpace -> WorkSpaceLayout
             -> WorkSpaceLayout -> bScreen
                   -> bScreen.areabase -> ScrArea (editor panels)
                         -> ScrArea.spacedata -> SpaceLink (first = active)
                         -> ScrArea.regionbase -> ARegion*
                         -> ScrArea.handlers -> wmEventHandler list
       -> wmWindow.runtime->event_queue
```

### wmWindow

Saveable part in DNA. Live OS handle in `runtime->ghostwin` (GHOST_IWindow*). Each window owns its own GPU context: `runtime->gpuctx` (GPUContext*).

**Each window has its own GPU context.** Must be explicitly activated before drawing: `wm_window_make_drawable()` calls `GPU_context_active_set()`.

### bScreen, ScrArea, ARegion

- `bScreen` is a DNA ID (layout blueprint). Window references active screen via workspace hook.
- `ScrArea` typed by `spacetype` with `SpaceType*` vtable. First `spacedata` item = active editor data.
- `ARegion` typed by `regiontype` (RGN_TYPE_WINDOW, HEADER, TOOLS, etc.). Owns `View2D`, `uiblocks`, `handlers`.

### Workspace switching

`ND_WORKSPACE_SET` notifier -> `WM_window_set_active_workspace` swaps in different `bScreen`. Old screen is dormant in WorkSpaceLayout, restored on re-activation.

## Event System

### GHOST -> Event Queue

GHOST delivers platform events via callbacks -> `wm_event_add_ghostevent()` converts to `wmEvent` -> appended to per-window `event_queue`. Plain linked list.

### Main Loop (`WM_main`)

```
loop:
  wm_event_do_notifiers(C)   // process deferred notifiers, tag regions dirty
  wm_event_do_handlers(C)    // drain event queues for all windows
  wm_draw_update(C)          // redraw tagged windows
```

### Event Dispatch

For each window, for each event on queue:
1. **Modal handlers first** -- `win->runtime->modalhandlers`. Consume events exclusively.
2. **Area + region handlers** -- find area under cursor, find region, dispatch to `region->runtime->handlers`. If not consumed, area handlers.
3. **Window-level handlers** -- global shortcuts.

`WM_HANDLER_BREAK` propagates upward to stop further dispatch.

### Handler Types

- `WM_HANDLER_TYPE_KEYMAP` -- lookup in `wmKeyMap`, find `wmKeyMapItem`, invoke operator
- `WM_HANDLER_TYPE_UI` -- UI system processing (button hover/click, menus)
- `WM_HANDLER_TYPE_OP` -- running modal operator, forwards all events
- `WM_HANDLER_TYPE_DROPBOX` -- drag-and-drop
- `WM_HANDLER_TYPE_GIZMO` -- gizmo handling

## Operators (invoke/modal/exec)

### wmOperatorType vtable

```c
struct wmOperatorType {
  const char *idname;           // "OBJECT_OT_delete"
  StructRNA *srna;              // RNA type for properties
  wmOperatorStatus (*exec)(bContext *, wmOperator *);
  wmOperatorStatus (*invoke)(bContext *, wmOperator *, const wmEvent *);
  wmOperatorStatus (*modal)(bContext *, wmOperator *, const wmEvent *);
  void (*cancel)(bContext *, wmOperator *);
  bool (*poll)(bContext *);
  // ...
};
```

### Call flow

- **exec**: non-interactive (Python, redo, macros)
- **invoke**: first user action. Can call exec immediately (FINISHED), or push modal (RUNNING_MODAL)
- **modal**: receives all subsequent events until FINISHED/CANCELLED

Registration: `WM_operatortype_append(fn)` -> global hash map keyed by `idname`.

### Notifiers

Lightweight broadcast bus. `WM_event_add_notifier(C, NC_SCENE|ND_FRAME, scene)` -> deduplicated via hash set. In `wm_event_do_notifiers`, all `ARegion` listeners iterated -- matching `listener()` callbacks set `region->do_draw`.

## UI Architecture

### Three layers: Block -> Button -> Layout

Rebuilt every frame (immediate-mode inspired), cached via per-region frame buffer.

**Block** -- unit of UI generation. Contains `Vector<unique_ptr<Button>>` and `ListBase<LayoutRoot>`.

**Button** -- single interactive element:
- `rnapoin` (PointerRNA) + `rnaprop` (PropertyRNA*) -- RNA-bound, auto read/write
- `optype` + `opptr` -- invokes operator on click
- `func` -- raw callback (legacy)
- `type` -- ButtonType enum (label, number, slider, text, checkbox, menu, etc.)

**Layout** -- tree of layout nodes. Subtypes: Row, Column, Grid, Split, Box, Radial (pie menu). Two-pass: estimate (measure) then resolve (position).

### RNA-driven UI

`layout->prop(ptr, prop)` -> queries `RNA_property_type/subtype` -> maps to ButtonType automatically (BOOLEAN->checkbox, INT+FACTOR->slider, ENUM->menu). Button stores `ptr` and `prop`.

At interaction: `button_apply_data()` calls `RNA_property_*_set()`.
At draw: `button_draw()` calls `RNA_property_*_get()`.

No manual sync. The button IS the RNA accessor.

## RNA (Runtime Naming Architecture)

### Purpose

Uniform, introspectable interface over arbitrary C struct data. Used by: property panels, Python `bpy.*`, animation system, library overrides, undo.

### Type system

**BlenderRNA** -- root. Owns `Vector<unique_ptr<StructRNA>>` + `Map<StringRef, StructRNA*>`.

**StructRNA** -- represents a C struct type:
- `ContainerRNA cont` -- linked list of PropertyRNA
- `StructRNA *base` -- inheritance
- `StructRefineFunc refine` -- get most-derived type at runtime
- `StructRegisterFunc reg` / `unreg` -- for Python subclassing

**PropertyRNA** -- base for all property types:
- `identifier`, `name`, `description`
- `type` + `subtype` -- determines widget choice
- `update` callback -- fires after set, triggers notifiers
- `rawoffset` + `rawtype` -- direct struct field access (fast path from makesrna)

Concrete subtypes: Bool, Int, Float, String, Enum, Pointer, Collection -- each with typed getter/setter.

**PointerRNA** -- fat pointer: `{ ID *owner_id; StructRNA *type; void *data; }`.

### Registration

At startup, `RNA_init()` calls every `RNA_def_*` function. `makesrna` build tool generates `rna_*_gen.c` with verified offsets and defaults.

### RNA -> Python

Each StructRNA has `py_type` void pointer to Python type object. `bpy.data.objects["Cube"]` creates PointerRNA, wraps into `pyrna_struct_Type`. Property access calls RNA getters/setters. Python never stores raw C pointers.

## Multi-Window

- Each window gets own GHOST_IWindow + GPUContext
- Only one window "drawable" at a time (`wm->runtime->windrawable`)
- Context switching is explicit: `GPU_context_active_set(win->runtime->gpuctx)`
- **No thread-per-window** -- entire WM/event/draw loop is main thread
- Background jobs via `wmJob` on worker threads, communicate back via notifiers
- Single event callback from GHOST routes to per-window queues

## Key Patterns

- **Operator invoke/modal/exec**: clean pattern for interactive tools
- **Notifier broadcast bus**: decoupled region invalidation
- **RNA-driven UI**: buttons auto-bind to properties, no manual sync
- **Per-window GPU context**: explicit activation, one drawable at a time
- **Single-threaded main loop**: notifiers -> events -> draw
- **Everything is a DNA ID**: even wmWindowManager is serialized to .blend
