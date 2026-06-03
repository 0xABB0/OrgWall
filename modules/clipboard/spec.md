# Clipboard — Spec

The single interface to the operating system's clipboard across every platform. One logical
payload — a *transferable* — carries many representations at once (UTF-8 text, HTML, PNG, a
file/URI list, plus any custom MIME type), so the receiving application picks the best. Every
operation returns a `Mel_Future` resolved on a target executor (MEL-ENGINE-III: the module owns no
thread; deferral and delivery ride the async substrate, not bespoke machinery). Not a serialization
format, not a drag-and-drop framework, not a clipboard-history manager.

## Model

- **Future-returning** — `read`, `write`, `query`, `clear`, `read_text`, `write_text` each return a
  `Mel_Future*`. The system clipboard is synchronous and instant on Apple/Win32/Android, a
  permission-gated promise on the Web, a selection-ownership round-trip on Linux/X11. One contract
  spans all: the result resolves the future, which delivers its continuation on the caller's
  executor (§6).
- **Substrate-built** — completion, deferral, cancellation, and cross-executor delivery are the
  `future` module's, not the clipboard's. There is no per-op timer, no slotmap-job completion
  lifecycle, no hand-rolled deliver-on-reactor path; the job embeds a `Mel_Future` and resolving it
  is the whole of completion (MEL-ENGINE-IX).
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
  registry, the lowering, and the future resolution; the platform layer only marshals to/from the
  OS (§5).

## 1. Lifecycle

```c
void mel_clip_init(const Mel_Alloc* alloc, Mel_Reactor* reactor);
void mel_clip_shutdown(void);
bool mel_clip_available(void);   // false where no host clipboard (Linux stub, headless web)
```

One process-global instance. The default target executor is derived from the init reactor
(`mel_reactor_executor`, an always-next-turn defer); a per-op override (§6.1) targets another
executor. A NULL init reactor makes the default executor the inline executor — completion delivers
through the inline trampoline (synchronous, non-re-entrant), used by hermetic unit tests; production
passes a reactor.

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
allocator), `mel_clip_job_alloc`, `mel_clip_job_add_warning`, and `mel_clip_job_resolve`
(resolves the embedded `Mel_Future`). A backend that completes asynchronously stashes
`mel_clip_job_token(job)` and recovers the job later via `mel_clip__job_from_token`
(generation-checked, NULL if the job was cancelled or shut down) — the Web path; `on_write`/`on_text`
`EM_JS` resolve callbacks recover the job by token and resolve its future.

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
typedef struct { const Mel_Clip_Format* items; u32 count; } Mel_Clip_Formats;
typedef struct { Mel_Executor* exec; const Mel_Alloc* alloc; } Mel_Clip_Opt;

Mel_Future* mel_clip_read(fmts, n, .exec=, .alloc=)   // n == 0 ⇒ every available format
Mel_Future* mel_clip_write(t, ...)   mel_clip_query(...)   mel_clip_clear(...)
Mel_Future* mel_clip_read_text(...)  mel_clip_write_text(text, ...)
```

Each macro forwards to a `_opt` function (the varargs-options idiom). `o.alloc` (default: the init
allocator) owns the result storage; `o.exec` (default: the executor derived from the init reactor,
or the inline executor when init took no reactor) is the future's target executor. The continuation
a consumer registers with `mel_future_then(f, cont, exec)` runs on the executor it names; the
clipboard's `o.exec` is only the module's default for the resolve path's deferral.

The future's value is read by op-specific accessors, valid once resolved:

```c
Mel_Clip_Status              mel_clip_future_status(f);        // CANCELLED ⇒ ERROR | RESULT_CANCELLED
const Mel_Clip_Transferable* mel_clip_future_transferable(f); // read
str8                         mel_clip_future_text(f);          // read_text
Mel_Clip_Formats             mel_clip_future_formats(f);       // query
void                         mel_clip_future_free(f);          // releases the result + the job
```

`write`/`write_text`/`clear` carry no value; their outcome is `mel_clip_future_status`.

### 6.2 Result lifetime

The future **owns** its result; the accessors return a borrowed view into module storage. The view
is **valid until the consumer calls `mel_clip_future_free(f)`** — which releases the result storage,
the slotmap entry, and the job. The consumer frees from its continuation (the substrate analog of
the old "free after the callback returns"): read the value, copy what it keeps, then free.
`o.alloc` lets the consumer place the result storage (e.g. a scratch arena). Freeing is mandatory —
the future is heap-backed; not freeing leaks the job.

### 6.3 Delivery — non-re-entrant by construction

Each operation allocates a heap *job* embedding a `Mel_Future`, copies any caller payload into
module storage, and indexes the job in a generation-checked slotmap (the token registry for async
recovery and the shutdown sweep). When the backend resolves — inline on synchronous platforms, from
a JS promise on Web — `mel_clip_job_resolve` builds the op's result view and calls
`mel_future_resolve`. Delivery to the registered continuation is the future+executor's job: the
reactor executor always defers to the next turn; the inline executor trampolines through a
thread-local drain. Either way the continuation never runs re-entrantly mid-resolve
(MEL-ENGINE-VIII — predictable). A redundant resolve coalesces (`resolved` guard, plus the future's
one-shot CAS).

### 6.4 Cancellation

`mel_clip_shutdown` tears down watch, then for every in-flight job: cancels its future (a registered
continuation is woken `RESULT_CANCELLED`, so no completion silently vanishes); a job with no
registered continuation — nobody to free it — is freed by shutdown itself. Then it frees the format
registry and the slotmap. A continuation whose target executor is never pumped after shutdown is the
consumer's responsibility (the substrate's structured-scope contract), as with any future.

## 7. Change tracking

```c
u64        mel_clip_sequence(void);            // monotonic counter; 0 ⇒ unsupported
Mel_Event* mel_clip_watch(.exec=, .alloc=);    // change channel; NULL ⇒ unsupported backend
void       mel_clip_unwatch(void);
```

`sequence` is the cheap synchronous poll where the OS exposes one (`NSPasteboard.changeCount`,
`UIPasteboard.changeCount`, `GetClipboardSequenceNumber`). `watch` returns a `Mel_Event` channel
carrying `u64` change sequences (loss policy `latest`); a low-frequency reactor timer (250 ms, a
legit reactor source) compares `sequence` and fires the new sequence into the channel on a move —
cost only while watching (MEL-ENGINE-III). Consumers `mel_event_subscribe_push(ev, exec, cb, user)`
(delivered on their executor) or `mel_event_subscribe_pull(ev, user)` (drained with
`mel_event_pull`). With no init reactor the channel exists but the timer is absent (nothing drives
it). Where `sequence()` is 0 (Android, Web), `watch` logs unsupported and returns NULL (honest,
MEL-ENGINE-VIII). `unwatch` destroys the timer and the channel.

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
  (§2); the op's result kind is a per-job `build_view` behavior (a function pointer), not a tag.
- **No fixed arrays** (MEL-CODE-002): reps, items, the format registry, the request/result lists are
  dynamic and allocator-fed. The per-backend MIME⇄native tables are `const` translation literals
  (protocol data), not capacity caps.
- **Allocators** (MEL-CODE-003): `init`, every builder, and every result path thread a `Mel_Alloc*`.
- **No silent defaults** (MEL-CODE-007): an unrepresentable rep, a denied web write, a missing format
  each surface a status bit and a log line.

## 10. Dependencies

`core`, `allocator`, `collection` (array + slotmap + list for `mel_container_of`), `string`,
`executor`, `future`, `event`, `reactor`, `log`, `platform` (Android JNI / Win32). No `gpu`, no
`window` yet (Linux deferred).

## 11. Failure modes

- Web write/read without a user gesture or on an insecure origin ⇒ promise rejects ⇒ `Denied`,
  logged.
- Requested format absent ⇒ that rep omitted, `FormatUnavailable`; `Empty` if none present.
- Write rep unrepresentable (Android raw image, Win32 HTML) ⇒ dropped, `RepresentationDropped`.
- `shutdown` with jobs in flight ⇒ each future cancelled (`RESULT_CANCELLED`); a continuation-less
  job is freed by shutdown; no UAF.
- Reactor never pumped ⇒ the continuation never fires; cancelled at shutdown, the wake submitted to
  the dead executor is the consumer's to drain (substrate-scope contract).
- Linux / headless ⇒ `available()` false, every op `NoClipboard`; branch on `mel_clip_available()`.
- Win32 `OpenClipboard` contended ⇒ retry-bounded, then `Error`, logged.
