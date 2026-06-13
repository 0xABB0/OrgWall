# camera — freeze v1

- **version: v1.** Frozen scope = **589 capability IDs** (`30-vocabulary.md`) × **12 axis-columns** (`40-matrix.csv`), cut into **13 modules** (`50-planes.md`). The vocab — not any module list — is the contract.
- **append-only.** A frozen cap-ID is permanent: never rename, never reuse a slot, never repurpose. A capability that is dropped stays in the vocab marked deprecated — its ID is never recycled. A new capability is a **new ID**. Cap #590 = +1 vocab row = +12 matrix cells = +1 line in one module's cap-set; it is **not** an ABI break.
- **descriptor extensibility.** The public API per module is fixed and small — `open` / `configure` / `start` / `stop` / `query-caps`. Capabilities are **runtime-enumerable DATA** (cap-ID + typed value), never compiled-in API surface. Descriptors (device, format, frame, control-value) carry an explicit `size`/`version` head; growth = append trailing fields, old readers ignore what they don't know. An unknown cap-ID on an older impl → `deny` (forward-compatible by construction). Adding an axis = +1 matrix column = +N rows; consumers that never query the new caps are unaffected.
- **changelog format.** One line per change, in `00-charter.md`'s changelog: `v1.M — <+cap-id area/module | −cap-id deprecated | reclassify cap-id@axis old→new (note) | +axis-column>`.

## frozen invariants the wireframe must honor (cross-plane seams — `50-planes.md` §seams)
- **provider/virtual pattern** — host OS = provider 0; externals/virtuals/machine-vision/**published-software-cams**/bare-sensors register via one provider face. Shared infra under cameradevice (ingest) + cameravirtual (egress); the egress↔ingest loop closes through `cap.device.class.software`.
- **raw-UVC + sensor-register access** — cameradevice owns the under-OS/bare-metal gateways (`cap.device.access.{raw_under_os,extension_unit,sensor_register}` + `cap.device.bringup`); cameracontrol/cameraphoto consume them for `emulate(raw-UVC)` cells.
- **shared capture clock** — cameracapture owns `cap.timing.av-clock.*`/`.av-sync.*` (the single A/V seam to the audio domain) + session lifecycle (`cap.os.session.*`) + the per-frame metadata-bag channel cameradetect/camerastats read.
- **depth is a feature, not a module** — assembled across cameracalib (geometry) + cameradepth (streams) + cameraeffects (portrait matte), all riding cameracapture's buffers.
- **transport-neutral controls** — `cap.control.*`/`cap.ptz.*` IDs carry no transport; a future VISCA/NDI control binding exposes the same IDs additively.

## open at freeze (do NOT block — resolve during wireframe/impl)
- **71 `?` cells** in the matrix = research-scheduled classification uncertainties (macОS-vs-iOS divergences, deprecated keys, embedded-Linux subdev reachability, ISP-SoC-variant lifts), never vocab gaps. Resolve per-cell when the owning module is wireframed; each resolution is a `reclassify@axis` changelog line.
- **cosmetic** — `40-matrix.csv` note column mixes keyed (`40-matrix-notes.md`) and inline notes across P4 fragments; every cell is documented. Normalize opportunistically, not gating.

## handoff → wireframe
Each module in `50-planes.md` → **one `wireframe` trio** (spec.md · public header · usage example); the module's frozen cap-set is its fixed scope. Smallest-dep first:

1. **cameradevice** (78) — device-lifecycle: enumerate/open/hotplug/topology/streams/raw+sensor access/provider face. *Foundation — everything depends on it.*
2. **cameracapture** (90) — data: frames (memory/zerocopy/pool/map/drop) + timing/clocks/av-sync + session lifecycle. ← cameradevice; composes `image`/`gpu`/`color`/audio-clock.
3. **cameracontrol** (128) — continuous sensor/ISP/lens control. ← cameradevice.
4. **cameraptz** (20) — mechanical PTZ. ← cameradevice. *opt-in.*
5. **cameraphoto** (66) — capture modes (RAW/comp-photo/bracket/encode/trigger). ← cameradevice. *opt-in.*
6. **cameracalib** (12) — optical geometry (intrinsics/extrinsics/distortion). ← cameradevice. *opt-in.*
7. **camerapolicy** (46) — OS consent/lifecycle/arbitration/orientation/privacy/thermal. ← cameradevice. *opt-in.*
8. **cameradepth** (27) — depth/IR/spatial sensing streams. ← cameracapture + cameracalib. *opt-in.*
9. **camerameta** (42) — emitted readout (applied-3A echo, access channel, embedded lines). ← cameracapture. *opt-in.*
10. **cameraeffects** (31) — OS-computed scene effects + segmentation. ← cameracapture. *opt-in.*
11. **cameradetect** (14) — OS-emitted detection metadata (faces/codes/scene). ← camerameta (bag access). *opt-in.*
12. **camerastats** (8) — sensor/ISP statistics maps + sensor-temp. ← camerameta. *opt-in.*
13. **cameravirtual** (27) — egress/publish + test source. ← cameradevice (provider) + cameracapture (frame format). *opt-in.*

Ship profiles (the cut's payoff): live-preview video = 1+2+3; glasses-shot embedded = 1+2+3+5; QR scanner = 1+2+11; OBS-class = 1+2+3+13.

On ship (MEL-SPEC-002): each module's frozen vocab slice migrates into that module's own `spec.md`/docs; the `design/camera/` scaffolding (00–70) is pruned once every module is wireframed.
