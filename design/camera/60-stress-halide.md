# camera — P6 stress: Halide / Obscura (pro stills, ios + macos)

> Reference app: Halide Mark II / Obscura — pro manual stills. Method: decompose each demanding feature → cap-IDs (verbatim §30) → owning module (§50). Confirm avf native/emulate on ios+macos (§40). App-computed overlays must resolve to "camera delivers full-bit-depth frame, rest is downstream" (§20 capture-vs-compute line).

## feature → cap → module

| feature | cap ids | owning module |
|---|---|---|
| RAW still — pure sensor Bayer | `cap.capture.raw.bayer` | cameraphoto |
| RAW still — ProRAW (post-ISP fused DNG, not Bayer) | `cap.capture.raw.fused` | cameraphoto |
| selectable RAW pixel-format / bit-depth | `cap.capture.raw.format` | cameraphoto |
| selectable RAW codec / compression | `cap.capture.raw.codec` | cameraphoto |
| RAW + embedded thumbnail / sidecar | `cap.capture.raw.sidecar` | cameraphoto |
| RAW + processed from one shot | `cap.capture.raw.plusprocessed` | cameraphoto |
| DNG container still | `cap.capture.encodedstill.dng` | cameraphoto |
| HEIC processed still | `cap.capture.encodedstill.heic` | cameraphoto |
| EXIF / orientation / thumbnail embed | `cap.capture.encodedstill.embeddedmeta` | cameraphoto |
| GPS tag embed | `cap.capture.encodedstill.embeddedgps` | cameraphoto |
| fire still | `cap.capture.still` | cameraphoto |
| high-res still dims ≠ preview (still WHILE previewing) | `cap.capture.still.maxdims` · `cap.capture.stillduringvideo` | cameraphoto |
| speed-vs-quality bias + the `photoQualityPrioritization` silent-override trap | `cap.capture.still.qualityprioritization` | cameraphoto |
| ZSL | `cap.capture.zsl` | cameraphoto |
| overlapped/responsive shutter + readiness | `cap.capture.responsive` | cameraphoto |
| exposure (EV) bracket | `cap.capture.bracket.exposure` | cameraphoto |
| focus (lens-position) bracket | `cap.capture.bracket.focus` | cameraphoto |
| OIS stabilization across a bracket | `cap.capture.bracket.lensstabilization` | cameraphoto |
| per-lens stills from a virtual multi-lens device | `cap.capture.constituentdelivery` | cameraphoto |
| manual exposure time (shutter) | `cap.control.exposure.manual-time` | cameracontrol |
| manual ISO | `cap.control.iso.manual` | cameracontrol |
| AE lock | `cap.control.exposure.ae-lock` | cameracontrol |
| exposure bias (EV compensation) | `cap.control.exposure.compensation-ev` | cameracontrol |
| metered-EV-offset readback | `cap.control.exposure.metered-offset` | cameracontrol |
| AE max-shutter clamp | `cap.control.exposure.ae-max-time-clamp` | cameracontrol |
| manual focus (normalized lens-position) | `cap.control.focus.manual-lens-position-normalized` | cameracontrol |
| AF mode / single-shot / locked | `cap.control.focus.af-mode` · `cap.control.focus.af-trigger` | cameracontrol |
| manual WB — Kelvin | `cap.control.wb.manual-temperature-kelvin` | cameracontrol |
| manual WB — tint | `cap.control.wb.manual-tint` | cameracontrol |
| manual WB — RGB gains | `cap.control.wb.manual-gains-rgb` | cameracontrol |
| WB lock | `cap.control.wb.lock` | cameracontrol |
| zoom factor (lens framing) | `cap.control.zoom.ratio` | cameracontrol |
| lens/sensor switching — enumerate constituents | `cap.topology.logical.constituents.enumerate` | cameradevice |
| lens/sensor switching — switchover zoom factors | `cap.topology.logical.switchover.zoomfactors` | cameradevice |
| lens/sensor switching — active-physical readout | `cap.topology.logical.constituent.activephysical` | cameradevice |
| Apple Log capture | `cap.capture.hdr.video.log` | cameraphoto |
| wide gamut (P3/BT.2020) | `cap.capture.hdr.video.widegamut` | cameraphoto |
| 10-bit HDR | `cap.capture.hdr.video.tenbit` | cameraphoto |
| high / standard fps (120/240) | `cap.capture.highspeed` · `cap.timing.frame-rate.clamp` | cameraphoto · cameracapture |
| depth / portrait still | `cap.depth.still` · `cap.seg.matte.portrait` · `cap.calib.delivery` | cameradepth · cameracalib |
| **focus peaking** (app-computed) | `cap.frame.format.bayer14`/`cap.frame.format.gray_highbit` + `cap.frame.map.cpu` + `cap.frame.map.plane_layout` + `cap.frame.deliver.next` | cameracapture (rest **[down]**) |
| **zebra** (app-computed) | same per-frame full-bit-depth path | cameracapture (rest **[down]**) |
| **histogram** (app-computed) | same per-frame full-bit-depth path | cameracapture (rest **[down]**) |
| **false-color** (app-computed) | same per-frame full-bit-depth path | cameracapture (rest **[down]**) |

## app-computed overlays — capture-vs-compute resolution

Per §20: no mainstream pro stills app relies on camera-emitted statistics; **peaking · zebra · histogram · false-color are all app-computed `[down]` from frames**. The camera-scope obligation is exactly: deliver per-frame full-bit-depth pixels the app can read on the CPU. The design delivers it:
- format deliverability — `cap.frame.format.bayer14` / `cap.frame.format.gray_highbit` / the full `cap.frame.format.*` set + `cap.frame.format.enumerate` / `.select` (cameracapture).
- CPU read — `cap.frame.map.cpu` + `cap.frame.map.plane_layout` (native ios+macos).
- delivery loop — `cap.frame.deliver.next` + `cap.frame.drop.policy_latest` (native).

All native on ios+macos. **No gap** — the raw per-frame data the overlays need is fully delivered; the visualization math is correctly downstream, not a camera cap.

## the `photoQualityPrioritization` trap — confirmed covered

The silent-override trap (`photoQualityPrioritization` defaults `.balanced` and overrides manual ISO/shutter unless set `.speed`) maps to `cap.capture.still.qualityprioritization` (native both) — a distinct cap from the partial-manual `cap.control.exposure.priority-mode` (shutter/aperture-priority AE, deny-both-avf, **not exercised by Halide** which does full manual). The override is a configure-value hazard, not a missing cap; honoring MEL-CODE-007 (no silent defaults) the wireframe must force-surface this cap rather than default it. **No vocab gap; flagged as a wireframe contract obligation.**

## ios vs macos divergences (deny cells touched by Halide features)

| cap | ios | macos | fallback |
|---|---|---|---|
| `cap.capture.hdr.video.log` (Apple Log) | native | **deny** (`API_UNAVAILABLE(macos)`) | `cap.capture.hdr.video.widegamut` (native) — wide-gamut/10-bit, no log curve |
| `cap.capture.raw.fused` (ProRAW) | native | **?** (`cap-iosonly`: macOS Apple-silicon unconfirmed) | `cap.capture.raw.bayer` (native both) |
| `cap.depth.still` | native | **deny** (`dep-macdepth`: no depth-producing hw) | none (hard hw-gated) |
| `cap.seg.matte.portrait` | native | **deny** (`dep-iosmatte`: `API_UNAVAILABLE(macos)`) | none (hard hw-gated) |
| `cap.calib.delivery` | native | **deny** (`dep-macdepth`) | none (hard hw-gated) |
| `cap.topology.logical.switchover.zoomfactors` | native | **deny** (`top-avf-switchover-nomacos`: iOS-only) | `cap.control.zoom.ratio` (native) — manual zoom, no auto lens-switch factors |

Everything else Halide exercises (RAW format/codec/sidecar/plusprocessed, full manual exposure/ISO/focus/WB + locks/bias, still maxdims, stillduringvideo, qualityprioritization, ZSL, responsive, exposure bracket, lens-stabilization, constituent-delivery, wide-gamut, 10-bit, highspeed, all `cap.frame.*`/`cap.timing.*`) is **native on both ios and macos**.

## gaps

**Nothing new is exercised.** Every Halide feature decomposed cleanly to an existing cap-ID in an existing module. No feature lacked a cap (no P3 add); no awkward cross-module scatter (the control/photo/topology split is exactly the design's control-sibling cut — manual params in cameracontrol, capture modes in cameraphoto, lens-decomposition in cameradevice, frames in cameracapture; assembled across modules per MEL-ENGINE-IX, the intended shape); no deny-both-without-fallback that Halide relies on.

Specific resolutions:
- **app-computed overlays (peaking/zebra/histogram/false-color)** ⇒ resolve to `cap.frame.* + cap.frame.map.cpu` delivered native both, rest `[down]`. Design delivers the raw per-frame data. **No gap.**
- **`photoQualityPrioritization` trap** ⇒ `cap.capture.still.qualityprioritization` (native). **No new cap**; wireframe obligation to force-surface, not default (MEL-CODE-007).
- **`cap.control.exposure.priority-mode`** is deny-both-avf with fallback-none, but **Halide does not use it** (it does full manual, not shutter/aperture-priority AE) — not a Halide gap.
- **deny-without-true-fallback on macos** (`cap.depth.still`, `cap.seg.matte.portrait`, `cap.calib.delivery`) is **hardware-gated honesty** (MEL-ENGINE-VII: macOS built-in/Continuity cams vend no depth hw) — the cap stays native on ios, deny is the honest alternative, not a design hole. Halide's depth/portrait feature is an iOS feature; on macOS it correctly degrades to non-depth stills.

### verdict
Halide is a **strong manual-stills exerciser but adds no new ceiling** beyond what §20's pro-mobile-capture teardown already folded into the vocab. The capture-vs-compute line, the RAW Bayer-vs-fused split, the `photoQualityPrioritization` trap, multicam cost budget, ZSL/SIS, and depth+matte sync were all already harvested from the Halide/Obscura/FiLMiC class. The design absorbs it whole.

### harder stills app to exercise next
To find an actual gap, stress a **computational-RAW / multi-frame-stack pipeline that reaches deeper than AVFoundation**, e.g. **Adobe Lightroom (mobile) long-exposure / HDR-DNG**, **ProCamera**, or — to actually break new ground — a **Linux/embedded astrophotography stacker (SharpCap / N.I.N.A. / KStars-Ekos)** driving **GenICam/INDI machine-vision cameras**: hardware sequencer, user-sets, multi-ROI readout, `cap.stream.transfer.usercontrolled`, PTP-scheduled triggers, `cap.capture.trigger.hardwareline` — the `[CEILING]` industrial caps that no consumer-OS / pro-mobile app touches. That is where a deny-without-future-provider-plane could surface a real P5 reconsideration.
