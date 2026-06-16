# Display, windowing & compositor — OS-surface atlas (finer grain)
> domains D15–D19. Consumed by 00-atlas.md. Capability ceiling, axis-neutral; platform APIs are hints.

### D15 · window — windows, surfaces & the event loop
def: owning a region of the screen and pumping its events.
- **lifecycle**:
  · create / destroy / surface acquisition (NSWindow / HWND / xdg-toplevel / ANativeWindow / `<canvas>`)
  · top-level role assignment (xdg-shell toplevel vs X11 override-redirect / WS_OVERLAPPED)
  · reparent / ownership (X11 reparent; win32 owner-window; not portable on Wayland)
  · recreate on surface loss (Android surface destroy/recreate; GL/Vulkan context loss)
- **style & decoration**:
  · server-side decorations (X11 WM / win32 NCAREA / macOS titlebar)
  · client-side decoration (Wayland CSD / xdg-decoration negotiation; macOS full-size content)
  · titlebar / border / caption-button presence & style
  · borderless / chromeless
- **geometry**:
  · size / position / move (Wayland leaves position to compositor — no programmatic set)
  · min / max size constraints & aspect-ratio lock
  · resize increments / step (ICCCM size-hints)
  · content-vs-frame coordinate split & safe-area / inset query
  · DPI-aware geometry & rescale-on-move (per-monitor v2)
- **state**:
  · minimize / maximize / restore
  · fullscreen (exclusive vs borderless-windowed / `requestFullscreen`)
  · hide / show / occlude-driven visibility
  · window-state-change events
- **multi-window & relationships**:
  · multiple top-levels
  · child / popup / xdg-popup (grab, positioner anchoring)
  · utility / palette / tool windows
  · transient-for / owner relationship
  · tabbed-window grouping (macOS NSWindowTabGroup?)
- **z-order & level**:
  · raise / lower / order within app
  · window level / layer band (macOS NSWindow.level; X11 _NET_WM_STATE; layer-shell for panels — Wayland)
  · always-on-top
  · multi-output placement
- **shape & transparency**:
  · per-pixel alpha / translucent surface
  · shaped / non-rectangular region (X11 SHAPE; HRGN)
  · click-through (ignore-mouse-events)
  · hit-test / input region (Wayland wl_surface input region; WM_NCHITTEST)
  · blur-behind / vibrancy (macOS NSVisualEffectView / DWM backdrop / acrylic-mica)
- **focus & activation**:
  · activate / request-attention (Wayland xdg-activation token; X11 _NET_ACTIVE_WINDOW)
  · focus-follows vs click-to-focus awareness
  · focus-in/out & key-focus vs activation split
  · steal-prevention / activation-token gating
- **event loop**:
  · run-loop integration & dispatch (CFRunLoop / GMainLoop / message pump / Wayland wl_display dispatch / RAF tick)
  · event injection / custom events / wake from other thread
  · idle / timer callbacks within the loop
  · modal nested loops (menu tracking, resize loop)
  · main-thread affinity constraint
- **modality** — app-modal · window-modal / sheet · system-modal
- **close & quit**: close request & veto (WM_CLOSE / windowShouldClose / beforeunload) · quit-on-last-window vs persistent · session-restore hooks
- **drag regions** — title-bar drag area · programmatic move/resize start (Wayland xdg_toplevel move/resize) · resize-handle hit zones
↑beyond: Wayland xdg-shell vs X11 ICCCM/EWMH (split, classify disjointly); win32 DWM extends (backdrop, immersive dark mode); Wayland layer-shell / foreign-toplevel protocols.
↓under: direct KMS/DRM scanout (no compositor) · fbdev.
apps: every GUI app; multi-window editors, tiling-aware tools.
status: spawn (`design/platform-surface.md`).

### D16 · display — monitors, modes, HDR & color
def: discovering and configuring the physical outputs.
- **enumeration & geometry**:
  · monitor list & identity / stable id (CGDirectDisplayID / wl_output / EnumDisplayMonitors / Display.getDisplays)
  · physical size (mm) & pixel resolution
  · virtual-desktop arrangement & per-output origin
  · primary / built-in vs external classification
  · work-area vs full-bounds (minus dock/taskbar)
- **modes & mode-set**:
  · available mode list (resolution × refresh × bit-depth)
  · current-mode query
  · mode-set / resolution change (gated; rare on Wayland/mobile/web)
  · refresh-rate selection
  · scaled vs native (HiDPI default-mode) reporting
- **scale & DPI**:
  · per-monitor scale factor & DPI (logical↔physical)
  · fractional scaling (Wayland wp-fractional-scale; GTK)
  · scale-change events on move/hotplug
  · text-scale vs layout-scale split awareness
- **HDR & EDR**:
  · HDR capability & current HDR-on state
  · EDR / headroom query (macOS maximumExtendedDynamicRangeColorComponentValue)
  · tone-mapping mode & SDR-white-level / reference luminance
  · peak / max-average luminance (nits) reporting
- **color & gamut**:
  · ICC / color-profile query per display (color-management protocol — Wayland WIP?)
  · wide-gamut support (P3 / Rec.2020) & current colorspace
  · color-space assignment for present (CAMetalLayer colorspace / DXGI color space)
- **orientation** — rotation (0/90/180/270) · reflection / flip · auto-rotate state (mobile)
- **brightness & gamma**:
  · brightness get/set (gated)
  · gamma / LUT ramp (SetDeviceGammaRamp / CGSetDisplayTransferByTable; deprecated on some)
  · per-channel calibration LUT
- **VRR / adaptive-sync**:
  · adaptive-sync / FreeSync / G-Sync capability
  · ProMotion / variable refresh range (min–max Hz)
  · current-refresh feedback
- **lifecycle events**:
  · hotplug connect / disconnect
  · arrangement / topology change
  · mode / scale change broadcast
- **mirroring** — mirror set detection · clone vs extend topology
- **vendor state** — night-shift / true-tone / blue-light state · overscan / underscan compensation
↑beyond: EDID parsing & manufacturer/serial decode; DisplayPort MST hub topology; HDR10+ / Dolby Vision dynamic metadata; HDCP capability query (boundary with D22/DRM).
↓under: DRM/KMS connector & mode introspection (`/dev/dri`); DDC/CI over I²C for external-monitor brightness.
apps: color-grading tools, games, presentation/multi-display apps.
status: spawn (`design/platform-surface.md`).

### D17 · present — compositor, vsync & frame pacing
def: getting rendered frames onto the glass on time.
- **swapchain**:
  · surface / swapchain create & resize (VkSwapchain / CAMetalLayer drawable / DXGI swap chain / IDXGISwapChain)
  · image / drawable count & acquisition
  · out-of-date / suboptimal recreation
  · format & color-space selection (shared with D16)
  · buffer-age query (EGL/Wayland for partial-update)
- **present modes**:
  · FIFO / vsync (queued, no tear)
  · mailbox / triple-buffer (latest-wins)
  · immediate / tearing (no vsync; VK_PRESENT_MODE_IMMEDIATE / DXGI_PRESENT_ALLOW_TEARING)
  · FIFO-relaxed / adaptive
- **vsync & tear control** — vsync on/off · tear-allowed flag · sync-to-which-output (multi-monitor)
- **frame callbacks & deadline**:
  · display-link / vsync callback (CADisplayLink / DisplayLink / Choreographer / Wayland frame callback)
  · waitable swapchain object (DXGI waitable / IDXGISwapChain2)
  · `requestAnimationFrame` tick (wasm)
  · present-deadline / target-time scheduling (Vulkan present-timing? / present-wait)
  · predicted next-vsync / refresh-period query
- **latency**:
  · latency reporting (frames-in-flight; DXGI frame statistics; present-id feedback)
  · low-latency mode / max-frame-latency (DXGI maximum frame latency; Reflex-style? — vendor)
  · pre-present / late-latch wait point
- **composition layers**:
  · layer / sublayer tree (CALayer / DComp visual tree)
  · subsurfaces (Wayland wl_subsurface)
  · hardware overlay planes (SurfaceFlinger overlays; DRM planes)
  · external-content layers (video / WebGL canvas compositing)
  · transform / opacity / clip per layer
- **occlusion & visibility**:
  · occlusion / fully-obscured notification (NSWindow occlusion state)
  · visibility / page-visibility (wasm document.hidden)
  · pause-rendering-when-hidden signal
- **damage & partial present**:
  · dirty-rect / damage region present (eglSwapBuffersWithDamage; Present1 dirty rects; Wayland surface damage)
  · partial-update / scroll-rect optimization
- **adaptive frame-rate**:
  · request preferred frame-rate range (CADisplayLink preferredFrameRateRange; Choreographer setFrameRate)
  · throttle-to-content (low-power scroll/idle)
↑beyond: explicit present-timing / GPU-present-time feedback; vendor low-latency frame generation (DLSS-FG / Reflex interop — boundary with D20); compositor bypass / direct-scanout flip.
↓under: KMS atomic pageflip & explicit vblank events (`drmModePageFlip` / `DRM_EVENT_VBLANK`).
apps: games, video players, scroll-heavy UIs.
status: spawn (`design/frame-pacing.md`, `design/frame-latency.md`).

### D18 · cursor — pointer cursor & confinement
def: the visible pointer and where it may go.
- **shapes**:
  · standard system shapes (arrow / ibeam / crosshair / hand / resize-NS/EW/NESW/NWSE / wait / not-allowed) (NSCursor / LoadCursor / Wayland cursor-shape protocol / `cursor:` CSS)
  · per-theme / system cursor-theme awareness (X11/Wayland XCURSOR theme + size)
- **custom**:
  · custom image / bitmap cursor (hotspot-anchored)
  · animated cursor (frames; ANI / CSS sequence?)
  · high-DPI / scaled cursor asset
- **visibility** — show / hide · hide-on-type / cursor-on-input reveal · auto-hide after idle
- **confinement & clip**:
  · clip / confine to rectangle (ClipCursor; Wayland pointer-constraints confine)
  · confine to window content region
  · barrier / pointer-barrier (XFixes pointer barriers?)
- **warp & position** — set / warp absolute position (WarpMouseCursorPosition; Wayland disallows free warp) · query position
- **relative / locked mode**:
  · pointer-lock / relative motion (Wayland locked-pointer; Pointer Lock API; raw-input relative)
  · lock-to-position vs free-but-hidden
  · re-center-on-frame for mouse-look
- **per-window** — per-window cursor association · cursor region / rects within a window (NSTrackingArea / WM_SETCURSOR)
- **hardware vs software** — HW cursor plane vs SW-composited awareness (DRM cursor plane)
apps: games (FPS mouse-look), drawing apps, remote desktop.
status: none.

### D19 · sysui — shell surfaces (menu / tray / dock / taskbar)
def: the OS-owned chrome an app populates outside its own window.
- **menus**:
  · app / global menu bar (macOS NSMenu; Unity/global-menu DBus export)
  · per-window menu bar (win32 HMENU; GTK menubar)
  · context / popup menu (right-click)
  · menu items: submenu · separator · checkbox / radio · icon · key-equivalent / accelerator · enable/validate state
  · services / share submenu integration
- **tray / status item**:
  · status-bar item / menu-extra (macOS NSStatusItem)
  · system tray icon (win32 Shell_NotifyIcon)
  · indicator / StatusNotifierItem (Linux SNI / AppIndicator over DBus; legacy XEmbed tray)
  · tray icon + attached menu + tooltip
  · balloon / tray notification (boundary with D55)
- **dock / taskbar**:
  · taskbar / dock presence & button
  · progress indicator on icon (DockTile / ITaskbarList3 progress / Unity LauncherEntry)
  · badge / overlay count (NSDockTile badge / setOverlayIcon / setBadgeCount)
  · attention / bounce / flash (requestUserAttention / FlashWindowEx)
  · taskbar thumbnail & toolbar buttons (ITaskbarList3 thumb-buttons / live preview — win32)
- **jump / dock menu** — jump list / recent / tasks (win32 JumpList) · dock-tile menu (macOS) · launcher quicklist (Unity)
- **app icon** — runtime icon set / overlay · alternate app icons (iOS?) · dock-tile custom content view (macOS)
- **global hotkeys**:
  · system-wide hotkey registration (RegisterHotKey; Carbon RegisterEventHotKey; Wayland global-shortcuts portal?)
  · media / consumer keys
  · conflict / already-registered reporting
- **WM hints**:
  · urgency / demands-attention (_NET_WM_STATE_DEMANDS_ATTENTION)
  · skip-taskbar / skip-pager
  · sticky / on-all-workspaces & workspace/virtual-desktop placement
  · window role / type hint (_NET_WM_WINDOW_TYPE: dock / utility / dialog)
↑beyond: Wayland portal-mediated shortcuts/tray (xdg-desktop-portal; no direct protocol — classify x11 direct vs wayland-portal); win32 taskbar thumbnail toolbars & live-preview surfaces.
apps: menu-bar utilities, background agents, launchers.
status: spawn (`tray`, `messagebox`, `dialog` sketches exist as domains).
