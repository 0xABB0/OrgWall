# clipboard

The interface to the operating system's clipboard across every platform. One logical payload — a
*transferable* — carries many representations at once (UTF-8 text, HTML, PNG, a file/URI list, plus
any custom MIME type), so the receiving application picks the best.

Every operation returns a `Mel_Future`: `read`, `write`, `query`, `clear`, `read_text`,
`write_text` each return `Mel_Future*`, resolved on the caller's executor (default: derived from the
init reactor; the inline executor when init took no reactor). The clipboard is synchronous and
instant on Apple/Win32/Android, a permission-gated promise on the Web, and a selection round-trip on
Linux — one contract spans them. Completion, deferral, and cancellation are the `future` module's:
the clipboard owns no per-op timer and no bespoke deliver path.

```c
mel_clip_init(alloc, reactor);
mel_clip_future_free(mel_clip_write_text(S8("hello")));

Mel_Future* f = mel_clip_read_text();
mel_future_then(f, &my_task, my_exec);       // my_task reads mel_clip_future_text(f), then frees it
```

The future's value is read by op-specific accessors — `mel_clip_future_text`,
`mel_clip_future_transferable`, `mel_clip_future_formats`, `mel_clip_future_status` — borrowed and
**valid until `mel_clip_future_free(f)`**, which releases the result and the job. `watch` returns a
`Mel_Event` channel of `u64` change sequences; subscribe push (delivered on your executor) or pull.

Two channels exist: `MEL_CLIP_CHANNEL_CLIPBOARD` (copy/paste) and `MEL_CLIP_CHANNEL_PRIMARY` (the
X11/Wayland middle-click selection); pass `.channel=` to any op (default CLIPBOARD). `mel_clip_has`
returns a `bool` future reporting presence without transferring. `mel_clip_channel_supported` and
`mel_clip_sequence_ch` are channel-scoped.

A format is a `u32` id over an open space with well-known constants (`MEL_CLIP_FMT_TEXT`, `HTML`,
`PNG`, `URI_LIST`, `RTF`); consumers register custom MIME types via `mel_clip_format_register`. Each
backend translates the canonical MIME to its native identifier (UTType, a registered Win32 format,
the MIME verbatim). Status is a severity plus a warning/result bitset — no enums; every fidelity
loss (dropped representation, transcoding, denied permission) is named in the status and the log.

Backends (one compiles per platform):
- macOS / iOS — `NSPasteboard` / `UIPasteboard` (full: text, html, png, files, multi-item, sequence).
- Win32 — user32 clipboard (text transcoded UTF-8⇄UTF-16, png/custom raw; HTML pending the CF_HTML wrapper).
- Android — `ClipboardManager` via JNI (text, html; no sequence counter).
- Web — `navigator.clipboard` async (text; rich `ClipboardItem` and enumeration pending).
- Linux — X11 selections (libxcb, `dlopen`'d) over the reactor: owns CLIPBOARD + PRIMARY, serves SelectionRequest; Wayland connection fallback (cross-client serve is a todo).

Spec: `spec.md`. Todo: `todo.md`. Dependencies: `core`, `allocator`, `collection`, `string`,
`executor`, `future`, `event`, `reactor`, `log`, `platform`.
