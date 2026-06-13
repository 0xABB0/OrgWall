# P6 stress — embedded smart-glasses shot-on-button (`embedded` / `embedded+baresensor`)

> Reference app: a bare-sensor smart-glasses firmware (RTOS/bare-metal MCU-SoC, no OS camera HAL) that brings a sensor up, configures the CSI-2/DVP link, sets exposure/gain by I²C register, receives raw Bayer into host-owned DMA buffers, and on a GPIO-button press takes a single still. + machine-vision inspection variant.
> Stress key: confirm the WHOLE glasses-shot path maps to **cameradevice + cameracapture + cameracontrol + cameraphoto** only (the embedded ship-profile, `50-planes.md`), native on the `embedded+baresensor` column.
> Caps + modules cited verbatim from `30-vocabulary.md` / `50-planes.md`; statuses verbatim from `40-matrix.csv` (`embedded,baresensor`).

## feature → cap → module

| feature | cap ids | owning module |
|---|---|---|
| **bring sensor up** — XCLK master clock + PLL/clock-tree + PWDN/RESET power-up sequence | `cap.device.bringup` (native) | cameradevice |
| sensor PLL → pixel-clock / link-freq | `cap.timing.device-clock.rate` (native, bs-manual); `cap.timing.frame-rate.clamp` (native) | cameracapture |
| probe sensor identity (chip-ID register) | `cap.enum.list` (native); `cap.device.id.stable` (native, bs-sensorchar); `cap.device.class.physical` (native, bs-sensorchar) | cameradevice |
| query the single static sensor's caps | `cap.enum.caps.per_device` (native) | cameradevice |
| **configure CSI-2 / DVP link** — lanes / link-freq / VC / data-type; DVP bus-width / PCLK-edge / sync | `cap.stream.channel.transport.config` (native) | cameradevice |
| open / start / stop the one sensor→pipe | `cap.topology.session.graph` (native) | cameradevice |
| **direct register R/W** (I²C/SCCB) | `cap.device.access.sensor_register` (native) | cameradevice |
| **manual exposure-time** by register | `cap.control.exposure.manual-time` (native) | cameracontrol |
| **manual analog gain** by register | `cap.control.gain.analog-manual` (native) | cameracontrol |
| **manual digital gain** by register | `cap.control.gain.digital-manual` (native) | cameracontrol |
| **select readout** — binning | `cap.capture.readout.binning` (native) | cameraphoto |
| select readout — decimation/skipping | `cap.capture.readout.decimation` (native) | cameraphoto |
| select readout — crop / windowing (single ROI) | `cap.capture.readout.cropregion` (native) | cameraphoto |
| select readout — pixel-mode (full-res vs binned) | `cap.capture.readout.pixelmode` (native) | cameraphoto |
| **receive raw Bayer frames** | `cap.capture.raw.bayer` (native); `cap.frame.format.bayer8/10/12/14/16` (native); `cap.frame.format.bayer.compressed` (native) | cameraphoto / cameracapture |
| (mono glasses/MV variant) mono frame | `cap.frame.format.gray8` / `cap.frame.format.gray_highbit` (native) | cameracapture |
| **host-owned DMA buffers** (allocator) | `cap.frame.pool.allocate` (native); `cap.frame.pool.queue_depth` (native); `cap.frame.pool.recycle` (native); `cap.frame.pool.lifecycle_events` (native) | cameracapture |
| CPU map + plane layout/stride | `cap.frame.map.cpu` (native); `cap.frame.map.plane_layout` (native) | cameracapture |
| dequeue next frame (get/return loop) | `cap.frame.deliver.next` (native) | cameracapture |
| drop policy (latest / block) + signal/reason/stats | `cap.frame.drop.policy_latest` / `.policy_block` / `.signal` / `.reason` / `.stats` (native) | cameracapture |
| app tag carried with a buffer | `cap.frame.cookie` (native) | cameracapture |
| **GPIO button → single still** (camera side = the trigger, not the wiring) | `cap.capture.trigger.hardwareline` (native); `cap.capture.trigger.software` (native); `cap.capture.trigger.acquisitioncontrol` (native) | cameraphoto |
| take the still itself | `cap.capture.still` (native); `cap.capture.still.grabframe` (native, bs-manual) | cameraphoto |
| **strobe / LED pin** asserted during exposure | `cap.control.flash.strobe-output` (native) | cameracontrol |
| **per-frame timestamp** from hardware timer | `cap.timing.frame.timestamp` (native); `.clock-domain-id` (native); `.capture-point` (native) | cameracapture |
| per-frame frame-counter (drop detection) | `cap.timing.frame.sequence-id` (native) | cameracapture |
| VSYNC / frame-done event | `cap.timing.frame-sync-event` (native) | cameracapture |
| rolling-shutter skew (derived) | `cap.timing.rolling-shutter.skew` / `.line-time-derived` (native) | cameracapture |
| test-pattern (bring-up without optics) | `cap.control.isp.test-pattern` (native) | cameracontrol |
| **multi-sensor genlock** (stereo/MV rig, XVS/XHS) | `cap.timing.genlock.sync-pins` (native) | cameracapture |
| CSI-2 VC/DT multiplex (image VC0 + embedded-data) | `cap.stream.source.multivs` (native, bs-vcdt) | cameradevice |
| **embedded sensor-metadata lines** (CSI-2 DT 0x12 register dump) | `cap.meta.embedded.sensorlines` (native) | **camerameta** ⚠ |
| **applied exposure/gain echo** (register read-back) | `cap.meta.applied.exposuretime` / `.iso` / `.analogdigitalgain` / `.frameduration` / `.blacklevel` / `.cropregion` / `.binningreadout` / `.geometryecho` (native, bs-regreadback) | **camerameta** ⚠ |
| per-frame metadata bag / rawblob channel | `cap.meta.access.bag` / `.rawblob` (native) | **camerameta** ⚠ |
| **session start/stop / running-state** (`video_stream_start/stop`) | `cap.os.session.start` / `.stop` / `.running_state_query` (native, bs-firmwaresession) | **camerapolicy** ⚠ |
| **GPIO shutter-button surface** (the camera-side capture-input) | `cap.os.capture_input.shutter_button` (native, bs-gpio) | **camerapolicy** ⚠ |
| sensor-mount orientation / flip / facing | `cap.os.orientation.sensor_mount` / `.output_rotation_apply` / `.front_mirror` / `.facing_hint` (native, bs-flip/bs-sensorchar) | **camerapolicy** ⚠ |
| (mono MV variant) IR/NIR mono stream | `cap.ir.stream` / `cap.ir.cfa.nir` (native, bs-mono) | **cameradepth** ⚠ |
| strobed-IR illumination flag | `cap.meta.illumination.ir` (native, bs-manual) | **camerameta** ⚠ |
| sensor pixel-pitch (datasheet constant, MV calib) | `cap.calib.pixelsize` (native, bs-sensorchar) | **cameracalib** ⚠ |
| test-pattern as synthetic source | `cap.testsrc.test_pattern` (native, bs-testpat) | **cameravirtual** ⚠ |

## gaps

### (a) new caps — none
The bare-sensor reopening already minted `cap.device.bringup` + `cap.device.access.sensor_register` + broadened `cap.stream.channel.transport.config` (CSI-2/DVP). The glasses-shot path exercises **nothing the bare-sensor inventory didn't already cover**. No NEEDED feature lands without a cap. The button→shot is fully covered by `cap.capture.trigger.hardwareline` / `.software` + `cap.capture.still` — the camera exposes the trigger; the GPIO/button wiring itself is correctly app/board, not a camera cap. The demosaic/3A boundary holds: camera stops at `cap.capture.raw.bayer` (native) + manual register control + the new `cap.device.bringup`/`.sensor_register`; demosaic and any auto-exposure loop are downstream (every `cap.control.exposure.ae-*` / `cap.control.3a.*` / ISP-tone cap reads `deny` with fallback to the manual sibling — `bs-noae`/`bs-no3a`/`bs-noisp`).

### (b) ship-profile gaps — P5 recheck (the real finding)
The 4-module ship-profile (`cameradevice + cameracapture + cameracontrol + cameraphoto`) does **not** cover the full NEEDED glasses-shot path. Features that classify **native** on `embedded+baresensor` but whose owning module is outside the 4:

- **embedded sensor-metadata lines** `cap.meta.embedded.sensorlines` ⇒ **camerameta** is a 5th linked module (P5)
  — explicitly named as a NEEDED glasses feature ("embedded sensor-metadata lines"); native via CSI-2 DT 0x12. Owned by camerameta per `50-planes.md`.
- **applied exposure/gain echo** `cap.meta.applied.{exposuretime,iso,analogdigitalgain,frameduration,blacklevel,cropregion,binningreadout,geometryecho}` ⇒ **camerameta** (P5)
  — the register read-back that confirms what was written (bs-regreadback). The natural "what exposure did I actually get" feedback for a manual-control firmware. Owned by camerameta.
- **per-frame metadata channel** `cap.meta.access.bag` / `cap.meta.access.rawblob` ⇒ **camerameta** (P5)
  — the carrier the embedded line + applied echo ride on.
- **session start/stop + GPIO shutter-button + mount-orientation** `cap.os.session.start`/`.stop`/`.running_state_query`, `cap.os.capture_input.shutter_button`, `cap.os.orientation.*` ⇒ **camerapolicy** (P5)
  — `video_stream_start/stop` IS the session start/stop (bs-firmwaresession); the GPIO shutter-button is `cap.os.capture_input.shutter_button` (bs-gpio) — and the prompt's own button→shot feature maps here, not only to `cap.capture.trigger.*`. Owned by camerapolicy.
- **(MV/mono variant)** IR/NIR mono stream `cap.ir.stream`/`cap.ir.cfa.nir` ⇒ **cameradepth** (P5); strobed-IR flag `cap.meta.illumination.ir` ⇒ **camerameta** (P5); pixel-pitch `cap.calib.pixelsize` ⇒ **cameracalib** (P5); test-pattern-as-source `cap.testsrc.test_pattern` ⇒ **cameravirtual** (P5).
  — the machine-vision-inspection + mono-glasses variant pulls four more modules. For RGB-Bayer glasses these are optional; for the MV-mono variant `cap.ir.stream` is the primary delivery path and is NOT optional.

**Resolution choices for P5 (recheck owns the call):**
1. *Cut-move* the bare-metal-essential caps into the core 4 — minimally `cap.meta.embedded.sensorlines` + `cap.meta.applied.*` + `cap.meta.access.{bag,rawblob}` into cameracapture (they already ride its frame-attachment channel, per camerameta's own plane note), and `cap.os.session.*` + `cap.os.capture_input.shutter_button` + `cap.os.orientation.*` into cameradevice/cameracapture. This keeps a true 4-module firmware. OR
2. *Widen the ship-profile note* in `50-planes.md` from 4 modules to acknowledge camerameta (+ camerapolicy for session/orientation; + cameradepth/cameracalib/cameravirtual for the MV-mono variant) as linked on `embedded+baresensor`. The current note ("links cameradevice + cameracapture + cameracontrol + cameraphoto only — every other module's caps read `deny`") is **factually wrong**: camerameta/camerapolicy/cameradepth/cameracalib/cameravirtual each carry ≥1 **native** cell on this column (14 cap.meta, 8 cap.os, 2 cap.ir, 1 cap.calib, 1 cap.testsrc).

Recommendation: option 1 for the bare-metal-essential subset (sensorlines + applied-echo + access channel + session/shutter/orientation), since these are not optional for any bare-sensor firmware and "deny→downstream" is false for them; revisit the MV-mono extras (ir/calib/testsrc) as genuinely opt-in.

### (c) deny-without-fallback on a NEEDED glasses feature — none
Every `deny` cell on the NEEDED path carries a fallback (no MEL-ENGINE-VII violation for any glasses feature):
- `cap.control.flash.torch-mode`/`.torch-strength` deny (bs-noflash) ⇒ fallback `cap.control.flash.strobe-output` (the native sensor STROBE pin — exactly what the glasses strobe needs). ✓
- `cap.capture.stillpipe.{method,trigger,hwbutton}` deny (bs-nouvc) — UVC-specific; not the bare-sensor path. Trigger covered natively by `cap.capture.trigger.hardwareline`/`.software`. ✓ (no fallback annotated because the bare-sensor trigger is a *different native cap*, not a fallback — acceptable: the feature is covered, just by a sibling.)
- `cap.capture.sequencer` deny (bs-noseq) ⇒ fallback none, but sequencer/bracket is NOT a glasses-shot feature (app re-writes exposure between frames). ✓ not needed.
- `cap.timing.device-clock.{pts,scr-sof}` deny (bs-nouvc) — UVC clock surface; the glasses timestamp uses `cap.timing.frame.timestamp` (native hardware-timer). ✓ not needed.

deny-without-fallback cells that exist on the column but are **not** glasses features (correct denies, no gap): `cap.capture.sequencer` (bs-noseq), the full `cap.effect.*` / `cap.egress.*` / comp-photo / ZSL families (bs-noeffect/bs-noegress/bs-nocompphoto/bs-nozsl) — all correctly deny→none and none are exercised by this app.

### `?` cells touched by the path (research-scheduled, not gaps)
- `cap.frame.zerocopy.dmabuf*` `?` (bs-dmabuf-q) — DMA-buffer handoff: native CPU-map path (`cap.frame.map.cpu`) + app-owned import (`cap.frame.zerocopy.import_external`) cover the allocator requirement; dmabuf only if the SoC has a DRM/GPU stack. Not blocking.
- `cap.capture.readout.shuttermode` `?` (bs-shuttermode-q) — shutter type is fixed per sensor (global IMX296/AR0234 vs rolling), not a runtime select; glasses pick the part, don't toggle. Not needed at runtime.
- `cap.meta.access.selectable` `?` (bs-embedded-q), `cap.timing.device-clock.timestamp-counter` `?` (bs-devcounter-q), `cap.capture.readout.multiroi` `?` (bs-multiroi-q) — part-dependent; none on the core glasses-shot path.

## verdict
- **anything new vs the bare-sensor reopening? NO.** The glasses app exercises no cap the bare-sensor inventory didn't already add; zero new cap-IDs.
- **does the glasses-shot path fit the 4-module ship-profile? NO** — the bring-up→register-control→Bayer-frame→button-trigger→still spine fits cameradevice + cameracapture + cameracontrol + cameraphoto, BUT the NEEDED embedded-metadata, applied-echo, session/shutter-button, and orientation features are native and owned by camerameta + camerapolicy (and the MV-mono variant pulls cameradepth/cameracalib/cameravirtual). The `50-planes.md` ship-profile note (4 modules only, all else deny) is contradicted by 26 native cells outside the 4. → **P5 recheck.**
