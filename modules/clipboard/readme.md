# clipboard

The interface to the operating system's clipboard across every platform. One logical payload — a
*transferable* — carries many representations at once (UTF-8 text, HTML, PNG, a file/URI list, plus
any custom MIME type), so the receiving application picks the best.

Every operation is reactor-driven with a completion: `read`, `write`, `query`, `clear` each take a
reactor and a callback, and the result arrives on that reactor, never re-entrantly inside the
request call. The clipboard is synchronous and instant on Apple/Win32/Android, a permission-gated
promise on the Web, and a selection round-trip on Linux — one contract spans them.

```c
mel_clip_init(alloc, reactor);
mel_clip_write_text(S8("hello"), on_done);
mel_clip_read_text(on_text);                 // on_text(str8, status, user) — valid only in-callback
```

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
- Linux — stub (X11/Wayland selection serving needs the `window` event-loop integration).

Spec: `spec.md`. Todo: `todo.md`. Dependencies: `core`, `allocator`, `collection`, `string`,
`reactor`, `log`, `platform`.
