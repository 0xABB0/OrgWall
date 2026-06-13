# P6 — stress-test: OBS Studio (slug `obs`, targets macos/win32/linux)

> Method: decompose OBS's demanding feature set → map each to verbatim cap-IDs (30-vocabulary) → owning module (50-planes) → confirm in 40-matrix that the caps are native/emulate on ≥1 impl per target; flag deny-on-all-targets-without-fallback. Source teardown: 20-beyond-os §"OBS Studio" + §"Pro capture cards".

## verdict
OBS exercised **new** ground (y). The continuous-control / capture / clock-sync / publish / zero-copy / hot-plug surface is **fully covered** — every feature maps to an existing cap that is native-or-emulate on ≥1 impl per target. Three demanding features hit **no matching cap**, all in the **capture-card ingest** path (DeckLink/AJA/Magewell — which OBS uses cross-platform): HDR-on-ingest, input-signal-format auto-detect, and OS-hidden-vcam discoverability. Plus one deny-on-all-targets-without-fallback cluster (HDR-video).

---

## feature → cap-id → owning module

| feature | cap ids | owning module |
|---|---|---|
| enumerate cameras / UVC webcams | `cap.enum.list` · `cap.device.class.external.uvc` · `cap.device.id.stable` · `cap.device.name.human` | cameradevice |
| enumerate capture-cards (HDMI/SDI as camera) | `cap.device.class.capture_card` | cameradevice |
| enumerate **existing OS virtual cams** | `cap.enum.list` · `cap.device.class.virtual` (avf only) | cameradevice |
| surface OS-**hidden** virtual/screen cams (CMIO AllowScreenCaptureDevices) | **— no cap —** | **(gap)** |
| open/stream many sources at once (multi-source, uncoordinated) | `cap.stream.multi.concurrent` · `cap.topology.session.graph` ; (coordinated multicam: `cap.topology.multicam.concurrent` · `cap.enum.concurrent_sets`) | cameradevice |
| hot-plug arrival/removal | `cap.device.hotplug` · `cap.os.hotplug.arrival` · `cap.os.hotplug.removal` · `cap.device.connected_state` | cameradevice · camerapolicy |
| auto-reconnect (win OnReactivate / linux udev) | `cap.device.connected_state` + hotplug events (re-open is app loop over them) | cameradevice · camerapolicy |
| **per-source clock rebasing** | `cap.timing.av-sync.rebase` · `cap.timing.av-sync.device-ts-trust` | cameracapture |
| **jitter buffer** (get_closest_frame, 2ms slack) | `cap.timing.av-sync.jitter-buffer` | cameracapture |
| **ts-jump recovery** (>2s reset+flush) | `cap.timing.av-sync.ts-jump-recovery` | cameracapture |
| **A/V sync-offset** (+TS_SMOOTHING 70ms) | `cap.timing.av-sync.offset` | cameracapture |
| shared capture clock (A+V one time-base) | `cap.timing.av-clock.shared-session` · `.cross-output-map` · `.app-timebase` | cameracapture |
| per-frame timestamps + clock domain | `cap.timing.frame.timestamp` · `.clock-domain-id` · `.sequence-id` | cameracapture |
| per-device buffered/unbuffered | `cap.frame.drop.policy_latest` · `.policy_block` · `cap.frame.pool.queue_depth` | cameracapture |
| dropped/late/lost frame accounting | `cap.frame.drop.signal` · `.reason` · `.stats` | cameracapture |
| wide YUV/RGB 8-bit formats | `cap.frame.format.yuv.{packed422_8,semiplanar420_8,planar420_8}` · `cap.frame.format.rgb.packed8` | cameracapture |
| 10-bit container (P010 / V210 / P210) | `cap.frame.format.yuv.semiplanar420_hdr` · `.packed422_hdr` · `cap.frame.format.rgb.hdr10bit` | cameracapture |
| HDR transfer **PQ/HLG** (camera-capture sense) | `cap.capture.hdr.video.hlg` · `.hdr10` · `.widegamut` | cameraphoto |
| **HDR static metadata on ingest** (MaxCLL/MaxFALL/mastering display, capture-card) | **— no cap —** | **(gap)** |
| range/space/matrix from signal | (out of camera scope — `image`/`color` own colorimetry; §14c) | image · color |
| zero-copy IOSurface (mac) | `cap.frame.zerocopy.iosurface` | cameracapture |
| zero-copy **DMA-BUF + DRM modifiers + explicit-sync** (PipeWire) | `cap.frame.zerocopy.dmabuf` · `.modifiers` · `.explicit_sync` | cameracapture |
| zero-copy D3D11 (win) | `cap.frame.zerocopy.d3d11` | cameracapture |
| CPU path (win/V4L2) + plane layout | `cap.frame.map.cpu` · `cap.frame.map.plane_layout` | cameracapture |
| device-control walk: exposure/focus/WB/brightness | `cap.control.catalog.introspect` · `cap.control.exposure.manual-time` · `cap.control.focus.af-mode` · `cap.control.wb.mode` · `cap.control.isp.brightness` | cameracontrol |
| **input-signal-format auto-detect + change events** (DeckLink format-changes-underneath-you) | **— no cap —** | **(gap)** |
| per-frame SMPTE timecode (capture-card) | `cap.timing.timecode.smpte` | cameracapture |
| SDI genlock / house-reference | `cap.timing.genlock.sdi-reference` `[CEILING]` | cameracapture |
| SDI embedded multi-channel audio on shared clock | `cap.timing.av-clock.shared-session` (SDI slice) → `audiocapture` seam (§14c) | cameracapture |
| **publish composited surface as OS-wide vcam** | `cap.egress.publish` · `.frame_push` · `.frame_pull` | cameravirtual |
| — install ceiling: CMIOExtension (signed sysext) | `cap.egress.install.systemext` | cameravirtual |
| — install ceiling: MFCreateVirtualCamera / DShow (admin COM reg) | `cap.egress.install.com_admin` | cameravirtual |
| — install ceiling: v4l2loopback (kernel module) | `cap.egress.install.kernel_module` | cameravirtual |
| — install ceiling: PipeWire node (userspace) | `cap.egress.install.userspace_node` | cameravirtual |
| publish: consumer attach/detach, share-mode, wrap-physical | `cap.egress.consumer.attach_events` · `cap.egress.share_mode` · `cap.egress.wrap_physical` · `cap.egress.consumer.format_negotiate` | cameravirtual |
| local-only fallback where no OS path | `cap.egress.publish.local_only_fallback` | cameravirtual |
| in-process synthetic source / test pattern | `cap.testsrc.provider` · `.frame_feed` · `.test_pattern` · `.manual_clock` | cameravirtual |
| consent / session start-stop / arbitration | `cap.os.consent.*` · `cap.os.session.*` · `cap.os.arbitrate.*` | camerapolicy |
| thermal/power pressure | `cap.os.thermal.*` | camerapolicy |

---

## gaps

### (a) feature with NO matching cap → propose new cap-ID (reopen P3)

1. **HDR static metadata on ingest** (capture-card path). 20-beyond-os §"Pro capture cards" line 30: DeckLink `IDeckLinkVideoFrameMetadataExtensions` delivers Rec.2020 + PQ/HLG **plus** mastering-display primaries + MaxCLL/MaxFALL (CEA-861.3 static metadata) *attached to ingested frames*. The existing `cap.capture.hdr.video.{hlg,hdr10}` describe a *camera selecting* an HDR capture **profile**; they are output-profile selectors, not a per-frame *received-static-metadata attachment*, and they sit in cameraphoto (capture-modes), not on the frame/meta channel an ingested SDI signal rides. No cap carries received HDR mastering metadata.
   ⇒ propose `cap.meta.hdr.static` (per-frame received HDR mastering-display + MaxCLL/MaxFALL static metadata), area 10 / **camerameta** — rides the `cap.meta.access.bag` channel like other emitted per-frame metadata. **(P3)**

2. **Input-signal-format auto-detect + change events** (capture-card path). 20-beyond-os §"Pro capture cards" line 29: DeckLink `bmdVideoInputEnableFormatDetection` → `VideoInputFormatChanged` — a source whose resolution/fps/format changes *underneath you* mid-stream, with an async notification. The OS camera model negotiates a fixed format up front; `cap.stream.reconfigure.live` is *app-initiated* reconfigure, the inverse of this *device-initiated* signal-change event. `cap.device.class.capture_card`'s v4l2 note mentions `DV-timings` but there is no cap for the auto-detect + change-event itself. v4l2 has the partial primitive (`VIDIOC_QUERY_DV_TIMINGS` + `V4L2_EVENT_SOURCE_CHANGE`).
   ⇒ propose `cap.stream.source.format_change_event` (async notification that the upstream signal's resolution/fps/format changed, with re-detect), area 2 / **cameradevice**. Classifies native on v4l2 (`SOURCE_CHANGE` event) + the capture-card provider plane; deny→`cap.stream.reconfigure.live` re-negotiate elsewhere. **(P3)**

3. **OS-hidden virtual / screen-capture device discoverability.** 20-beyond-os §"OBS Studio" line 57: OBS sets `kCMIOHardwarePropertyAllowScreenCaptureDevices=1` (CMIO low-level) to enumerate virtual/screen cams that AVFoundation hides by default. `cap.enum.list` enumerates *currently-visible* devices; there is no cap for the opt-in toggle that *unhides* OS-suppressed virtual/screen-capture device classes from enumeration.
   ⇒ propose `cap.enum.include_hidden_virtual` (opt into enumerating OS-suppressed virtual/screen-capture devices), area 1 / **cameradevice**. macos native (CMIO property); deny→`cap.enum.list` (visible-only) elsewhere. **(P3)**

### (b) wrong-seam scatter → P5 recheck
None. Every covered OBS feature's caps cluster cleanly within their owning module. The A/V-sync machinery sits whole in cameracapture; the publish path + all four install ceilings sit whole in cameravirtual; the SDI-embedded-audio routes through the single sanctioned A/V seam (§14c). No awkward cross-module scatter surfaced.

### (c) deny-on-all-targets-without-fallback (MEL-ENGINE-VII)
- **`cap.capture.hdr.video.hlg`** — `deny`+`fallback:none` on **win32 (mf)** and **linux (v4l2/libcamera/pipewire)**; native only on macos (avf). HLG ingest/capture is unreachable on 2 of 3 targets with no degradation path declared.
- **`cap.capture.hdr.video.hdr10`** (PQ) — `deny`+`fallback:none` on **win32** and **linux**; on macos `deny`→fallback `cap.capture.hdr.video.hlg`. HDR10/PQ unreachable on win32+linux, no fallback.
- **`cap.capture.hdr.video.widegamut`** — `deny`+`fallback:none` on **win32** and **linux**; native only on macos.

These reflect that the consumer camera HALs on win32/linux expose no HDR-video capture profile — honest per MEL-ENGINE-I (deny ≠ never; the capture-card provider plane is where HDR ingest actually lives). But for the **OBS-on-win32/linux** target specifically, HDR is real (DeckLink delivers it), so the `fallback:none` is a coverage hole the *new gap-(a)#1 cap* (`cap.meta.hdr.static`, classified native on the capture-card provider plane) is the proper home for — the HDR truly arrives through the **ingest/metadata** seam on those targets, not the camera-capture-profile seam. Recommend P4 re-examine whether these three `fallback:none` cells should fall back to the proposed ingest-metadata cap once minted.

Other deny clusters touched but **legitimately fallback-covered** (not gaps): `cap.frame.zerocopy.{iosurface,dmabuf,d3d11}` each deny off-platform with a cross-fallback to the platform-native surface kind; `cap.topology.multicam.concurrent` deny on macos(avf)/v4l2 but emulate on mf/libcamera/pipewire and fallback `cap.stream.multi.concurrent`; `cap.egress.*` deny on libcamera but native on v4l2/pipewire (linux still covered); `cap.stream.reconfigure.live` deny on all linux impls but native macos/win32 (per-target ≥1 path holds via mf, and linux degrades to renegotiate).
