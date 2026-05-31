# 02 — Navigator instance stack

Parent: `../spec-next.md`. Depends on 01. The core fix: navigation history becomes
a stack of *instances*, not a `back` field on the identity singleton.

## Goal

Introduce `Mel_Navigator` and rewrite `push` / `replace` / `back` (and
`pop_to` / `pop_to_root`) against it. One implicit Navigator for now (the
single-Root case that mobile, web, and simple desktop all use); multi-Root arrives
in spec 04. Thread a per-navigation `arg` through the builder.

## New file

`modules/gui/src/nav.c`, declared in `gui_internal.h`. `screen.c` keeps the
registry; `nav.c` owns the stack and the verbs. Both sit under the `mel_app_*`
public surface (`include/gui/screen.h`).

## Data model

```c
typedef struct {
    str8           name;     /* into the registry */
    Mel_Gui_Handle frame;    /* this instance's live frame */
    void*          arg;      /* per-navigation payload */
} Mel_Nav_Entry;

typedef struct {
    Mel_Nav_Entry* entries;  /* stack; top is last; dynamic, mel_gui__alloc() */
    u32            count, cap;
    bool           own_root;
} Mel_Navigator;
```

`back` is **stack order**, never a field. Because the registry holds no live state,
a name may appear in multiple entries: repeats, cycles, and two instances of one
screen all work. The builder runs on every push, restoring rebuild-on-re-entry.

In this step there is exactly one global `Mel_Navigator` (the implicit foreground
Navigator). Spec 04 generalises to many.

## Builder signature change

```c
typedef void (*Mel_Screen_Build)(Mel_Gui_Handle frame, void* arg);
```

`arg` is the per-navigation payload when present, else the registry
`default_user`. Resolution rule: `arg = nav_arg ? nav_arg : def->default_user`.
Update `hello-world-gui` builders to the new parameter name (they ignore it today).

## Verbs

`from` names the target Navigator; in this single-Navigator step it is accepted
and validated against the foreground Navigator but otherwise resolves to the one
stack. Keep the parameter so 04 needs no signature churn.

```c
void mel_app_push   (Mel_Gui_Handle from, str8 name, void* arg);
void mel_app_replace(Mel_Gui_Handle from, str8 name, void* arg);
void mel_app_back   (Mel_Gui_Handle from);
void mel_app_pop_to (Mel_Gui_Handle from, str8 name);
void mel_app_pop_to_root(Mel_Gui_Handle from);
```

Semantics, invariant across platforms (desktop emulates hide/show):

- `push`: create the screen's frame, build it, make it the visible top; hide the
  previous top; push the entry.
- `replace`: create + build the new frame, make it top, **destroy** the previous
  top's frame and pop its entry, then push the new entry. Predecessor is not on
  the back stack.
- `back`: destroy the top frame, pop its entry, reveal the new top. No-op (assert
  in debug? — no: legitimate at root) when only the root entry remains.
- `pop_to` / `pop_to_root`: repeated `back` to the named entry / to depth 1.

Frame creation + build + autosize currently in `ensure_created` moves here as a
per-push `instantiate(entry)`; the `created` latch is deleted. (Autosize itself
relocates to the backend in spec 05; for now leave the call where it works.)

## present in this step

`mel_app_present(name, arg)` temporarily aliases `push` against the single
Navigator so the API is coherent while spec 04 builds real multi-Root. Note the
alias in a comment referencing spec 04.

## Migration

`mainscreen.c` calls `mel_app_replace(h, S8("replaced"))` and
`mel_app_present(S8("details"))`; `replacescreen.c` calls `mel_app_back(h)`. Update
to the new `arg`-carrying signatures (pass `NULL`).

## Done when

- `back` survives `A→B→A→B`: no clobbered history; each `back` returns correctly.
- Pushing the same screen twice yields two independent instances.
- `replace` then `back` does not return to the replaced screen.
- `hello-world-gui` navigation works through push/replace/back.
