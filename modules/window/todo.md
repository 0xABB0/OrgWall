# window — todo

Audit of `third-party/sdl3` `SDL_video.h` (plus Vulkan/Metal/flags adjacencies)
against the window module's public surface. Items below are features SDL affords
that this module does not. Cross-module ownership is recorded so covered ground is
not re-implemented here.

## Window state & mode
- Minimize / maximize / restore (`SDL_MinimizeWindow`, `SDL_MaximizeWindow`, `SDL_RestoreWindow`).
- Fullscreen, incl. exclusive-mode selection (`SDL_SetWindowFullscreen`, `SetWindowFullscreenMode`, `GetWindowFullscreenMode`).
- Raise z-order without activating (`SDL_RaiseWindow`); `set_focus` conflates the two.
- State query (`SDL_GetWindowFlags`); no way to poll minimized/focused/occluded/grabbed — transitions arrive only by callback.
- Commit barrier for async WM state changes (`SDL_SyncWindow`).
- Demand attention (`SDL_FlashWindow`).

## Runtime-mutable attributes
- Toggle bordered / resizable / focusable live (`SDL_SetWindowBordered`, `SetWindowResizable`, `SetWindowFocusable`); these are create-time only.
- Min/max size at runtime (`SDL_SetWindowMinimumSize`, `SetWindowMaximumSize`); read once from `Mel_Window_Opt`.
- Opacity / transparent framebuffer (`SDL_SetWindowOpacity`, `SDL_WINDOW_TRANSPARENT`).
- Always-on-top (`SDL_SetWindowAlwaysOnTop`).
- Aspect-ratio lock (`SDL_SetWindowAspectRatio`).
- Window icon (`SDL_SetWindowIcon`).

## Window kinds & relationships
- Popup / tooltip / menu children (`SDL_CreatePopupWindow`; `TOOLTIP`/`POPUP_MENU`/`UTILITY` flags).
- Modal and parent/child (`SDL_SetWindowModal`, `SetWindowParent`, `GetWindowParent`).
- Shaped (non-rectangular) windows (`SDL_SetWindowShape`).
- Custom hit-testing (`SDL_SetWindowHitTest`) — required to move/resize an `undecorated` window, which is creatable but immovable.
- System menu (`SDL_ShowWindowSystemMenu`).
- Taskbar/dock progress (`SDL_SetWindowProgressState`, `SetWindowProgressValue`).

## Getters absent
- Read back title (`SDL_GetWindowTitle`), position (`SDL_GetWindowPosition`; `x,y` stored, no getter), pixel format (`SDL_GetWindowPixelFormat`).

## Input grab & confinement
- Mouse / keyboard grab (`SDL_SetWindowMouseGrab`, `SetWindowKeyboardGrab`, `GetGrabbedWindow`).
- Cursor confinement rect (`SDL_SetWindowMouseRect`).

## Input callback completeness
`Mel_Window_Input_Cb` carries position-only pointer events and raw keycode.
- Pointer events lack button identity (left/right/middle indistinguishable).
- No scroll-wheel event.
- No keyboard modifiers; no scancode-vs-keycode split.
- No text input / IME (`SDL_StartTextInput`, `SetTextInputArea`).
- No touch (`SDL_touch.h`), no pen/stylus (`SDL_pen.h`).
- No file drag-and-drop onto the window.
- No cursor management — visibility, custom cursors, warp (`SDL_mouse.h`).

## Cross-module joins (gap is the wiring, not the feature)
- window → display mapping (`SDL_GetDisplayForWindow`): the `display` module enumerates and describes monitors but nothing resolves which display a window occupies, blocking per-window scale/HDR/ICC lookup.
- Raw inset / border geometry without `gui`: safe-area and chrome insets live in `gui/insets.h`, unreachable by a window+gpu consumer that has no widget tree (`SDL_GetWindowSafeArea`, `SDL_GetWindowBordersSize`).
- Raw window pixel buffer for presentation (`SDL_GetWindowSurface` family): `paint` draws through the native 2D context; `Mel_Pixmap` is offscreen with readback and has no blit-to-window. Absent by design unless a byte-level present path is wanted.

## Covered elsewhere — do not re-implement here
- Multi-monitor enumeration, modes, VRR, HDR/EDR, ICC, color space, connector, physical size, virtual position, scale, hotplug events — `display` module (exceeds `SDL_video.h`).
- 2D drawing into a window without GPU — `paint` module (borrowed-window drawable + `Mel_Pixmap`).
- Safe-area / cutout / IME / system-bar insets and PAD/EDGE_TO_EDGE policy — `gui/insets.h` (typed superset of `SDL_GetWindowSafeArea`).

## SDL-parity state surface (`include/window/state.h`)
Implemented as the augmented surface: min/max size, aspect-ratio lock, fullscreen + exclusive
video-mode select, opacity, always-on-top, borderless/resizable runtime toggles, window icon,
modal + parent/child, custom hit-test, window shape (alpha mask), mouse/keyboard grab,
mouse-confinement rect, taskbar progress (state+value), flags query, safe-area rect, ICC fetch
(sync + async over future/executor with generation-checked cancel op), pixel-format/density,
window-by-id + enumerate-all, maximize/minimize/restore/raise, flash, get/present CPU surface.
Closed sets are anonymous flag bitsets (no reflection enums); operations carry `Mel_Window_Status`.

## Backend coverage
- cocoa (host): full `Mel_Window_Backend_Ops` (`src/cocoa/state.m`).
- win32: full ops authored (`src/win32/state.c`), compiles on win-pilot; unbuilt/untested on macos host.
- ios / android / linux / wasm: no-op `src/stub/backend.c` returns NULL ops — every augmented call
  reports `MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE` (honest-absent, MEL-ENGINE-VII).
  Linux X11/Wayland ops not yet authored (gap, not a refusal).

## Declared but never fired
- `Mel_Window_Backing_Cb` (`on_backing_lost`, `on_content_replaced`) — declared, never invoked.
- `Mel_Window_Display_Cb.on_orientation_changed` — declared, never invoked.
- `on_hdr_changed` — fired on macOS backing/screen changes, but no HDR state query exists to read the result.

## Gate residuals (deferred, honest-absent)
- Backend OOM allocs unchecked in `cocoa_get_surface`/`cocoa_icc_profile` and `win32_get_surface`/`win32_icc_profile`: `mel_assert` cannot be used in `win32/state.c` because `S8`/`countof` expands `(size)` which collides with the local `DWORD size`; needs a collision-free assert or a renamed local before adding the OOM guard. `state.c` async fetch path is guarded (`mel_assert(op != NULL)`).
- `set_fullscreen_mode` (cocoa + win32) now returns false → honest `MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE`; exclusive video-mode switch (CGDisplaySetDisplayMode / ChangeDisplaySettingsEx) unauthored.
- win32 `set_min_size`/`set_max_size`/`set_aspect` return false (honest unavailable); `WM_GETMINMAXINFO` handler in `src/win32/backend.c` unauthored.
- win32 `set_keyboard_grab` and `set_shape`, cocoa `set_mouse_grab`/`set_keyboard_grab`/`set_mouse_rect` return false (honest unavailable); WH_KEYBOARD_LL / SetWindowRgn / CGAssociateMouseAndMouseCursorPosition unauthored.
- `mel_window_set_hit_test` returns `MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE`: no backend consumes `n->hit_test` (no `WM_NCHITTEST`, no cocoa hitTest override) yet.
- Live-state honesty: setters store `n->mouse_grab`/`n->keyboard_grab`/etc. before dispatch, so `query_state` flags may report a state the (now-false) backend op never effected. `MEL_WINDOW_STATE_TRANSPARENT` is composed from `n->transparent`, which is written nowhere.
- `g_icc_ops` slotmap is process-static, never `mel_slotmap_free`'d on `mel_window_shutdown`; persists across init/shutdown. Unsynchronized global — concurrent fetch/cancel/release is an unguarded data race; ratified loop-affinity asserts absent.
- `mel_window_fetch_icc_opt` ignores `opt.reactor`; ICC read resolves synchronously on the calling thread inside the deliver task (no background-thread path), so cancel only lands if invoked before drain.
- Linux/ios/android/wasm route to NULL-ops stub (honest-absent). X11/XCB window-state ops authorable via the gui dlopen'd-XCB pattern — gap, not a refusal (MEL-ENGINE-I/VII).
