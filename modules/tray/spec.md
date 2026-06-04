# tray — Spec

The desktop-class system tray / status-icon surface. A status icon plus a nested menu, created and
mutated explicitly, lowered onto each desktop's native tray. Not a notification surface, not a menu
bar.

## Model

- **Objects** — `Mel_Tray` (status icon + top-level menu), `Mel_Tray_Menu` (a menu, top-level or
  submenu), `Mel_Tray_Item` (a menu entry). Each is a value handle over `Mel_SlotMap_Handle` (the
  `display` idiom); generation guards turn use-after-destroy into `_alive == false`. `_NULL` is
  zero; `_equal` compares index and generation.
- **Providers** — a runtime vtable registry (the `vibration` idiom); the host backend registers at
  `mel_tray_init`. One provider is active at a time — the system tray is a single resource, not an
  enumeration. The active provider is the first registered whose `supported()` is true; an internal
  seam forces a specific provider for tests (no silent reselection at runtime).
- **Events** — delivered on the `event` channel, pull (`poll_events`) and push (`subscribe`)
  faces, the `display` precedent. The module owns no thread.

## Objects

```c
void mel_tray_init(const Mel_Alloc* alloc, Mel_Executor* exec);
void mel_tray_shutdown(void);
bool mel_tray_supported(void);

Mel_Tray_Create_Result mel_tray_create_opt(Mel_Tray_Opt opt);   // variadic mel_tray_create(...)
void                   mel_tray_destroy(Mel_Tray t);
bool                   mel_tray_alive(Mel_Tray t);
bool                   mel_tray_equal(Mel_Tray a, Mel_Tray b);
```

One process-global registry (the `display`/`vibration` precedent). `mel_tray_create` returns
`{ Mel_Tray, status }`; an invalid handle is the did-not-start signal. Destroy tears down the tray,
its menu, and all descendants recursively, invalidating every handle.

## Status

The engine-wide `{ value, status }` convention. Status is a `u32` severity (`Ok | Warned | Error`,
mask `0x3`) plus a flag bitset — not an enum (MEL-CODE-001). Branch-free `mel_tray_failed` /
`mel_tray_warned`. The human-readable cause goes to `mel_log_error("tray", …)` at the failure site.

- Warning bits — `ImageRescaled`, `TooltipDropped`, `TitleDropped`, `SubmenuFlattened`: a fidelity
  loss the platform forced (win32 has no tray title; the Linux shell tray has no tooltip).
- Error bits — `NoProvider`, `InvalidArg`, `DeadHandle`, `BackendFail`: the operation did not take
  effect.

## Image

`Mel_Tray_Image` is a raw RGBA8 buffer (`rgba`, `width`, `height`) **or** a file path (`path`);
`template_mask` marks a monochrome template image (macOS dark/light auto-tint). The module copies
the buffer and path into its allocator (MEL-CODE-003), so the caller's storage need not outlive the
call. An empty image leaves the platform default.

## Menu

```c
Mel_Tray_Menu        mel_tray_menu(Mel_Tray t);                          // top-level menu
Mel_Tray_Item_Result mel_tray_item_add(Mel_Tray_Menu m, Mel_Tray_Item_Desc d);
Mel_Tray_Item_Result mel_tray_item_insert(Mel_Tray_Menu m, u32 at, Mel_Tray_Item_Desc d);
Mel_Tray_Status      mel_tray_item_remove(Mel_Tray_Item item);
Mel_Tray_Item_Result    mel_tray_separator_add(Mel_Tray_Menu m);
Mel_Tray_Submenu_Result mel_tray_submenu_add(Mel_Tray_Menu m, str8 label);
```

A menu holds a dynamic array of item handles (MEL-CODE-002) — no fixed cap. `insert` places at an
index, `add` appends; an out-of-range index is an `InvalidArg` error, not a clamp (MEL-CODE-007). A
submenu is an item whose `submenu` handle is a fresh `Mel_Tray_Menu`; nesting is arbitrary depth.

### Entry kind

Entry kind is a flag bitset (`MEL_TRAY_ITEM_BUTTON | CHECKBOX | SEPARATOR | SUBMENU`), **not an
enum** (MEL-CODE-001): a descriptor field that the backend reads, deliberately open to future kinds
without an ABI break. `ENABLED` / `CHECKED` are further flags. Exactly one kind bit is set per item;
an item created with no kind bit defaults to `BUTTON` only at the `add` boundary, logged-adjacent —
the absence of a silent runtime default elsewhere (MEL-CODE-007). `set_checked` on a non-checkbox is
an `InvalidArg` error.

### Per-entry callback

Each entry carries `on_activate` + `user`. A click dispatches through the provider into
`mel_tray__dispatch_item_clicked`, which toggles a checkbox's `CHECKED` flag (and pushes the new
flag to the backend), fires an `ITEM_CLICKED` event, then calls the callback. `set_callback`
rebinds it.

## Provider registry

```c
typedef struct {
    const char* name; void* user;
    bool (*supported)(void* user);
    Mel_Tray_Status (*create)(void* user, const Mel_Tray_Lowered* lowered);
    void            (*destroy)(void* user, u64 token);
    Mel_Tray_Status (*set_image|set_tooltip|set_title|set_visible)(…);
    Mel_Tray_Status (*menu_create)(void* user, u64 menu_token);
    void            (*menu_destroy)(void* user, u64 menu_token);
    Mel_Tray_Status (*item_add)(void* user, const Mel_Tray_Item_Lowered* lowered);
    void            (*item_remove)(void* user, u64 token);
    Mel_Tray_Status (*item_set_label|item_set_flags)(…);
    void* (*native)(void* user, u64 token);
} Mel_Tray_Provider_Desc;
```

The core owns handles, the menu tree, and string/image lifetime; the provider receives `*_Lowered`
descriptors carrying packed-handle tokens (`mel_slotmap_handle_pack64`) and materializes native
objects. The provider dispatches back through `mel_tray__dispatch_{activate,item_clicked,menu}` with
those tokens; the core unpacks, validates the handle, and fires the event.

`mel_tray_native(t)` returns the provider's native object (`NSStatusItem*`, the popup `HMENU`, the
`AppIndicator*`) past the hatch; the consumer owns correctness beyond it.

## Platform lowering

- **macOS — `NSStatusItem`.** Host provider attaches a variable-length status item to the system
  `NSStatusBar`; the top-level menu is the item's `NSMenu`, so a click opens it. Items are
  `NSMenuItem`; checkbox state is `NSControlStateValue`; submenu is `setSubmenu:`. A shared
  `MelTrayTarget` is the action target; RGBA buffers become `NSBitmapImageRep`-backed `NSImage`
  (template-tinted when `template_mask`).
- **win32 — `Shell_NotifyIconW`.** A `HWND_MESSAGE` window receives the callback message; right-click
  tracks a popup `HMENU` via `TrackPopupMenu`; item commands arrive as `WM_COMMAND` and map by
  command id back to the item token. RGBA → 32-bit `CreateDIBSection` → `CreateIconIndirect`. No
  tray title (`TitleDropped`).
- **Linux — `libappindicator` + GTK3.** Loaded with `dlopen` (Ayatana first, then legacy), no hard
  link dependency. The menu is a `GtkMenu`; items are `GtkMenuItem` / `GtkCheckMenuItem`; activation
  is a `g_signal_connect` `activate` handler carrying the token. Honest-absent when the libraries or
  a display are missing (`supported()` false). The shell tray has no tooltip (`TooltipDropped`); the
  icon is a named/file icon (raw RGBA warned as `ImageRescaled`).
- **iOS / Android / wasm — honest-absent.** No system tray exists; the host provider registers
  nothing, `mel_tray_supported()` is false (MEL-ENGINE-VII).

## Events

```c
u32 mel_tray_poll_events(Mel_Tray_Event* out, u32 cap);
Mel_Tray_Subscription mel_tray_subscribe(Mel_Executor* exec, Mel_Tray_Event_Callback cb, void* user);
```

`Mel_Tray_Event` carries a kind flag (`ACTIVATED | ITEM_CLICKED | MENU_OPENED | MENU_CLOSED`), the
tray, the item (for clicks), and a button bitset. Pull drains the ring; push delivers on the
consumer's executor (the `display` machinery). Subscribe without an executor (none passed, none at
init) is a loud `NULL` (MEL-ENGINE-VIII).

## Coding-guideline compliance

- **No enums** — kind/state/event-kind/buttons are flag bitsets; status is severity-plus-bitset.
- **No fixed arrays** — menu children, provider list, tray/menu/item maps are dynamic.
- **Allocators** — every allocating call takes `Mel_Alloc*`; the registry holds it.
- **No silent defaults** — out-of-range insert, dead handle, checkbox misuse, missing provider all
  fail loud.

## Failure modes

- Create with no supported provider ⇒ `NoProvider`/`Error`, `MEL_TRAY_NULL`.
- Operation on a destroyed handle ⇒ `DeadHandle`/`Error`, logged.
- `item_insert` past the end ⇒ `InvalidArg`/`Error`.
- `set_checked` on a non-checkbox ⇒ `InvalidArg`/`Error`.
- Backend native call fails ⇒ `BackendFail`/`Error`, logged at the site.
- Platform lacks tray (mobile/web) ⇒ honest-absent; `supported()` false; create fails `NoProvider`.
- Subscribe with no executor ⇒ loud `NULL` subscription.

## Dependencies

`core`, `allocator`, `collection` (slotmap, array), `string`, `event`, `executor`, `log`. No `gpu`,
no `paint`/`image` hard dependency: the icon is a raw RGBA buffer or a path the backend loads, so the
heavier pixel-surface machinery is not a prerequisite.
