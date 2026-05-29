# display

Per-system enumeration of physical displays: pixel geometry, refresh and VRR
envelope, HDR / wide-color capability, color profile, and occlusion / power
state. Implements the spec in `spec.md` (this folder).

Displays are a property of the *system*, not of the GPU adapter. This module
mints the handles; the GPU adapter (when it exists) will report which subset it
can drive. `display` is therefore a **producer** — every coupling the spec names
(`adapter_displays`, `swapchain_create{ target_display }`, surface migration) is
a downstream consumer that imports this module's handle, never a dependency of
it. The module builds and runs standalone today.

## Public surface

`<display/display.h>`

- `Mel_Display` — typed value handle over `Mel_SlotMap_Handle`. Trivially
  copyable, threadable. Compared by `mel_display_equal`. Identity persists across
  configuration changes (mode switch, HDR toggle); the generation rolls on
  hot-unplug.
- `Mel_Display_Descriptor` — by-value snapshot from `mel_display_describe`
  (`{ value, status }`). Optional fields carry a `has_*` flag (no `Option` type
  in this codebase). The variable-length `refresh_modes[]` and
  `supported_color_spaces[]` are fixed-capacity inline arrays so the snapshot
  stays self-contained; the ICC blob is registry-owned and valid while the
  handle is alive.
- `Mel_Color_Space`, `Mel_Color_Icc_Profile` — the spec parks the color-space
  enumeration here; it is consumed identically by GPU swapchain configuration and
  media pipelines once those exist. One enum, one meaning.
- Event types — `Mel_Display_Event` with a kind (`added` / `removed` /
  `configuration_changed` / `power_state_changed`) and a changed-field bitset.
- `mel_display_native_handle` — the P2 escape: the tagged-union platform object
  (`NSScreen*` today). Invalidates to a `Lost`-tagged variant on removal.

### Lifecycle

    mel_display_init(alloc);              // alloc NULL -> heap; idempotent; runs a refresh
    u32 n = mel_display_count();
    Mel_Display ds[16];
    u32 listed = mel_display_list(ds, 16);
    Mel_Display_Describe_Result r = mel_display_describe(ds[0]);
    mel_display_refresh();                // re-enumerate + diff -> events
    Mel_Display_Event ev[64];
    u32 got = mel_display_poll_events(ev, 64);
    mel_display_shutdown();

## Architecture

`src/display.c` is a portable registry/diff core: it owns a slotmap of displays,
keys entries by a backend-supplied stable id, and on `refresh()` diffs against the
live set — preserving handles for survivors, rolling generations for the departed,
and emitting events with a per-field changed-mask (collapsing to
`power_state_changed` when only `state` moved). The per-platform reading lives
behind one seam, `mel_display__enumerate` (`src/display_backend.h`); a new
platform implements only that.

Each platform implements that one seam in its own axis dir, selected by the
build's platform/runtime chain — `src/macos/` (`NSScreen` + Core Graphics),
`src/ios/` (`UIScreen`), `src/win32/` (`IDXGIOutput6`), `src/linux/` (XRandR),
`src/android/` (`DisplayManager` over JNI), `src/emscripten/` (browser `screen.*`),
and a `src/wasi/` no-display stub. macOS is runtime-verified on hardware; the rest
are compile-verified against their SDKs (see `todo.md` for per-platform gaps).

## Honesty contracts

- Backends that cannot report a field omit it rather than synthesizing one
  (P1, MEL-ENGINE-VIII). On macOS, absolute panel luminance in nits is not
  published, so `hdr.has_luminance` is false — never a fabricated figure. The EDR
  triplet is reported as Apple's component-value ratios, not nits (see the field
  naming in `display.h` and the open item in `todo.md`).
- Use-after-removal is loud-not-fatal: `mel_display_describe` logs and returns
  `INVALID_HANDLE`; `mel_display_native_handle` returns the `Lost` variant. (Today
  `mel_assert` is a no-op stub, so "loud" means log + status, not crash.)

## Verification

- `modules/display/test/display_test.c` — platform-agnostic contract tests
  (dead / null / equal). Run: `./nob test macos -- --filter display`.
- `apps/display-gui` — live display inspector (the fork-per-test harness cannot
  exercise Cocoa — AppKit aborts in the fork child — so real macOS enumeration is
  verified here). A 250 ms reactor tick re-enumerates and re-paints a canvas with
  every decoded descriptor field for every display, plus an event log fed by
  `mel_display_poll_events` (watch `config_changed`/`added`/`removed` as you change
  resolution or hot-plug). A "Toggle EDR headroom" button puts an EDR-requesting
  `CAMetalLayer` on screen so `hdr.edr_max_now` rises above 1.0 on demand. Run:
  `./nob run display-gui macos`.

Remaining work is tracked in `todo.md`.
