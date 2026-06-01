# Clipboard — Spec

The single interface to the operating system's clipboard across every platform. One logical
payload — a *transferable* — carries many representations at once (UTF-8 text, HTML, PNG, a
file/URI list, plus any custom MIME type), so the receiving application picks the best. Every
operation is reactor-driven with a completion (MEL-ENGINE-III: the module owns no thread; all
timing rides the consumer's reactor). Not a serialization format, not a drag-and-drop framework,
not a clipboard-history manager.

## Model

- **Async throughout** — `read`, `write`, `query`, `clear` each take a reactor and a completion.
  The system clipboard is synchronous and instant on Apple/Win32/Android, a permission-gated
  promise on the Web, a selection-ownership round-trip on Linux/X11. One contract spans all: the
  result arrives on the consumer's reactor, never re-entrantly inside the request call (§6.3).
- **Transferable** — the payload is a list of *items*, each item a list of *representations*
  `{ format, bytes }` (§3). The common case is one item with one text representation; the model
  does not special-case it.
- **Open format space** — a format is a `u32` id over an open numeric space with well-known
  constants, each mapping to a canonical MIME string; consumers register custom MIME types and
  receive stable ids (§4). Not an enum (MEL-CODE-001, MEL-ENGINE-IV).
- **Platform layer, not a vtable** — exactly one host clipboard per platform, chosen at build time
  by which source compiles (the `power` precedent). There is no runtime indirection: the platform
  translation unit *defines* a fixed set of `mel_clip__plat_*` functions and the core *calls* them
  directly, the linker resolving the one implementation. The core owns the data model, the format
  registry, the lowering, and the completion delivery; the platform layer only marshals to/from the
  OS (§5).

## 1. Lifecycle

```c
void mel_clip_init(const Mel_Alloc* alloc, Mel_Reactor* reactor);
void mel_clip_shutdown(void);
bool mel_clip_available(void);   // false where no host clipboard (Linux stub, headless web)
```

One process-global instance. The init reactor is the default completion reactor; a per-call
override (§6.1) resumes completion elsewhere. `mel_clip_init` registers the compiled-in backend via
`mel_clip__backend_init` (§5.3). A NULL init reactor (and no per-call override) makes completion
fire inline at resolve time — used by hermetic unit tests; production passes a reactor.

## 2. Status

`Mel_Clip_Status` is a `u32`: a 2-bit severity (`Ok | Warned | Error`, mask `0x3`) plus a
result/warning bitset — **not an enum** (MEL-CODE-001). Branch-free `mel_clip_failed` /
`mel_clip_warned`. The human-readable cause goes to `mel_log_error("clipboard", …)` at the failure
site (MEL-ENGINE-VIII).

Result bits (`<<2 .. <<6`) — terminal outcomes: `Denied` (permission / no user gesture on Web),
`NoClipboard` (no backend or display), `Empty` (nothing for the requested formats), `Cancelled`
(aborted, or shut down before completion), `Stale` (clipboard changed between request and serve).

Warning bits (`<<8 ..`) — the degradation channel, every loss named: `FormatUnavailable`
(requested representation absent from the result), `RepresentationDropped` (a write rep the platform
could not carry), `Transcoded` (re-encoded — UTF-8 ⇄ UTF-16 on Win32), `Truncated` (exceeded a
platform limit).

## 3. Transferable — data model

```c
typedef struct { Mel_Clip_Format format; str8 bytes; }      Mel_Clip_Rep;   // text reps are UTF-8
typedef struct { Mel_Array(Mel_Clip_Rep) reps; }            Mel_Clip_Item;
typedef struct { Mel_Array(Mel_Clip_Item) items; const Mel_Alloc* alloc; } Mel_Clip_Transferable;

void           mel_clip_transferable_init(Mel_Clip_Transferable* t, const Mel_Alloc* a);
void           mel_clip_transferable_free(Mel_Clip_Transferable* t);
Mel_Clip_Item* mel_clip_item_add(Mel_Clip_Transferable* t);
void           mel_clip_rep_add(Mel_Clip_Item* it, Mel_Clip_Format f, str8 bytes, const Mel_Alloc* a);
```

`str8` is the byte container throughout; a representation's bytes are an opaque span. `rep_add`
copies `bytes` into the item's reps allocator, so `write` (async) owns the payload until completion
(§6.3). `transferable_free` frees each rep's bytes through `it->reps.allocator`; pass the
transferable's own allocator to `rep_add`.

## 4. Format registry

A format is `Mel_Clip_Format` (`u32`; `0` is none), an open id space with well-known constants —
the vibration-primitive idiom, not an enum:

```
MEL_CLIP_FMT_TEXT=1 (text/plain;charset=utf-8)  HTML=2 (text/html)  PNG=3 (image/png)
URI_LIST=4 (text/uri-list)  RTF=5 (text/rtf)
```

```c
Mel_Clip_Format mel_clip_format_register(str8 mime);  // dedup by MIME; stable for the process
str8            mel_clip_format_mime(Mel_Clip_Format f);
```

The registry is a dynamic array seeded with the well-known set at init (MEL-CODE-002); registered
MIME strings are duplicated into module storage. The canonical interchange is the MIME string; each
backend owns a MIME ⇄ native translation (§8): UTType on Apple, a registered clipboard format on
Win32, the MIME verbatim on Android/Web. Custom MIME passes through unchanged where the platform
accepts arbitrary type strings (MEL-ENGINE-IV).

## 5. Platform layer

### 5.1 Interface

The platform translation unit implements these directly; the core links and calls them. No struct,
no registration — there is one clipboard per platform, selected by the build (`backend.h`):

```c
bool  mel_clip__plat_available(void);
void  mel_clip__plat_read(Mel_Clip_Job* job);    // requested formats; emit reps; resolve
void  mel_clip__plat_write(Mel_Clip_Job* job);    // lowered payload; resolve
void  mel_clip__plat_clear(Mel_Clip_Job* job);
void  mel_clip__plat_query(Mel_Clip_Job* job);    // emit available format ids; resolve
u64   mel_clip__plat_sequence(void);               // 0 ⇒ unsupported (§7)
void* mel_clip__plat_native(void);                 // §5.3
```

The host-none stub (Linux/fallback) defines `available == false` and resolves every op
`NoClipboard`. A unit test links its own `mel_clip__plat_*` against the core — an in-memory fake.

`Mel_Clip_Job` is opaque; the platform layer uses accessors: `mel_clip_job_request_count/request/wants`
(read/query inputs), `mel_clip_job_item_count/rep_count/rep` (write payload),
`mel_clip_job_emit` / `mel_clip_job_emit_format` (build results, bytes copied into the result
allocator), `mel_clip_job_alloc`, `mel_clip_job_add_warning`, and `mel_clip_job_resolve`. A backend
that completes asynchronously stashes `mel_clip_job_token(job)` and recovers the job later via
`mel_clip__job_from_token` (generation-checked, NULL if the job was cancelled) — the Web path.

### 5.2 Lowering lives in the core

The core copies the caller payload, then each backend marshals only the representations its OS
honors; unrepresentable reps are dropped with `RepresentationDropped`, the degradation policy in one
place (MEL-ENGINE-IX). Multiple items are honored natively where the platform supports them (Apple),
and collapse to the first item with a `RepresentationDropped` warning elsewhere.

### 5.3 Raw access

`mel_clip_native(void)` returns the backend's native object (the `NSPasteboard*` / `UIPasteboard*`)
for a consumer that bypasses the model.

## 6. Operations and completion

### 6.1 Surface

```c
typedef void (*Mel_Clip_On_Read)(const Mel_Clip_Transferable* t, Mel_Clip_Status s, void* user);
typedef void (*Mel_Clip_On_Text)(str8 text, Mel_Clip_Status s, void* user);
typedef void (*Mel_Clip_On_Write)(Mel_Clip_Status s, void* user);
typedef void (*Mel_Clip_On_Query)(const Mel_Clip_Format* fmts, u32 n, Mel_Clip_Status s, void* user);

typedef struct { Mel_Reactor* reactor; const Mel_Alloc* alloc; void* user; } Mel_Clip_Opt;

mel_clip_read(fmts, n, cb, .reactor=, .alloc=, .user=)   // n == 0 ⇒ every available format
mel_clip_write(t, cb, ...)   mel_clip_query(cb, ...)   mel_clip_clear(cb, ...)
mel_clip_read_text(cb, ...)  mel_clip_write_text(text, cb, ...)
```

Each macro forwards to a `_opt` function (the vibration varargs-options idiom). `o.alloc` (default:
the init allocator) owns the result storage; `o.reactor` (default: the init reactor) is where the
completion fires.

### 6.2 Result lifetime

The `Mel_Clip_Transferable*` and the `str8` handed to a read/text callback are **valid only for the
duration of the callback** — the core frees the result storage immediately after the callback
returns. A consumer that keeps the data copies it. (This is the deliberate contract; `o.alloc` lets
the consumer place the transient storage, e.g. a scratch arena.)

### 6.3 Delivery — non-re-entrant

Each operation allocates a heap *job*, copies any caller payload into module storage, and indexes
the job in a generation-checked slotmap. When the backend resolves — inline on synchronous
platforms, from a JS promise on Web — `mel_clip_job_resolve` arms a one-shot `0`-interval reactor
timer on the job's reactor. The timer fires on the **next** reactor turn (the readiness scan
precedes dispatch), delivers the consumer callback, then frees the job. Completion is therefore
never re-entrant within the request call, even when the backend completes synchronously
(MEL-ENGINE-VIII — predictable). A redundant resolve coalesces (`resolved` guard). With no reactor,
delivery is inline.

### 6.4 Cancellation

`mel_clip_shutdown` destroys any pending delivery timer, marks unresolved jobs `Cancelled`, delivers
each, and frees all module storage — no completion silently vanishes, no use-after-free of the
consumer's allocator past shutdown.

## 7. Change tracking

```c
u64  mel_clip_sequence(void);                       // monotonic counter; 0 ⇒ unsupported
void mel_clip_watch(cb, .reactor=, .user=);          // fire on every change
void mel_clip_unwatch(void);
```

`sequence` is the cheap synchronous poll where the OS exposes one (`NSPasteboard.changeCount`,
`UIPasteboard.changeCount`, `GetClipboardSequenceNumber`). `watch` arms a low-frequency reactor
timer (250 ms) that compares `sequence` and fires `cb` on a move — cost only while watching
(MEL-ENGINE-III). Where `sequence()` is 0 (Android, Web), `watch` logs unsupported and does nothing
(honest, MEL-ENGINE-VIII).

## 8. Platform lowering

- **macOS — `NSPasteboard`.** `generalPasteboard`; `changeCount` is the sequence; read via
  `dataForType:`, write via `NSPasteboardItem` + `writeObjects:` (multi-item, multi-rep), clear via
  `clearContents`, query via `types`. MIME ⇄ UTI: `text/plain` ⇄ `public.utf8-plain-text`,
  `text/html` ⇄ `public.html`, `image/png` ⇄ `public.png`, `text/uri-list` ⇄ `public.file-url`,
  `text/rtf` ⇄ `public.rtf`; an unknown UTI registers as a custom format (its UTI as MIME). Links
  `AppKit`, `Foundation`.
- **iOS — `UIPasteboard`.** `generalPasteboard`, `.changeCount`, `dataForPasteboardType:` /
  `setData:forPasteboardType:`, `.items` for multi-item write, `.pasteboardTypes` for query. Same
  UTI table. Links `UIKit`, `Foundation`.
- **Win32 — user32.** `OpenClipboard(NULL)` (retry-bounded) / `EmptyClipboard` / `SetClipboardData`
  / `GetClipboardData`; `GetClipboardSequenceNumber` is the sequence; `EnumClipboardFormats` for
  query. Text is `CF_UNICODETEXT` (UTF-16) ⇒ transcoded, flag `Transcoded`. PNG and custom MIME go
  through `RegisterClipboardFormatA` as raw bytes. HTML requires the `CF_HTML` byte-offset wrapper
  and is currently dropped with `RepresentationDropped` (todo).
- **Android — `ClipboardManager` (JNI).** Service `"clipboard"` via the `platform` bridge.
  `getPrimaryClip` / `setPrimaryClip`; `ClipData.newPlainText` / `newHtmlText`; `Item.coerceToText`;
  `getPrimaryClipDescription` for query; `clearPrimaryClip` (fallback: set an empty clip). Text and
  HTML are carried; raw image bytes need a `ContentProvider` ⇒ `RepresentationDropped`. No public
  sequence counter ⇒ `sequence == 0`, `watch` unsupported (a listener is the future path).
- **Web — `navigator.clipboard`.** Async, secure-context- and permission-gated. `writeText` /
  `readText` for text; promise rejection ⇒ `Denied`, never a fake success. `EM_JS` calls in,
  `EMSCRIPTEN_KEEPALIVE` resolves back via the split-token. No sequence ⇒ `watch` unsupported. Rich
  `ClipboardItem` read/write (`text/html`, `image/png`) and format enumeration are the future path;
  non-text write reps are dropped with `RepresentationDropped`, `query` logs unsupported.
- **Linux — X11/Wayland.** Stub: `available()` false, every op `NoClipboard`. The real backend owns
  the `CLIPBOARD`/`PRIMARY` selections and serves `SelectionRequest` from retained payload —
  coupled to the `window` event loop, so it lands once that hook exists.

## 9. Coding-guideline compliance

- **No enums** (MEL-CODE-001): formats are an open `u32` id space (§4); status is severity + bitset
  (§2); the job kind is the set callback, not a tag.
- **No fixed arrays** (MEL-CODE-002): reps, items, the format registry, the request/result lists are
  dynamic and allocator-fed. The per-backend MIME⇄native tables are `const` translation literals
  (protocol data), not capacity caps.
- **Allocators** (MEL-CODE-003): `init`, every builder, and every result path thread a `Mel_Alloc*`.
- **No silent defaults** (MEL-CODE-007): an unrepresentable rep, a denied web write, a missing format
  each surface a status bit and a log line.

## 10. Dependencies

`core`, `allocator`, `collection` (array + slotmap), `string`, `reactor`, `log`, `platform`
(Android JNI / Win32). No `gpu`, no `window` yet (Linux deferred).

## 11. Failure modes

- Web write/read without a user gesture or on an insecure origin ⇒ promise rejects ⇒ `Denied`,
  logged.
- Requested format absent ⇒ that rep omitted, `FormatUnavailable`; `Empty` if none present.
- Write rep unrepresentable (Android raw image, Win32 HTML) ⇒ dropped, `RepresentationDropped`.
- `shutdown` with jobs in flight ⇒ all resolve `Cancelled`; storage freed; no UAF.
- Reactor never pumped ⇒ completion never fires; resolved `Cancelled` at shutdown.
- Linux / headless ⇒ `available()` false, every op `NoClipboard`; branch on `mel_clip_available()`.
- Win32 `OpenClipboard` contended ⇒ retry-bounded, then `Error`, logged.
