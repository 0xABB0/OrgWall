# shell

Miscellaneous operating-system integration: handing a target off to the OS to do what it does best.
Two operations:

- `open_url(str8)` — launch the user's registered handler for an `http`/`https`/`file`/`mailto` URL
  or any platform-recognised scheme.
- `reveal_path(str8)` — surface a file or folder in the OS file manager, selected, the way Finder,
  Explorer, or a `FileManager1` browser do it.

Both return a `Mel_Future`, resolved on the caller's executor (default: derived from the init
reactor; the inline executor when init took no reactor). The OS launch is fire-and-forget on most
platforms; the future resolves as soon as the hand-off is accepted (Win32, Linux, Android, Web) or
when the platform's completion handler fires (the macOS `NSWorkspace` and iOS `UIApplication`
configuration-callbacks). Completion, deferral, and cancellation are the `future` module's; shell
owns no per-op timer and no bespoke deliver path. A generation-checked `Mel_Shell_Op` (optionally
reported via `.out_op`) cancels a still-pending hand-off through `mel_shell_cancel`.

```c
mel_shell_init(alloc, reactor);
mel_shell_future_free(mel_shell_open_url(S8("https://melody.example")));

Mel_Shell_Op op;
Mel_Future*  f = mel_shell_reveal_path(S8("/Users/me/notes.txt"), .out_op = &op);
mel_future_then(f, &my_task, my_exec);       // my_task reads mel_shell_future_status(f), then frees it
```

`mel_shell_future_status(f)` is the only value accessor — a severity (`OK`/`WARNED`/`ERROR`) plus a
result/warning bitset, no enums and no error strings. Every honest outcome is named:
`NO_BACKEND`, `NO_HANDLER`, `DENIED`, `NOT_FOUND`, `BAD_TARGET`, `SPAWN_FAIL`, `CANCELLED`, and the
`REVEAL_DEGRADED` warning when a platform can only open the containing folder rather than select the
item. The borrowed status is valid until `mel_shell_future_free(f)`, which releases the job.

Backends (one compiles per platform):
- macOS — `NSWorkspace openURL:configuration:completionHandler:` / `activateFileViewerSelectingURLs:`
  (full: async-verified launch, item-selecting reveal).
- iOS — `UIApplication openURL:options:completionHandler:` with `canOpenURL:` (full open_url; reveal
  is honestly absent — iOS exposes no file manager for arbitrary paths).
- Win32 — `ShellExecuteW` (full: `open` verb for URLs; `explorer.exe /select,` for reveal).
- Linux — `posix_spawnp("xdg-open")` for open_url; `org.freedesktop.FileManager1.ShowItems` over
  `dbus-send` for reveal, degrading to `xdg-open` on the parent directory (`REVEAL_DEGRADED`).
- Android — `Intent.ACTION_VIEW` via JNI with `FLAG_ACTIVITY_NEW_TASK` (open_url full; reveal opens a
  `resource/folder` view, `REVEAL_DEGRADED`).
- Web — `window.open(url, "_blank", "noopener")` (open_url; reveal is honestly absent — the browser
  sandbox has no file manager).

Spec: `spec.md`. Dependencies: `core`, `allocator`, `collection`, `string`, `executor`, `future`,
`reactor`, `log`; `platform` on Android only.
