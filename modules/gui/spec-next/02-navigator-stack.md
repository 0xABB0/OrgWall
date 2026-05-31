# 02 — Navigator instance stack

Parent: `../spec-next.md`. Depends on 01. The core fix: navigation history becomes
a stack of *instances*, not a `back` field on the identity singleton.

## Goal

Introduce `Mel_Navigator` and rewrite `push` / `replace` / `back` (and
`pop_to` / `pop_to_root`) against it. Thread a per-navigation `arg` through the
builder.

As built (deviation from the original draft): **per-Root Navigators were pulled
forward into this step.** `hello-world-gui` already opens coequal windows via
`present` and returns to them, so a single implicit Navigator with
`present`-aliases-`push` would have regressed it (hiding a window with no way
back). The model is therefore a set of Navigators, one per Root; `present` creates
a new Root + Navigator rather than aliasing `push`. Spec 04 is consequently
reduced to the per-platform `present` *degrade* matrix and the capability query —
the data-structure work lives here.

## New file

`modules/gui/src/nav.c`, declared in `gui_internal.h`. `screen.c` keeps the
registry; `nav.c` owns the stack and the verbs. Both sit under the `mel_app_*`
public surface (`include/gui/screen.h`).

## Data model

As built (`nav.c`):

```c
typedef struct {
    str8           name;     /* into the registry */
    Mel_Gui_Handle frame;    /* this instance's live frame */
    void*          arg;      /* per-navigation payload */
} Mel_Nav_Entry;

typedef struct {
    Mel_Array(Mel_Nav_Entry) entries;   /* stack; top is last */
} Mel_Navigator;

static Mel_Array(Mel_Navigator) g_navs; /* one Navigator per Root */
```

`back` is **stack order**, never a field. Because the registry holds no live state,
a name may appear in multiple entries: repeats, cycles, and two instances of one
screen all work. The builder runs on every push, restoring rebuild-on-re-entry.

`nav_of(from)` resolves `from` to its Navigator by `mel_gui__toplevel(from)` then
scanning `g_navs` for the entry whose `frame` matches. Only `present` ever grows
`g_navs`, and it holds no `Mel_Navigator*` across that growth; the in-stack verbs
never grow `g_navs`, so the `nav_of` pointer stays valid for their duration.
`mel_gui__navs_reset` (called from gui shutdown) frees each Navigator's entries
then `g_navs`.

## Builder signature change

```c
typedef void (*Mel_Screen_Build)(Mel_Gui_Handle frame, void* arg);
```

`arg` is the per-navigation payload when present, else the registry
`default_user`. Resolution rule: `arg = nav_arg ? nav_arg : def->default_user`.
Update `hello-world-gui` builders to the new parameter name (they ignore it today).

## Verbs

`from` resolves to its owning Navigator via `nav_of(from)`.

```c
void mel_app_present    (str8 name, void* arg);                       /* new Root + Navigator */
void mel_app_push       (Mel_Gui_Handle from, str8 name, void* arg);
void mel_app_replace    (Mel_Gui_Handle from, str8 name, void* arg);
void mel_app_back       (Mel_Gui_Handle from);
void mel_app_pop_to     (Mel_Gui_Handle from, str8 name);
void mel_app_pop_to_root(Mel_Gui_Handle from);
```

Semantics, invariant across platforms (desktop emulates hide/show):

- `present`: `instantiate(name)`, seed a fresh Navigator with that single entry,
  push it onto `g_navs`, show the frame (`nav_replace(frame, NONE)` — nothing to
  hide, so the previous Root stays visible: coequal windows on desktop).
- `push`: capture the current top frame, `instantiate(name)`, push the entry,
  `nav_replace(new, prevTop)` (show new, hide previous top).
- `replace`: capture the top entry, `instantiate(name)`, overwrite the top entry
  in place, `nav_replace(new, old)`, then `mel_gui_destroy(old.frame)`. Predecessor
  destroyed, not on the back stack. New-before-destroy ordering keeps the frame
  count from hitting zero (which would quit the reactor).
- `back`: pop the top entry, `nav_back(prevTop, popped)` (show predecessor, hide
  popped), `mel_gui_destroy(popped.frame)`. No-op when only the root entry remains
  (legitimate at root, not an assert).
- `pop_to` / `pop_to_root`: resolve the Navigator once, then pop+destroy from the
  top down until the named entry / depth 1; reveal the final top once at the end
  (not via repeated `back`, since `from`'s frame may be among the destroyed).

`instantiate(name, arg)` = `mel_frame_create(.title=name)` → resolve
`arg ? arg : def->default_user` → `def->build(frame, user)` → `autosize_frame`.
The `created` latch is gone; the builder runs per instance. `autosize_frame` moved
verbatim from `screen.c` into `nav.c`; it relocates to the backend in spec 05.

## Migration (as built)

Per Gabbo's verb mapping for the demo:

- `main.c` startup: `mel_app_present(S8("main"), NULL)`.
- "Open Details" → `mel_app_present(S8("details"), NULL)` — a new window (Root).
  `details` stays back-button-less; closed to dismiss (reconciliation in spec 03).
- "Open Structural" → `mel_app_push(h, S8("structural"), NULL)`; its "Back to Main"
  → `mel_app_back(h)`.
- "Replace With Replaced Screen" → `mel_app_replace(h, S8("replaced"), NULL)`
  (destroys `main`); the replaced screen's button → `mel_app_present(S8("main"),
  NULL)` ("Re-open Main Window").
- Other GUI apps (`display-gui`, `hello-gpu`, `midi-monitor`, `barcode-gui`) only
  needed the `present` arg added (`, NULL`).

## Done when

- `back` survives `A→B→A→B`: no clobbered history; each `back` returns correctly.
- Pushing the same screen twice yields two independent instances.
- `replace` then `back` does not return to the replaced screen.
- `hello-world-gui` navigation works through present/push/replace/back. ✓ builds
  clean (cocoa) and launches without assert; full interactive pass pending a
  Windows/where-clicked run.
