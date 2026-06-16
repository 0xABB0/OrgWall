# OS-surface atlas — super-charter

The complete capability surface an operating system exposes to a userspace
application framework, decomposed into **domains**. One domain = one
`module-design` run (its own `design/<domain>/` tree, charter→freeze). This file
is the layer above every per-domain charter: it fixes *which* charters exist, the
axes they share, and the boundary they obey. It is **not** a module list — the
plane cut (P5, per domain) may split one domain into many modules or fuse
several. `camera` (D38) is the one fully-designed cell (`design/camera/`); every
other row is a stub naming a future run.

The domain bodies live in the per-band files (`10-substrate.md` … `20-…`); this
file is the index + the rules they all inherit.

## scope & method
- **Grain.** Domain → capability-areas → **sub-features** (selective depth). Each
  domain carries a 1-line def and its capability areas; the *load-bearing* areas
  (those whose sub-features classify differently per axis, or cluster toward a
  seam) are exploded one level into `·` sub-features so module seams become
  visible. The complete native surface per axis (every class / fn / ioctl /
  permission) is still that domain's P1 inventory — done later, per domain.
- **Boundary.** Baseline = everything an app can reach across the six platform
  axes. Per domain, two reaches extend the ceiling past the baseline and are
  **named, never omitted** (MEL-ENGINE-I):
  - `↑beyond` — hardware standards + pro-app features the OS hides or
    half-exposes (the P2 beyond-OS extension).
  - `↓under` — below-the-HAL or no-OS reaches (the camera precedent:
    `uvc-direct`, `genicam`, `embedded+baresensor`).
- **Axis-neutral.** Sub-features are capabilities, not APIs. Platform APIs appear
  only as parenthetical hints. A `?` suffix on a sub-feature = unverified, research
  scheduled (never a fabricated certainty).
- **Top-down.** Derived from the OS surface itself; the ~90 existing modules were
  deliberately *not* consulted. Reconciliation against them is downstream work,
  after the true ceiling is mapped.
- **Excluded by construction.** Kernel/driver-authoring surface (LKM/KEXT/WDM,
  scheduler-class authoring, ring-0) is out everywhere *except* where a power
  domain descends into it via `↓under`.

## shared gating axes
Every per-domain charter inherits these columns and *refines* them — splitting
where one platform exposes disjoint API families that classify differently
(camera split `android` → camera2-NDK + CameraX, `linux` → v4l2 + libcamera +
pipewire). The atlas pins the platform set; the per-domain charter pins the API
generation.

| axis | dominant API family | default GPU |
|------|---------------------|-------------|
| `macos` | Cocoa / Foundation / AppKit; CoreFoundation; XNU/BSD syscalls | metal |
| `ios` | UIKit / Foundation; tighter sandbox, no fork/exec | metal |
| `linux` | glibc/musl + Linux uAPI; X11 **vs** Wayland; systemd/D-Bus — split per domain | vulkan |
| `android` | NDK (C) **+** framework (Java/JNI); SELinux; pinned API level | vulkan |
| `win32` | Win32 **+** WinRT (disjoint subsets); NT syscalls | vulkan (d3d12 emerging) |
| `wasm` | browser Web APIs in a sandbox; the heavy-`deny` axis | webgpu |

GPU-backend axis (orthogonal, validated per platform): `metal | vulkan | webgpu`
(+ `d3d12`, see `design/gpu-d3d12.md`).

## index — 80 domains across 18 bands

Each row: `Dxx slug — title · status`. `designed` → fully run; `spawn` → a
`design/*.md` sketch already exists; `none` → unstarted.

### `10-substrate.md` — Bands I–III · execution, memory & time
- D01 process — process lifecycle & invocation · none
- D02 thread — threading, scheduling & affinity · none
- D03 sync — synchronization primitives · none
- D04 async-io — completion-based & readiness I/O multiplexing · none
- D05 dylib — dynamic linking & runtime code loading · none
- D06 jit — runtime code generation & W^X execution · spawn (`design/jit-*.md`)
- D07 cpu — CPU introspection & performance counters · none
- D08 vmem — virtual memory & address space · none
- D09 mempolicy — pressure, budgets & OOM · none
- D10 time — clocks, timers & calendars · none

### `11-storage.md` — Band IV · storage & filesystem
- D11 fs — files, directories & metadata · none
- D12 fs-sandbox — scoped & brokered file access · none
- D13 fs-watch — change notification & volumes · none
- D14 prefs — preferences & structured persistence · none

### `12-windowing.md` — Band V · display, windowing & compositor
- D15 window — windows, surfaces & the event loop · spawn (`design/platform-surface.md`)
- D16 display — monitors, modes, HDR & color · spawn (`design/platform-surface.md`)
- D17 present — compositor, vsync & frame pacing · spawn (`design/frame-pacing.md`, `frame-latency.md`)
- D18 cursor — pointer cursor & confinement · none
- D19 sysui — shell surfaces (menu / tray / dock / taskbar) · spawn (`tray`/`dialog`/`messagebox`)

### `13-gpu.md` — Band VI · graphics & GPU compute
- D20 gpu — GPU rendering & compute · spawn (`design/gpu-rhi.md`, `render-graph.md`, slang/bindless)
- D21 gpu-mem — GPU resources & cross-API sharing · spawn (`design/gpu-async-resolve-transfer.md`)
- D22 video-codec — hardware encode / decode / process · spawn (`design/media-video.md`)

### `14-audio.md` — Band VII · audio & music
- D23 audio-out — playback & output routing · spawn (`audioout`/`audiomixer`/`audioplayback`)
- D24 audio-in — capture & input DSP · spawn (`audioin`/`audiocapture`/`pcm`)
- D25 audio-policy — session, focus & interruptions · spawn (`audiopolicy`)
- D26 midi — MIDI I/O & timing · spawn (`midi`)

### `15-input.md` — Band VIII · human input
- D27 input-keyboard — keyboard & raw keys · spawn (`input`)
- D28 input-text — text composition & IME · none
- D29 input-pointer — mouse, trackpad & scroll · spawn (`input`)
- D30 input-touch — multitouch & gestures · none
- D31 input-pen — stylus & tablet · none
- D32 gamepad — game controllers & force feedback · spawn (`gamepad`/`hid`)
- D33 haptics — vibration & tactile feedback · spawn (`vibration`)
- D34 hid — generic & custom HID · spawn (`hid`)

### `16-sensors.md` — Band IX · sensors & physical-world I/O
- D35 sensor-motion — inertial & motion · spawn (`sensor`)
- D36 sensor-env — environmental sensors · spawn (`temperature`/`frequency`)
- D37 location — positioning & geofencing · spawn (`geolocation`)
- **D38 camera — image/video capture & egress · designed → `design/camera/`**
- D39 proximity-nfc — NFC & short-range tags · none
- D40 scan-code — barcode / document detection · spawn (`barcode`)

### `17-connectivity.md` — Band X · connectivity & networking
- D41 net-socket — transport sockets · spawn (`net`)
- D42 net-iface — interfaces, reachability & policy · none
- D43 net-discovery — name resolution & service discovery · none
- D44 net-http — HTTP stack & web transport · spawn (`http`/`server`, `design/http2-client.md`)
- D45 bluetooth — Bluetooth Classic & BLE · none
- D46 radio-wifi — Wi-Fi & local radios · none
- D47 telephony — cellular, SMS & calls · none
- D48 usb — USB & peripheral buses · none
- D49 serial-bus — serial, GPIO & embedded buses · none

### `18-ipc-sysint.md` — Bands XI–XII · IPC, inter-app & system integration
- D50 ipc-local — pipes, queues & shared memory · spawn (`channel`/`port`)
- D51 ipc-rpc — system buses & object RPC · none
- D52 interapp — deep links, intents & sharing · none
- D53 clipboard — pasteboard & drag-and-drop · spawn (`clipboard`)
- D54 dnd-files — *folded into D53*
- D55 notification — local & push notifications · spawn (`notification`, `design/notification*.md`, `push-*.md`)
- D56 background — background execution & scheduled work · none
- D57 packaging — install, entitlements & sandbox model · none
- D58 docmodel — documents, thumbnails & associations · none
- D59 shell-cli — terminal, TTY & process environment · spawn (`shell`/`repl`)
- D60 sysconfig — locale, theme & system settings · spawn (`locale`)
- D61 i18n — internationalization data & algorithms · spawn (`locale`/`string`)

### `19-security-power.md` — Bands XIII–XIV · security, identity, power & events
- D62 permission — runtime consent & authorization · none
- D63 credstore — keychain, secrets & secure hardware · none
- D64 crypto — cryptographic primitives & random · spawn (`digest`/`hash`/`rng`/`guid`)
- D65 authn — sign-in, biometrics & passkeys · none
- D66 integrity — attestation & anti-tamper · none
- **D80 content-protection — DRM & secure media path · none** *(new; gate-resolved)*
- D67 power — battery, charging & energy policy · spawn (`power`)
- D68 thermal — thermal state & throttling · spawn (`thermal`/`temperature`)
- D69 sysevents — session, sleep/wake & lifecycle · spawn (`event`/`signal`)

### `20-assistive-ml-xr-userdata.md` — Bands XV–XVIII · accessibility, speech, ML, XR & user-data
- D70 a11y — accessibility tree & assistive control · none
- D71 speech-tts — text-to-speech synthesis · spawn (`tts`)
- D72 speech-stt — speech recognition & voice · spawn (`stt`)
- D73 ml-infer — neural inference & accelerators · none
- D74 ml-vision — OS vision / NL detectors · none
- D75 xr — head/hand tracking & spatial scene · spawn (`design/xr.md`)
- D76 pim — contacts, calendar & reminders · none *(promoted to full domain)*
- D77 media-library — photos, music & media assets · none *(promoted)*
- D78 health-wallet — health, fitness & wallet · none *(promoted)*
- D79 print — printing & page output · none *(promoted)*

## scope edges & deliberately unchecked
Honest accounting of the boundary (MEL-ENGINE-VIII; the missing 20% is where
breaks hide):
- **Kernel/driver-authoring** (ring-0, scheduler-class / driver-model authoring) —
  excluded by construction; only `↓under` reaches descend, and only where a power
  domain (D49 serial-bus, D48 usb, D38 camera baresensor) demands.
- **Payments / in-app purchase / commerce** (StoreKit / Play Billing) — store
  surface, omitted as commerce, not OS-capability. Adjacent to D66 (receipt
  validation) and D80. **Gate if wanted.**
- **Web-only capabilities** with no native peer (Service Workers, WebRTC,
  Web-Bluetooth/Serial/USB/HID/NFC as their own model, Storage buckets) — folded
  into each domain's `wasm` *column*, not separate domains. **Confirm that mapping
  holds per domain at P1.**
- **Virtualization / containers** (Hypervisor.framework, WSL, namespaces as a
  capability) — excluded as host-tooling, not app-framework surface.
- **Granularity risk** — Bands V (window/display/present) and VI (gpu/gpu-mem/
  video-codec) are split into multiple domains; whether each is one
  module-design run or several is a per-domain charter call, not settled here.

## consumption → per-domain runs
Each indexed row seeds one `design/<domain>/` run via `module-design`, smallest /
most-foundational dependency first (Band I–IV substrate before V+ surface). The
shared axes here are inherited and refined per charter. `camera` (D38) is the
worked reference for shape and depth.

## changelog
- (initial) — 79 domains across 18 bands; userspace baseline + flagged
  `↑beyond`/`↓under` reaches; top-down, existing modules not consulted; Band XVIII
  + 4 named items left at explicit scope gates.
- grain refined to domain → areas → **sub-features** (selective depth); domain
  bodies moved to per-band files (`10`–`20`), this file slimmed to super-charter +
  index. Gate decisions folded in: Band XVIII (PIM / media-library / health-wallet
  / print) **promoted** D76–D79 to full domains; **DRM → new D80
  content-protection** (gate-resolved); D54 dnd-files folded into D53. Total 80
  domains. DRM removed from "unchecked"; payments + web-only mapping remain open.
