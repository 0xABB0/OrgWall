# Human input — OS-surface atlas (finer grain)
> domains D27–D34. Consumed by 00-atlas.md. Capability ceiling, axis-neutral; platform APIs are hints.

---

### D27 · input-keyboard — keyboard & raw keys
def: physical key activity and layout.
- **events**:
  · down / up
  · auto-repeat (rate vs synthesized)
  · key-press char vs raw key
  · focus-gated delivery vs background / global
- **identity & mapping**:
  · scancode (positional / USB-HID usage)
  · keycode (virtual / VK)
  · keysym / character (layout-resolved)
  · the scancode↔keycode↔keysym resolution chain
- **layout**:
  · active layout query
  · layout-change events
  · multi-layout / per-locale enumeration
  · layout-to-character lookup (without dispatching a keystroke)
- **modifiers & locks**: shift / ctrl / alt / super-cmd · caps / num / scroll lock state · left/right distinction · sticky-keys awareness
- **dead keys & sequences**: dead-key compose · compose-key sequences · marked / pending state
- **raw / unfiltered** (Raw Input / evdev / IOHIDManager / HID):
  · pre-layout scancode stream
  · n-key rollout / simultaneous-key state (no ghosting)
  · multiple physical keyboards distinguished
  · bypass of OS repeat & autorepeat
- **remap awareness**: OS-level remap visibility · injected / synthetic-event flag
↓under: evdev grab (EVIOCGRAB), HID usage tables (keyboard usage page 0x07).
apps: games, terminals, accessibility remappers, shortcut-heavy tools.
status: spawn (`input` domain).

### D28 · input-text — text composition & IME
def: turning input into Unicode text, including complex scripts.
- **composition / preedit**:
  · marked / preedit string & styling
  · candidate list & selection
  · commit vs cancel
  · cursor / clause segmentation within preedit
- **complex scripts**: CJK conversion · Indic / Brahmic reordering · Thai/Lao clusters · Arabic/Hebrew shaping handoff
- **virtual keyboard**: show / hide request · keyboard-frame & inset geometry · keyboard type / return-key hint · safe-area / scroll-into-view reaction
- **assistance**: autocorrect · predictive / suggestion bar · autocapitalize · smart quotes/dashes · undo of autocorrect
- **dictation**: dictation entry point · interim-text insertion (boundary with D72)
- **text-field semantics the IME needs**:
  · content / input type (email / number / password / URL)
  · current selection & marked range
  · surrounding-text query for context
  · cursor rectangle for candidate-window placement
- **interplay**: clipboard-paste vs composition · bidi caret movement & visual/logical order · key-event vs committed-text reconciliation
apps: editors, chat, form-heavy apps, anything multilingual.
status: none.

### D29 · input-pointer — mouse, trackpad & scroll
def: indirect pointing devices and their gestures.
- **motion**:
  · absolute position (in-window / screen)
  · relative / raw deltas (unaccelerated, for mouse-look)
  · sub-pixel / high-resolution deltas
  · enter / leave / capture-outside-window
- **buttons**: primary / secondary / middle · extra (back/forward, 4..N) · multi-click count · press vs release vs synthesized-click
- **scroll / wheel**:
  · line / notch stepped
  · pixel / smooth / high-resolution
  · momentum / inertial phase & end
  · horizontal & precise diagonal
  · scroll-direction (natural) awareness
- **trackpad gestures**: pinch (magnify) · rotate · 2/3/4-finger swipe · smart-zoom (two-finger double-tap) · force-click / pressure stage
- **acceleration & devices**: pointer-acceleration awareness · multiple simultaneous pointers · device-class / source distinction (mouse vs trackpad vs touchpad-as-pointer)
- **hover & proximity**: hover position without buttons · proximity / approach where sensed
apps: drawing/CAD, browsers, games, presentations.
status: spawn (`input` domain).

### D30 · input-touch — multitouch & gestures
def: direct touch surfaces.
- **touch points**:
  · per-touch id / tracking across moves
  · position (logical & device)
  · phase (begin / move / stationary / end / cancel)
  · simultaneous-point count & limits
- **per-touch attributes**: pressure / force · radius / contact size · major-minor ellipse · orientation/angle where sensed
- **gesture recognition** (OS-provided): tap & multi-tap · long-press · swipe (directional) · pinch · rotate · pan · system vs app recognizer arbitration
- **system-gesture conflict**:
  · edge / system-gesture reservation (back / home / notification pull)
  · defer-system-gesture request
  · home-indicator / edge-swipe priority
- **palm rejection**: OS palm-reject signal · accidental-touch suppression
- **timing & fidelity**: touch prediction · coalesced / batched intermediate points · per-sample timestamps · input latency reporting
apps: mobile apps, drawing, maps, games.
status: none.

### D31 · input-pen — stylus & tablet
def: pressure-sensitive pointing.
- **pressure & geometry**:
  · pressure / normal force
  · tilt (altitude / X-Y tilt)
  · azimuth / rotation (barrel roll where sensed)
  · contact area
- **hover & range**: in-range / proximity without contact · hover position & distance · approach phase
- **buttons & gestures**: barrel / side button · eraser end · double-tap (Apple Pencil) · squeeze (Pencil Pro) where present
- **identity**: per-pen / per-tool id · tool-type (pen vs eraser vs airbrush) distinction · pen serial / pairing where exposed
- **palm rejection**: pen-priority palm-reject · simultaneous touch + pen disambiguation
- **mapping & timing**:
  · tablet active-area → screen mapping (Wintab / RealTimeStylus / Pointer Events / MotionEvent tool-type)
  · absolute vs relative tablet mode
  · prediction / lookahead
  · coalesced high-rate samples & timestamps
- **api families**: Wintab vs Pointer-Events vs RealTimeStylus (win32 split) · MotionEvent tool-type (android) · PencilKit/UITouch attributes (ios)
↓under: HID digitizer usage page (0x0D) raw reports.
apps: drawing & note apps (Procreate, Photoshop), signature capture.
status: none.

### D32 · gamepad — game controllers & force feedback
def: enumerated game input devices and their outputs.
- **enumeration & lifecycle**:
  · device discovery & hotplug connect/disconnect
  · controller class / profile (standard / extended / racing)
  · capability query (which axes/buttons/sensors present)
  · connection transport (USB / BT / wireless dongle)
- **input state**:
  · digital buttons (face / shoulder / stick-click / system)
  · analog triggers (L2/R2 with travel)
  · analog sticks (X/Y, dead-zone awareness)
  · dpad / hat
  · standard-mapping normalization vs raw HID layout
- **rumble & force feedback**:
  · dual / low+high-frequency motors
  · trigger / impulse haptics (Xbox triggers)
  · localized-actuator addressing
- **adaptive triggers** (DualSense): resistance / weapon-effect modes · trigger-effect curves · force-position points
- **on-pad sensors**: gyro / accel motion · touchpad surface (DualShock/DualSense) · per-touch on touchpad
- **indicators & state**: lightbar / RGB color · player-index LED · battery level & charging state · headset-jack / audio presence
- **assignment & profiles**: per-player assignment · remap / profile · controller-profile import
↑beyond: HID++ / raw controller protocols, Steam Input, trigger-effect curves, gyro-as-mouse.
↓under: raw HID gamepad reports (usage page 0x01 joystick/gamepad), libusb/hidraw for unmodeled pads.
apps: games, emulators, robotics teleop.
status: spawn (`gamepad`/`hid` domains; `apps/ballgame`).

### D33 · haptics — vibration & tactile feedback
def: driving tactile actuators.
- **simple vibration**:
  · one-shot buzz (duration / amplitude)
  · canned patterns (timing arrays)
  · predefined system feedback (selection / impact / notification)
- **rich haptic engine** (CoreHaptics / VibrationEffect composition):
  · transient (sharp tap) vs continuous events
  · intensity envelope
  · sharpness / frequency envelope
  · composed / sequenced primitive patterns
  · dynamic parameter modulation during playback
- **audio-coupled**: audio-synchronized haptics · audio-to-haptic (AHAP-with-audio) where present
- **routing target**: device built-in actuator · game-controller actuator (D32 overlap) · per-actuator selection
- **engine lifecycle & query**: capability / supported-feature query · engine start/stop & reset · interruption (call / route) recovery
apps: games, notification design, accessibility cues.
status: spawn (`vibration` domain; `apps/hello-vibration`).

### D34 · hid — generic & custom HID
def: arbitrary human-interface / USB-HID devices the OS doesn't model specially.
- **enumeration & matching**:
  · by VID / PID
  · by usage page / usage
  · transport (USB / Bluetooth / BLE-HID)
  · hotplug add/remove events
- **report descriptor**: descriptor fetch · report-item / usage parsing · report-id & length discovery
- **reports**:
  · input reports (read / event)
  · output reports (write)
  · feature reports (get / set)
  · numbered vs unnumbered report handling
- **raw transfer**: raw read/write bypassing parsing · report buffering / queue
- **usage tables**: standard usage-page lookup · vendor-defined usage pages
- **access gating**: permission / consent prompt (WebHID user gesture) · exclusive vs shared open · OS-claimed-device contention (keyboards/mice blocked)
↓under: libusb / WinUSB / IOHIDManager / hidraw raw paths; HID usage tables; exclusivity grabs.
apps: specialized peripherals (foot pedals, DJ decks, scientific instruments).
status: spawn (`hid` domain).
