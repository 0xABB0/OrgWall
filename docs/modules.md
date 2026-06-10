# Module index

One line per module: what it GIVES, then its key types. Before writing any capability —
a data structure, a hash, an async primitive, a platform wrapper — scan this index; if a
module covers it, use the module. Before using or modifying a module, read its `readme.md`.
Each line restates the opening of that module's readme; the readme is canonical.

## Foundation

- core — base scalar types, compiler attributes, common macros; key: `u32`/`i64`/`f32`, `MEL_NODISCARD`
- allocator — the `Mel_Alloc` callback waist plus heap/guard/tracking allocators; every allocating API takes `const Mel_Alloc*`
- collection — dynamic array, slotmap (generation handles), pool, queue/stack, lock-free MPSC; key: `Mel_Array`, `Mel_SlotMap`, `Mel_Mpsc`
- string — `str8` views and operations, builder, parse/format; key: `str8`, `Mel_String_Builder`
- quark — string interning: `str8` ↔ stable `u32` handle; key: `Mel_Quark`, `Mel_Quark_Table`
- hash — fast non-cryptographic hashing; key: `mel_xxh3_64`, `mel_xxh3_128`
- guid — 128-bit device identity in SDL GameControllerDB format; key: `Mel_Guid`
- rng — PRNGs and range/distribution helpers; key: `Mel_Rng`, `mel_pcg32_next`, xoshiro256
- math — vectors, matrices, quaternions, geometry, arbitrary-precision reals (MPFR); key: `Mel_Vec3`, `Mel_Mat4`
- curve — author-defined timing curves (cubic Bézier, linear, stepped); key: `Mel_Bezier`, `mel_curve_eval`
- easing — Penner easing functions; key: `mel_ease_*`
- color — color types, spaces, conversions (sRGB, Lab, OKLab, gamut mapping)
- frequency — exact-rational frequency unit; key: `Mel_Hz`, cents transposition
- temperature — exact-rational temperature unit; key: `Mel_Degrees`
- time — clocks, durations, sleep, calendar, frame pacing; key: `Mel_Duration`, `mel_nanos_since_unspecified_epoch`, `Mel_Frame_Clock`
- reflect — reflection metadata over types
- log — structured logging: domains, levels, sinks (console, sqlite); key: `MEL_LOG`, `Mel_Log_Sink`
- debug — assertions and stacktrace capture; key: `mel_assert`, assert handlers
- test — the test harness behind `mel_add_test` targets; key: `MEL_TEST`
- cpu — static CPU topology and cache geometry; key: `Mel_Cpu_Info`
- locale — OS-resolved user language preference list; key: `mel_locale_primary`

## Async substrate

- executor — the async waist everything composes over: intrusive zero-alloc `Mel_Task` (armed-coalesced), `Mel_Executor` vtbl, `Mel_Waker`
- future — one-shot write-once result; consume only via `mel_future_then` on an executor; key: `Mel_Future`
- channel — CSP channel, buffered or rendezvous, select, future flavors; key: `Mel_Channel`
- event — 1→N broadcast over the executor waist; key: `Mel_Event`, `mel_event_subscribe_push`
- vat — one OS thread as control domain: THE main loop. Sources (wakeables/deadline/drain/cancel), waiters (kqueue, cocoa UI, web guest), fair driver, timers, ticks, vsync, `mel_vat_executor`, retain/release lifetime, `mel_vat_post` cross-thread; key: `Mel_Vat`, `Mel_Vat_Source`, `Mel_Vat_Tick`
- boot — framework-owned `main`; the app defines `mel_app_setup(Mel_Vat*)`. Args, exit code, LIFO exit hooks, app lifecycle phases; key: `mel_app_on_exit`, `mel_app_lifecycle_subscribe`
- thread — OS threads, mutex, condvar, atomics; key: `Mel_Thread`, `Mel_Mutex`
- fiber — stackful fiber contexts and stacks, synchronous switching; key: `Mel_Fiber`
- signal — fiber parking/waking primitives; key: `Mel_Signal`, `Mel_Counter`
- job — fiber-pool worker-thread executor; key: `Mel_Job_Executor`, `mel_job_run`, `mel_job_yield`
- routine — frame-cadenced stackful routine scheduler (per-tick game/app logic); key: `Mel_Routine_Ctx`, `mel_routine_wait`
- coro — stackless coroutines with reified frames via the `coro-gen` codegen (`.coro.h`); key: `Mel_Coro_Frame`
- await — the suspension waist gluing coros/fibers to futures, channels, timers, executors; key: `mel_await_coro_start`, `mel_await_future`

## I/O & OS

- io — byte streams over files/memory/custom, vat-bound async where the fd is physically async, honest sync otherwise; key: `Mel_Stream`, `mel_io_file_open`
- fs — async filesystem ops on a worker pool, completions delivered on the vat; key: `Mel_Fs`, `mel_fs_stat`, `mel_fs_read_file`
- storage — key-value persistence atop fs/io; key: `Mel_Storage`
- port — the async I/O proactor (readiness synthesized to completion) on the vat: sockets, pipes, fds; key: `Mel_Port`, `mel_port_read`
- process — subprocess spawn/run, piped stdio, async exit reaping on the vat; key: `Mel_Process`, `mel_process_run`
- server — embedded HTTP/WebSocket server: routing, pub/sub, RPC, TLS (mongoose-backed); key: `Mel_Server`
- shell — open URLs and reveal paths via the OS shell; key: `mel_shell_open_url`
- dylib — runtime dynamic library loading; key: `Mel_Dylib`
- platform — low-level per-OS integration hooks the platform modules share
- power — power source, profile, battery telemetry
- thermal — thermal pressure tier and per-domain temperatures

## UI, input & media

- window — OS windows and render surfaces, loop-decoupled via `Mel_Window_Host`; key: `Mel_Window`, `mel_window_create`
- display — physical display enumeration: modes, refresh, HDR, color space; key: `Mel_Display`
- gui — the widget GUI framework on the vat: screens, panels/column layout, labels, buttons, canvas widget; key: `mel_gui_init(Mel_Vat*)`, `mel_panel_create`, `mel_label_create`, `mel_button_create`, `Mel_Canvas`
- paint — immediate-mode 2D painting (CoreGraphics/GDI/DOM backends); key: `Mel_Painter`, `Mel_Pixmap`
- gpu — the RHI over Metal/Vulkan/D3D12/WebGPU: devices, buffers, pipelines, vsync-paced render sources; key: `Mel_Gpu_Device`
- image — CPU-side pixel buffers, format-plural; key: `Mel_Image`
- camera — OS camera capture: authorization futures, frame streaming, delivery on an executor; key: `mel_camera_init(alloc, Mel_Executor*)`
- barcode — barcode/QR encode AND decode (code128/39, EAN, ITF, QR detect+decode, Reed-Solomon); key: `barcode/decode.h`, `barcode/qr.h`
- audio — the audio engine: voices, mixing, faders, offline render; key: `mel_audio_create(alloc, opt)`, `Mel_Voice`
- midi — MIDI protocol constants and device I/O
- musictheory — notes, scales, note↔frequency math
- vibration — haptics/force-feedback across devices, pattern playback on the vat; key: `mel_vib_init(alloc, Mel_Vat*)`, `mel_vib_play`
- input — canonical input-device spine: keyboard, mouse, touch, pen, hot-plug, events; key: `Mel_Input_Device`, `Mel_Scancode`
- gamepad — joysticks and standardized gamepads atop hid; key: `Mel_Joystick`
- hid — raw HID device transport; key: `Mel_Hid_Device`
- sensor — IMU access (accelerometer, gyroscope); key: `Mel_Sensor`
- clipboard — clipboard read/write/watch with futures on the vat; key: `mel_clip_init(alloc, Mel_Vat*)`
- dialog — native file/folder pickers, async on the vat; key: `mel_dialog_open_file`
- messagebox — modal native alerts; key: `mel_msgbox_show`
- tray — system tray / status icons with menus; key: `Mel_Tray`

## Dev tooling

- jit — backend-agnostic JIT interface; key: `Mel_Jit_Backend`, `Mel_Jit_Module`
- llvm — LLVM ORC backend and IR utilities for jit
- clang — C frontend: compile C source into jit modules, REPL language; key: `mel_clang_compile`
- repl — backend-agnostic read-eval-print engine; key: `Mel_Repl_Lang`
- build — the build framework itself (`nob`); not a discovered module; authoring surface in `modules/build/platforms.md`

## Exemplars — copy the shape, don't reinvent it

- boot-hosted app (minimal): `apps/hello-window/src/setup.c`
- boot-hosted CLI on async stdin (port read on fd 0): `apps/repl/src/setup.c`
- future-chain state machine (stage = task run pointer): `apps/barcode-reader/src/setup.c`
- gui app with screens/widgets: `apps/hello-world-gui/`
- vat source, fd readiness: `modules/port/src/posix/`
- vat source, deadline-paced: `modules/vat/src/tick.c`
- worker→vat completion hop (embedded task + retain/release): `modules/fs/src/fs.c`
- loop-test run-helper (waiter + fair driver + deadline-0 idle source): `modules/port/test/test_port.c`
