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

## Backend coverage
- cocoa: full. win32: partial. ios / android / linux / wasm: no-op stub (`src/stub/backend.c`).

## Declared but never fired
- `Mel_Window_Backing_Cb` (`on_backing_lost`, `on_content_replaced`) — declared, never invoked.
- `Mel_Window_Display_Cb.on_orientation_changed` — declared, never invoked.
- `on_hdr_changed` — fired on macOS backing/screen changes, but no HDR state query exists to read the result.
