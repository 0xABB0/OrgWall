# P6 stress — computational night / portrait (`compphoto`)

> reference app class: computational night + portrait, targets **ios + android**.
> features → cap-IDs → owning module; deny-without-fallback on a target platform = GAP.
> the design boundary under test: **camera gives bracket + OS comp-photo mode + frames; FUSION/stacking is downstream** (app/ML). A feature forcing a fusion cap *into* camera = scope leak.

## feature → cap → module

| feature | cap ids | owning module |
|---|---|---|
| exposure bracketing (auto + manual EV) | `cap.capture.bracket.exposure` · `cap.control.exposure.compensation-ev` · `cap.control.exposure.manual-time` · `cap.control.iso.manual` | cameraphoto · cameracontrol |
| focus bracketing | `cap.capture.bracket.focus` · `cap.control.focus.manual-lens-position-normalized` · `cap.control.focus.manual-distance-diopters` | cameraphoto · cameracontrol |
| bracket lens/OIS stabilization | `cap.capture.bracket.lensstabilization` | cameraphoto |
| Android Extensions explicit-select (HDR/Night/Bokeh/FaceRetouch) | `cap.capture.compphoto.selectable` | cameraphoto |
| extension strength set + active-type readout | `cap.capture.compphoto.strength` | cameraphoto |
| Windows-Studio-style property-controlled mode | `cap.capture.compphoto.propertycontrolled` | cameraphoto |
| Apple implicit mode (Night/SmartHDR/DeepFusion), observe-only | `cap.capture.compphoto.implicit` · `cap.control.flash.scene-detect` · `cap.meta.scene.nighthint` | cameraphoto · cameracontrol · cameradetect |
| deferred-photo-proxy (RETIRED ceiling) | `cap.capture.compphoto.deferred` `[trap: retired MEL-ENGINE-I]` | cameraphoto |
| ZSL | `cap.capture.zsl` | cameraphoto |
| ZSL reprocess (multi-frame reprocess) | `cap.capture.zsl.reprocess` | cameraphoto |
| multi-frame capture (burst of frames) | `cap.capture.burst` · `cap.capture.bracket.exposure` | cameraphoto |
| responsive/overlapped shutter | `cap.capture.responsive` | cameraphoto |
| sensor-merge HDR-still (in-stack) | `cap.capture.hdr.still.sensormerge` | cameraphoto |
| portrait/segmentation matte for bokeh | `cap.seg.matte.portrait` · `cap.seg.matte.semantic` · `cap.seg.mask.person` | cameradepth |
| depth for bokeh | `cap.depth.map.float` · `cap.depth.map.disparity` · `cap.depth.confidence` · `cap.depth.still` · `cap.depth.zoom.cofeasible` | cameradepth |
| live background blur/replace (the OS-rendered bokeh) | `cap.effect.background_blur` · `cap.effect.background_replace` · `cap.effect.segmentation_mask.deliver` | cameraeffects |
| RAW for multi-frame stacking | `cap.capture.raw.bayer` · `cap.capture.raw.fused` · `cap.capture.raw.format` · `cap.capture.raw.plusprocessed` | cameraphoto |
| per-lens constituent stills (multi-lens fusion source) | `cap.capture.constituentdelivery` | cameraphoto |
| bracket-frame ↔ EV-step correlation (tag which frame is which) | `cap.meta.bracketcorrelation` | camerameta |
| per-frame applied exposure/iso/ev/wb echo (stacking metadata) | `cap.meta.applied.*` · `cap.meta.state.*` | camerameta |
| scene night-hint / flicker / illuminance readout | `cap.meta.scene.nighthint` · `cap.meta.scene.illuminance` · `cap.control.catalog.scene-illuminance` | cameradetect · cameracontrol |
| multi-frame FUSION / stacking itself | — (no cap; downstream by design) | DOWNSTREAM (app/ML) |

## three-way controllability classification (the key stress)

The vocab expresses "the OS runs a mode the app can't control" **cleanly**, via four orthogonal IDs:

| platform | `.selectable` | `.propertycontrolled` | `.implicit` | `.strength` |
|---|---|---|---|---|
| ios (avf) | **deny** → fb `.implicit` | deny → fb `.implicit` | **native** | deny → fb `.implicit` |
| android camera2 | **native** | deny → fb `.selectable` | deny → fb `.selectable` | **native** |
| android camerax | native | deny → fb `.selectable` | deny → fb `.selectable` | emulate (cxinterop) |
| win32 (mf) | native | native | native | deny → fb `.selectable` |

Verdict: **clean.** An Apple implicit mode = `.selectable` reads `deny` (app cannot trigger it) while `.implicit` reads `native` (the mode still happens, observe-only). The `deny` on `.selectable` for ios carries fallback `→ .implicit`, i.e. "you can't pick it, but it runs and you can observe it" — exactly the semantics demanded. No "native, but…" annotation is forced; the four IDs split the controllability axis at the right grain. **Not a gap.**

`cap.capture.compphoto.deferred` is correctly recorded as a **retired ceiling**: `deny` on *every* axis (ios/android included) with note `cap-deferred:deferral retired per MEL-ENGINE-I; resolve synchronously` — not an active capability, kept in vocab as the honest ceiling per the append-only rule. **Correct treatment.**

## fusion boundary

Confirmed held. There is **no `cap.*.fusion` / `cap.*.stack` / `cap.*.merge` ID** anywhere in the vocab. The closest:
- `cap.capture.hdr.still.sensormerge` = *in-stack* multi-exposure merge **at the sensor/ISP** (a capture deliverable the OS performs — libcamera `HdrMode`, v4l2 `WIDE_DYNAMIC_RANGE`), not app-side stacking. On ios/android it `deny`s → `cap.capture.compphoto.implicit`/`.selectable`. Legitimately a camera cap (the OS does it), not a leak.
- `cap.capture.constituentdelivery` exposes `isVirtualDeviceFusionSupported` as a **delivery flag** (per-lens stills from a virtual device), not a fusion engine. Camera hands frames; app fuses.

The app-side fuse — HDR merge, focus-stack, night-stack — has **no cap**, by design. `20-beyond-os.md` already tags "HDR merge / focus-stack fusion `[down]`" and "histogram/waveform/zebra app-computed `[down]`". **No scope leak.**

## gaps

### (a) no cap → new cap-id
None. Every compphoto feature maps to an existing ID. The vocab's comp-photo §5 block + cameradepth seg/depth + camerameta echo cover the full feature set.

### (b) awkward scatter → P5
None rising to a cut-change. The feature legitimately assembles across cameraphoto (modes/bracket/raw/zsl) + cameracontrol (manual EV/ISO/focus) + cameradepth (matte/depth) + cameraeffects (rendered bokeh) + camerameta (per-frame echo) + cameradetect (night-hint) — this is MEL-ENGINE-IX composition working as intended, not scatter. A compphoto app links cameraphoto + cameracontrol + cameradepth + camerameta (+ cameraeffects if it wants OS-rendered blur); it does **not** drag in cameraptz/cameravirtual/camerastats. The split holds.

### (c) deny-without-fallback on a TARGET platform (ios/android) — the real findings

1. **`cap.meta.bracketcorrelation` — deny / fallback:none on BOTH ios AND android.**
   - ios avf `met-avfnobracketid` · android camera2/camerax `met-cam2nobracketid` — both "no bracket setting-ID correlation tag", fallback **none**.
   - This is the sharpest finding. The design's whole compphoto boundary is *"camera provides the bracket + the frames; fusion is downstream."* But on **neither** target platform can the camera tell the downstream stacker **which delivered frame corresponds to which EV/focus step**. The app must infer ordering from delivery sequence + the per-frame `cap.meta.applied.exposure`/`.ev` echo (which IS native on both). So the boundary is honored only if the downstream fuser reconstructs the bracket↔frame mapping itself — the camera cannot hand it a correlation key. Deny-without-fallback on both targets; the partial rescue (applied-EV echo) lives in a *different* cap and is inference, not a tag.

2. **`cap.seg.matte.portrait` / `.semantic` / `cap.seg.mask.person` — deny / fallback:none on android (`dep-noseg`).**
   - Standalone portrait/semantic mattes are **iOS-native, android-deny-without-fallback**. The portrait *render* on android is reachable only as a sealed OS effect via `cap.capture.compphoto.selectable`(Bokeh) — the app gets the *blurred result*, never the *matte* to composite itself. So "portrait/segmentation matte for bokeh" is a **target-platform asymmetry**: ios hands the matte (app renders bokeh its way); android hands only OS-rendered bokeh, no matte. Not a vocab gap (the cap exists and classifies cleanly), but a **deny-without-fallback cell on a primary target** — the app's portrait pipeline cannot be platform-uniform.

3. **`cap.depth.zoom.cofeasible` — deny / fallback `cap.depth.map.float` on android (`dep-nozoomco`); native ios only.**
   - Depth+zoom co-feasibility (needed for zoomed portrait bokeh) is avf-only. Android falls back to plain depth-map (loses the co-feasibility guarantee). Has a fallback, so softer — but it means a zoomed-portrait feature behaves differently across the two targets.

4. **`cap.capture.compphoto.implicit` on macos = `?` (unconfirmed)** — not a target platform (targets are ios+android), so out of scope for this stress, but flagged: ios is `native`, android is `deny`→`.selectable`, both clean; the `?` is macos-only.

## conclusion

- **New caps needed: NO.** Vocab covers the full compphoto feature set; the three-way controllability split is the cleanest part of the design.
- **Cut changes needed: NO.** Cross-module assembly is intended composition.
- **Real findings are deny-without-fallback cells on the target platforms**, not missing vocab. The design correctly stops at the fusion boundary — but the boundary exposes that the *handoff key* (bracketcorrelation) and the *self-composite matte* (seg.matte on android) are absent on the very platforms the app ships to. These are honest ceilings (MEL-ENGINE-I keeps them as deny rows), but they mean the downstream fuser carries more inference burden than the boundary narrative implies.
