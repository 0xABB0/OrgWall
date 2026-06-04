# melody-showcase

A single binary that exercises every module built during the SDL-parity session,
runnable on the macOS host. Two modes share one process.

## Modes

- **Default (windowed, interactive)** — opens a window and runs the app/reactor loop.
  A `paint` canvas draws live panels; key commands trigger one-shot module actions.
- **`--smoke` (headless)** — exercises each listed module once in sequence without
  opening a window or blocking on input, prints one `module: <result-or-honest-absence>`
  line per module, then exits 0. This is what the gate runs.

Run:

    ./nob run melody-showcase            # windowed
    ./nob run melody-showcase -- --smoke # headless gate path
    ./apps/melody-showcase/build/macos-debug/melody-showcase --smoke

## Smoke lines (one per module)

- `cpu` — core/logical counts, L1/L2/L3 cache sizes, cache line, total RAM, detected SIMD tier, SIMD alignment.
- `platform` — OS name, sandbox bitset, app id, device-class bitset, tablet/TV predicates.
- `power` — power source and battery level/charging, or honest battery-absent with capability bits.
- `time` — monotonic clock seconds, UTC wall time, and the locale-driven date format string + order/clock bitsets.
- `locale` — preferred-language count, primary tag, and the ordered preferred list.
- `display` — connected monitor count.
- `input` — enumerated device count and the first device's name/caps/key/button counts (honest-absent if none).
- `gamepad` — connected joystick count + first device's gamepad-mapping/axes/buttons, or honest "none connected".
- `sensor` — accel/gyro sensor presence, or honest "none present" (macOS desktops have no built-in IMU).
- `hid` — enumerated HID device count and the first device's vid/pid/product.
- `dylib` — opens `libSystem.B.dylib`, resolves `malloc`, prints the resolved address.
- `io` — fixed memory stream write-then-read roundtrip with byte-match verification.
- `messagebox` — backend availability (the modal itself is suppressed in smoke; shown in windowed mode).
- `tray` — tray support + created status-item and live menu handle (menu mutation needs the app run loop; done in windowed mode).
- `vibration` — haptic device presence (honest-absent when no device).
- `debug` — installs a guarded assert handler, fires a failing `mel_assert`, confirms it is caught/handled (no crash).
- `fs` — writes a temp file under the system temp dir and reads it back (async via the fs proactor).
- `storage` — writes then reads a relative key inside an fs-backed storage root.
- `process` — runs `/bin/echo` via the async runner and captures its stdout + exit code.
- `dialog` — backend availability (open-file picker suppressed in smoke; requested in windowed mode).
- `shell` — backend availability (URL open suppressed in smoke; requested in windowed mode).
- `clipboard` — writes text then reads it back, prints the round-tripped text + change sequence.
- `app` — the reactor loop is driven to completion by the smoke continuation chain.

Honest absence is surfaced, never skipped or faked: gamepad, sensor, and vibration
report their absence on a host with no such device.

## Windowed panels (live, refreshed ~200 ms)

cpu / platform / power / time / locale / display readouts; live `input` mouse position,
buttons, and last key; `gamepad`, `sensor`, `hid` live counts; tray on/off, vibration
device count, clipboard and process availability. An action log shows the most recent
key-command results and `app` lifecycle phases as they fire.

## Key commands (windowed)

- `O` — dialog: open-file picker
- `M` — messagebox: modal alert
- `T` — tray: toggle tray icon + menu (add icon, separator, checkbox item)
- `C` — clipboard: copy text (read-back is async)
- `U` — shell: open a URL
- `F` — fs + io + storage: growable stream probe
- `L` — dylib: open libSystem + resolve a symbol
- `P` — process: spawn `/bin/echo`, capture stdout asynchronously
- `H` — hid: re-enumerate devices
- `V` — vibration: play a short pattern if a device exists
- `D` — debug: fire a guarded demo assertion that is caught/handled
- `Q` — quit

## Build

`build.c` declares the `melody-showcase` executable under the `gui` subsystem and
`mel_depends` on every module it calls. It builds on the macOS host; no platform-specific
sources are required because every dependency degrades honestly on the host.
