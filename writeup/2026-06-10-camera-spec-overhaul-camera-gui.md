# Camera spec overhaul + camera-gui test bench

## Work done

- **Reviewed and overhauled `design/camera.md`** (the never-implemented camera
  rewrite, written against the deleted `reactor` module). Kept the sound core:
  provider+stable_id identity, const-singleton classifications, visitor
  enumeration, intrusive subs, caller-owned `Mel_Future` storage, push-only
  frames. Rewrote what rotted:
  - Threading section now speaks vat: `Mel_Vat_Source` (wakeables/deadline/
    drain/cancel), `MEL_VAT_WAKE_IN`, per-backend waiter gating (V4L2 on the
    epoll waiter, MF re-specced as an IOCP-native completion source, web on the
    guest vat), the cocoa-ui-waiter-refuses-fd constraint recorded.
  - Invariant 1 amended: `mel_vat_source_open` allocates the source node from
    the vat's allocator — named as the one sanctioned allocation.
  - New "home vat" init (`mel_camera_init(Mel_Vat*)`); owner-thread confinement
    asserted via `mel_vat_is_owner` (resolves old open decision #4).
  - Future consumption discipline stated (then-continuation on
    `mel_vat_executor`, or `mel_await_future`); `authorize` bool return dropped.
  - Hotplug subs gained a required executor (against the old design's
    inline-only delivery; `mel_executor_inline` is the explicit opt-in).
  - Old open decision #1 resolved: per-device+config `footprint` query, caller
    provides stream storage; provider desc publishes its session size, so
    third-party providers fit (no compile-time max).
  - Old open decision #2 resolved: `cfg.buffers` explicit (MEL-CODE-007).
  - Migration ledger added: shipped-module symbol map, per-backend port notes
    (avf/camera2/v4l2/mf/web), consumers `camera-scanner` + `barcode-reader`.
- **New app `apps/camera-gui`** ("Camera Bench"): GUI exerciser for the current
  camera module. Sidebar of groupboxes — Authorization (status + Authorize),
  Devices (Refresh + per-device buttons, dynamic create/destroy), Modes
  (per-mode buttons from describe), Stream (Open/Start/Stop/Close + mirror
  checkbox), Stats (frames, measured fps, last seq/extent/format), Hotplug
  (4-line rolling log) — and a letterboxed live-preview canvas. Frame path is
  camera-scanner's proven triple-buffer + `mel_vat_post` scheme. Smoke-verified
  on macos: builds, packages, launches, enumerates one device, receives the
  initial hotplug event, survives, exits clean. Authorize/open/preview need an
  attended run (TCC prompt).

## Bugs found while verifying

- **AVF backend never enumerated modes (fixed here):** `avf_enumerate` set
  `modes = NULL, mode_count = 0` since the backend's first commit, so
  `mel_camera_describe` reported zero modes on macOS and both camera-scanner
  and barcode-reader bailed with "describe failed or has no modes" — the
  macOS capture path was dead; the apps had only ever been verified on
  android/ios. Fixed in `camera_avf.m`: modes are collected per device from
  `dev.formats` (fourcc→`mel_image_format` via the existing map, dimensions,
  fps ranges merged per format+extent) and interned like device names,
  cleared per enumerate and on shutdown. Verified: camera-scanner and
  barcode-reader now describe, open, and stream on macos.

- **Hotplug feedback storm (app-side, fixed):** the hotplug handler called
  `rebuild_devices()` which called `mel_camera_refresh()`, and refresh emits a
  `changed` event per device even when nothing changed → infinite event loop
  (~3500 events in 3 s). App now rebuilds without refresh, and only on
  added/removed.
- **`mel_camera_refresh` emits spurious `changed` events** (module): every
  refresh reports `changed [unknown]` for an unchanged device. Diff looks
  unconditioned. Worth a look in `modules/camera/src/camera.c`.
- **Zero-size drawRect asserts** (gui/paint): AppKit delivers `drawRect:`
  before layout sizes the view; `mel_drawable_borrow` asserts `w > 0 && h > 0`
  and the app dies. The cocoa canvas backend should skip zero-size paints.
  Worked around in the app with initial bounds + preferred size.
- **Assert handler segfaults while reporting** (debug): the failed assert
  above crashed inside `mel_assert_default_handler` →
  `mel_stacktrace_format` → `str8_fmt_alloc` → `vsnprintf` on a garbage
  pointer (EXC_BAD_ACCESS). The handler ate the real diagnostic; only the
  crash report revealed the true assert. MEL-ENGINE-VIII failure in the
  failure path itself.

## Kludges

- `camera-gui` reads open/start/stop future status inline immediately after
  the call — idiomatic for the *current* module (documented inline resolve;
  camera-scanner does the same) but it is exactly the consumption pattern the
  overhauled spec eliminates. Migrates with the ledger.
- Hotplug log is 4 fixed 128-byte lines (display window, oldest dropped). Not
  a growable collection, but noting it against MEL-CODE-002's spirit.
- The smoke test only proves launch/enumerate/hotplug/paint; no unattended way
  to pass the TCC camera prompt, so capture itself is unverified here.

## CLAUDE.md suggestions (recommendations only)

- None for the root file. If module-bug triage gets routine, a `todo.md`
  convention pointer ("file module bugs in `<module>/todo.md`") could anchor
  findings like the ones above.

## Suggestions

- Fix the three module bugs above; the assert-handler one first (it masks
  every other debug assert on macos gui apps).
- `mel_camera_refresh` could return a change summary so UIs can avoid
  re-describing every device.
- Log tag column truncates `camera-gui` to `camera-` — wider tag field or
  explicit truncation marker.
- When the camera rewrite lands, `camera-gui` is the migration harness: port
  it first, then `camera-scanner`/`barcode-reader`.
