# 2026-06-01 — Clipboard module

## Work done

New `modules/clipboard/` — the OS clipboard interface across every platform. Gabbo chose the
ambitious path on each design fork: async-throughout, a multi-representation transferable over an
open format-ID space with sequence tracking, and all tractable platforms implemented now.

- **Core** (`src/clipboard.c`, `include/clipboard/clipboard.h`, `include/clipboard/backend.h`):
  - Transferable data model — `Mel_Clip_Transferable` (items) → `Mel_Clip_Item` (reps) →
    `Mel_Clip_Rep { format, bytes }`, all dynamic and allocator-fed.
  - Open `u32` format space with well-known constants (TEXT/HTML/PNG/URI_LIST/RTF) mapping to
    canonical MIME, plus `mel_clip_format_register` for custom MIME (dedup, stable). No enums.
  - Severity-plus-bitset status (`Ok|Warned|Error` + result/warn bits), mirroring vibration.
  - Async job machinery: each op heap-allocates a job, copies the caller payload, indexes it in a
    generation-checked slotmap. Resolution arms a one-shot `0`-interval reactor timer on the job's
    reactor, so completion lands on the **next** turn — non-re-entrant even for synchronous
    backends. The Web path stashes the slotmap token (split into two u32 across the JS boundary) and
    recovers the job via `mel_clip__job_from_token`, generation-guarded against UAF after shutdown.
  - `sequence()` poll + `watch` (a 250 ms reactor timer comparing the sequence; only runs while
    watching). Shutdown cancels pending jobs and frees all storage.
- **Platform layer** — direct linked `mel_clip__plat_*` functions, one translation unit per
  platform, build-time selected (the `power` precedent). An initial draft used a registered
  function-pointer vtable; that was wrong — the backend is statically known at compile time, so the
  indirection bought nothing (it is the *vibration* pattern, justified there only by multiple
  runtime providers). Removed in favor of direct calls. The core calls `mel_clip__plat_read` etc.;
  the linker resolves the single implementation.
  - **apple** (`src/apple/clipboard_apple.m`) — `NSPasteboard` (macOS) + `UIPasteboard` (iOS), one
    MIME⇄UTI table, multi-item/multi-rep write via `writeObjects:` / `.items`. **Verified**: builds
    on macOS and iOS; macOS core test green.
  - **win32** (`src/win32/clipboard_win32.c`) — user32 clipboard, UTF-8⇄UTF-16 transcode for text,
    raw bytes for PNG/custom via `RegisterClipboardFormatA`, `GetClipboardSequenceNumber`.
  - **android** (`src/android/clipboard_android.c`) — `ClipboardManager` via the `platform` JNI
    bridge; text + html, allocator-fed string conversion. Builds with the NDK toolchain.
  - **web** (`src/web/clipboard_web.c`) — `navigator.clipboard` async via `EM_JS` +
    `EMSCRIPTEN_KEEPALIVE` resolvers. Builds under emscripten.
  - **host-none** (`src/clipboard_host_none.c`) — Linux/fallback stub: `available()` false.
- **Test** (`test/test.clipboard.c`) — 7 hermetic cases: format registry mime/dedup, write→read
  text roundtrip, sequence advance, empty/no-backend status, transferable build/free. The test
  *is* the platform layer: it links its own `mel_clip__plat_*` (an in-memory fake) against the core
  with `reactor=NULL` (inline resolution), so it touches neither the real system clipboard nor a
  pumped reactor. The test target compiles `clipboard.c` directly rather than linking the library,
  to avoid duplicating the host backend's `mel_clip__plat_*` symbols. **7/7 pass.**
- Spec authored in `design/clipboard.md`, iterated, then relocated to `modules/clipboard/spec.md`
  (MEL-SPEC-002) corrected to match the implementation; `design/clipboard.md` removed. `readme.md`
  and `todo.md` added.

Build matrix: macOS ✅ (+tests), iOS ✅, wasm ✅, android ✅, linux ✅. Win32 ⚠️ — see kludges.

## Kludges (MEL-ENGINE-VIII — full confession)

- **Win32 unverified.** The win32 backend never compiled here: this environment has no Windows SDK
  (`windows.h` not found). Confirmed environmental — the existing `power`/`window` win32 sources
  fail identically. The code is written to the documented API but is unproven; treat as a draft
  until built on a host with the SDK.
- **Android / Web written but not run-verified.** Both compile (NDK, emscripten) but were not
  exercised on a device / in a browser. JNI method signatures and the `EM_JS`/`_malloc`/`stringToUTF8`
  bridge are written from the documented APIs, not observed running.
- **Win32 HTML dropped.** `text/html` needs the `CF_HTML` byte-offset wrapper; rather than ship
  untested offset arithmetic that would produce malformed clipboard HTML, the win32 backend drops
  HTML with `RepresentationDropped` + a warning. Honest degradation, not a fake. (todo.md)
- **Android has no sequence/watch.** No public clipboard sequence counter; `sequence()` returns 0
  and `watch` logs unsupported rather than synthesizing one (a `OnPrimaryClipChangedListener` on a
  Looper thread is the real path). Honest, but a capability gap.
- **Web is text-only.** `read`/`write`/`clear` cover text via `readText`/`writeText`; rich
  `ClipboardItem` (html/png) and `query` enumeration are stubbed (`query` logs unsupported, non-text
  write reps dropped). The 95% path works; the rest is flagged.
- **clang-format mangled the `EM_JS` JavaScript.** It parsed the JS body as C and turned `!==` into
  `!= =`, breaking the web build. Fixed by rewriting that line to use `!=` (and the EM_JS bodies now
  avoid strict-equality operators so a future format run won't re-break them). Worth knowing: never
  trust clang-format on `EM_JS`/`EM_ASM` bodies.
- **`mel_clip_native()` is macros-only meaningful on Apple.** Win32/Android/Web return NULL for the
  native escape hatch (no single stable native object to hand out); only Apple returns the
  pasteboard. Not wrong, but asymmetric.
- **Single result item on read.** The read result is modelled as one item with multiple
  representations; multi-item read (rare) collapses to the first. Multi-item *write* is honored on
  Apple, collapsed-with-warning elsewhere.

## CLAUDE.md suggestions (recommendations only — not applied)

- The repo CLAUDE.md says invoke `./nob` from the repo root, but a fresh worktree has no `nob`
  binary (it is a build artifact). Bootstrapping is `clang -std=c23 -g -Imodules/build -o nob nob.c`
  (the recipe lives in `nob.c`'s `NOB_REBUILD_URSELF`). Worth a one-line "Bootstrapping" note in the
  Build section, since agents working in worktrees will hit this.
- Consider noting in the coding guidelines that `EM_JS`/`EM_ASM` bodies must be excluded from
  clang-format (a `// clang-format off` block, or a `.clang-format` `RawStringFormats`-style carve
  out), since the formatter corrupts embedded JavaScript.

## Suggestions

- **Feature direction.** The clipboard wants the `window` integration for Linux (selection serving
  is event-loop-bound) and for a proper Win32 owner `HWND`. When the window module grows a
  "borrow the native event loop" hook, both Linux X11/Wayland and a richer Win32 path drop in
  behind the existing backend vtable with no core change — the async contract already accommodates
  the selection round-trip.
- **Repo hygiene — reactor.** `modules/reactor/src/macos/event_pump.m` references `_NSApp`
  (AppKit) but `reactor/build.c` links only Foundation/CoreFoundation. Every macOS executable that
  links reactor must therefore supply `-framework AppKit` itself (the `window`/`app` modules do via
  Cocoa; the clipboard-core test had to add it explicitly). The reactor should link AppKit on macOS
  so the coupling is not pushed onto every consumer.
- **Repo hygiene.** Several modules' JNI helpers (`midi`, `paint`) convert strings through fixed
  `char buf[1024]` stacks — a latent MEL-CODE-002 violation and a truncation bug for long strings.
  The clipboard JNI path uses allocator-fed conversion instead; a shared
  `platform/android` str8⇄jstring helper (allocator-fed) would let those modules shed the fixed
  buffers. Small, mechanical, removes real debt.
- A `hello-clipboard` demo (GUI button → write text, button → read-and-show) would exercise the
  reactor delivery path end-to-end on macOS, which the hermetic test deliberately bypasses.
