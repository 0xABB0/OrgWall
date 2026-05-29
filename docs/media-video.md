# `media.video` — video sessions, frames, decode / encode / process, camera, protected content

Melody's video surface. Camera preview, video calls, video editors, streaming encoders, screen capture, remote desktop, ML preprocessing, and XR passthrough composition all need the same properties as rendering: explicit memory, explicit sync, color correctness, zero-copy import / export, and predictable fallbacks. `media.video` owns the *model* — sessions, frames, color metadata, codec enumeration, protected-content discipline; the GPU module owns the *recording surface* — `cmd_video_decode`, `cmd_video_encode`, `cmd_video_process` stay there, alongside the `VIDEO_BITSTREAM` / `VIDEO_PARAMETER` / `VIDEO_DECODE_OUTPUT` / `VIDEO_ENCODE_INPUT` / `VIDEO_PROCESS_INPUT` / `VIDEO_PROCESS_OUTPUT` buffer / texture usages and the `VideoDecode` / `VideoEncode` / `VideoProcess` queue roles. This document is the consumer of those primitives.

`media.video` is the canonical first child of a new top-level `media.*` family. Likely future siblings: `media.audio`, `media.image`. Possible internal split: `media.video.decode`, `media.video.encode`, `media.video.process`, `media.video.camera` — flagged as design space; the unified surface specified here is the contract that any future split must preserve.

---

## Inherited principles

- **P1 — Emulate-to-equivalent.** WebGPU's `import_only` decode tier and absent encode tier are honest emulation, not a stigma: the WebCodecs `VideoFrame` bridge is a first-class primary path, not a degraded fallback. Where hardware video-process is absent and the operation can be honestly served by a compute shader (color-space conversion, scale, format conversion), the engine lowers to compute and reports `caps.media.video_process = shader_emulated`. Where no faithful path exists (hardware decode on a WebGPU runtime), the call hard-gates loudly.
- **P2 — Full-control escape.** Session helpers are convenience. Apps bypass them and ride the GPU's U5 native-interop path directly: import platform decoder / encoder outputs as `Borrowed` frames, manage the DPB by hand, record `cmd_video_*` from raw GPU commands. The primitives exposed here — frame import / export, plane views, color metadata, sync import / export, video queues, bitstream buffers, process commands — are sufficient to reimplement the helper end-to-end.
- **MEL-ENGINE-I — don't shy from the hard problem.** Hardware video on five backends with five codec sets and protected-content discipline is exactly the burden MEL-ENGINE-I forbids us from refusing. The unified media-IO surface ships.
- **MEL-ENGINE-VIII — fail with honor.** Protected content is never silently stripped to make a feature seem available. Color metadata is never guessed — no implicit Rec.709, no implicit limited range, no implicit BT.601 fallback. Missing capability is a hard gate, not a lie.
- **MEL-ENGINE-X — remember thy place.** Media is a peer of rendering. Melody is an application RHI; a video editor, a camera app, a streaming encoder, a teleconference client are first-class citizens of the engine, not bolt-ons hung off a game renderer.

---

## Public objects

### `Mel_Media_Video_Session`

The codec-bound execution context. Carries: codec (`H264 | H265 | AV1 | VP9 | …`), profile, level, chroma format (`Mono | YUV420 | YUV422 | YUV444`), bit depth (8 / 10 / 12), dimensions (max / current), rate-control class (decode-side rate-distortion hint, encode-side CBR / VBR / CQP / ICQ / constant-quality), mode (`Decode | Encode | Process`), and the **required parameter sets** descriptor (SPS / PPS for H.264; VPS / SPS / PPS for H.265; sequence header / OBU set for AV1; sequence header for VP9). Sessions are typed-handle value types, slotmap-owned, nameable, importable where the backend allows it, and visible to leak detection. Generation field turns use-after-destroy into a loud `session_alive()` failure (MEL-ENGINE-VIII).

Sessions inherit the GPU module's status / result model: `Mel_Media_Video_Session_Create_Result` carries a per-action status enum with low-two-bit severity (gpu-rhi.md §3.2); warnings (a 10-bit encode session granted at 8-bit because the device refused the 10-bit profile) coexist with a valid handle.

### `Mel_Media_Video_Frame`

A typed wrapper around one or more GPU texture views plus color metadata. The plane shape depends on the chroma format and the platform's preferred layout (`NV12`, `P010`, `P016`, `YV12`, `AYUV`, `Y210`, `Y410`, packed 4:4:4); the frame's `planes[]` enumerates them and a `plane_view(frame, plane)` accessor exposes the underlying `Mel_Gpu_Texture_View` for direct sampling or compute consumption. Frames are slotmap-owned typed-handle values. Bitstreams and parameter buffers are not frames — they are ordinary GPU buffers with `VIDEO_BITSTREAM` / `VIDEO_PARAMETER` usage (specified in the GPU resource surface), referenced from this module by handle.

Every frame carries the color-metadata descriptor (below) inline. The metadata is *part of the frame's identity*; a frame without complete color metadata is a construction failure, not a frame with defaulted fields.

---

## Submodule split

The unified surface specified here covers four operational shapes; a later split is open design space.

- **`media.video.decode`** — session in `Decode` mode; bitstream-buffer → frame; DPB management; access-unit framing helpers (start-code / length-prefixed / OBU walkers).
- **`media.video.encode`** — session in `Encode` mode; frame → bitstream-buffer; rate-control state; force-IDR / QP / ROI overrides; B-frame structure and DPB management.
- **`media.video.process`** — session in `Process` mode; frame[] → frame; scale / crop / rotate / deinterlace / CSC / tone-map / format-conversion op chains.
- **`media.video.camera`** — capture-side surface: `Mel_Media_Camera` enumeration, configuration, `Borrowed`-frame stream onto the engine reactor. Camera frames carry full color metadata at arrival (no implicit Rec.709 conversion at the boundary); the underlying texture imports through the platform's native interop primitive (`CVPixelBuffer`, `AHardwareBuffer`, Media Foundation `IMFSample`, `MediaStreamTrackProcessor` on Web).

Any split must preserve (i) the single `Mel_Media_Video_Session` / `Mel_Media_Video_Frame` types across submodules; (ii) the single color-metadata descriptor; (iii) the single import / export interop discipline. If the four shapes diverge enough to warrant separate compilation units, the split is mechanical; until then, one module.

---

## Operations

The recording verbs live in the GPU module's command surface; this section specifies their *semantics*. Each takes a GPU command list `cmd`, recorded onto a command pool bound to a `VideoDecode` / `VideoEncode` / `VideoProcess` queue role.

- **`cmd_video_decode(cmd, session, bitstream_buffer, dpb_state, output_frame)`** — decode one access unit. `bitstream_buffer` is a GPU buffer with `VIDEO_BITSTREAM` usage, slice / range identified by an inline offset descriptor. `dpb_state` carries the reference-picture set and current frame slot. `output_frame` is a `Mel_Media_Video_Frame` whose underlying texture views were allocated with `VIDEO_DECODE_OUTPUT` usage. The decoder consumes the session's parameter sets (or, on `VK_KHR_video_maintenance2`, inline parameters per `VkVideoDecodeAV1InlineSessionParametersInfoKHR`).
- **`cmd_video_encode(cmd, session, input_frame, dpb_state, output_bitstream)`** — encode one frame. `input_frame` is a `Mel_Media_Video_Frame` whose underlying texture views were allocated with `VIDEO_ENCODE_INPUT` usage. `output_bitstream` is a GPU buffer with `VIDEO_BITSTREAM` usage receiving the encoded access unit. Rate-control parameters come from the session; per-frame overrides (force-IDR, QP offset, region-of-interest map via `VK_KHR_video_encode_quantization_map`) are inline.
- **`cmd_video_process(cmd, session, input_frames[], output_frame, ops[])`** — composite of scale, crop, rotate, deinterlace (where supported), color-space conversion, tone-map, and format conversion. `input_frames[]` carry `VIDEO_PROCESS_INPUT` usage; `output_frame` carries `VIDEO_PROCESS_OUTPUT`. `ops[]` is an ordered list of transforms; the backend fuses them where the hardware processor admits a single pass and chains where it does not. On backends without a hardware processor, the operation lowers to a compute pipeline that consumes the planes through sampler-YCbCr conversion (Vulkan) or direct sampler bindings.

The GPU module's render-graph (gpu-rhi.md §8) admits `VideoDecode` / `VideoEncode` / `VideoProcess` pass kinds; `media.video` operations compose inside graph passes without special pleading (MEL-ENGINE-IX).

### Session lifecycle

A session is created with the codec configuration descriptor and an explicit operational mode. Reconfiguration mid-stream (resolution change in a streaming decoder, rate-control retarget in an encoder) is a session-level operation, not a frame-level one — `session_reconfigure(session, desc)` returns a result whose status reports whether the underlying hardware admitted the change in place or whether a session rebuild is required (warning-with-valid-handle on rebuild, so the app drains the in-flight DPB before submitting against the new configuration). Sessions are destroyed explicitly; pending submissions complete first (the engine waits internally — `Mel_Media_Video_Session` is a U3 awaitable resource and `session_destroy` is an async completion).

### External sync on `Borrowed` frames

A `Borrowed` frame imported from a platform source carries external sync semantics; the engine never assumes ownership of the underlying synchronization primitive. The interop descriptor declares: (i) the *acquire* primitive (XR runtime wait-semaphore, `IOSurface` use-count, `AHardwareBuffer` implicit-sync fence, DXGI keyed mutex, `GPUExternalTexture` shader-invocation lifetime), and (ii) the *release* primitive (the corresponding signal). The engine inserts the appropriate `cmd_queue_ownership_acquire` / `cmd_queue_ownership_release` (gpu-rhi.md §8.2) at pass boundaries; the user does not write barriers. A `Borrowed` frame outliving its acquire window is a loud `frame_alive()` failure (MEL-ENGINE-VIII).

---

## Interop is the primary path

This is the load-bearing claim of the module (gpu-rhi.md §7.6). Cameras, OS decoders, browser video frames, platform capture, XR passthrough — these are the *sources*; the engine's session helpers are one consumer among several. The interop surface is therefore peer-grade, not afterthought.

- **`CVPixelBuffer`** (macOS / iOS / visionOS) — `CVMetalTextureCacheCreateTextureFromImage` per plane; the resulting `MTLTexture` imports as a `Borrowed` `Mel_Gpu_Texture` with external sync from the `IOSurface`'s ownership discipline. Camera frames from `AVCaptureSession`, VideoToolbox decoder output, and ARKit / `cp_layer_renderer` passthrough rides this path.
- **`AHardwareBuffer`** (Android) — `VK_ANDROID_external_memory_android_hardware_buffer` import; the `VkImage` imports as a `Borrowed` texture with external sync from the `AHardwareBuffer`'s implicit-sync fence (`VK_ANDROID_external_format_resolve` where granted for YUV samplers without descriptor specialization). `Camera2` / `CameraX` / `MediaCodec` output rides this path.
- **DXGI shared handles** — `IDXGIResource1::CreateSharedHandle` + `OpenSharedResource1`; the `ID3D12Resource` imports as a `Borrowed` texture. Media Foundation decoder output, Desktop Duplication frames, NVENC / AMF encoder input rides this path.
- **OpenXR swapchain images** — XR runtime composition layer surfaces (passthrough cameras, swapchain images for foveated layers) import as `Borrowed` frames with the XR runtime's wait-semaphore as external sync. Composition without a render-to-RGB-staging copy is the whole point on visionOS / Quest.
- **Browser `VideoFrame`** — `GPUExternalTexture` via `importExternalTexture({ source: video_frame })`. The frame is valid for the lifetime of the surrounding shader invocation; the engine surfaces the WebCodecs lifecycle as a typed `Mel_Media_Video_Frame` whose `Borrowed` underlying view is invalidated at the natural WebGPU boundary (MEL-ENGINE-VIII).
- **Platform capture surfaces** — `ScreenCaptureKit` / `IDXGIOutputDuplication` / `MediaProjection` / `getDisplayMedia` — same `Borrowed`-frame discipline; no mandatory RGB staging copy.

A frame imported through any of these paths can be sampled directly by user shaders, fed to `cmd_video_process`, encoded by `cmd_video_encode`, or re-exported through the inverse path — all without an intermediate copy. The engine never silently inserts a staging blit (MEL-ENGINE-III: no stolen cycles).

---

## Protected content

Some media paths expose protected sessions and surfaces — HDCP-bound display output, DRM-bound decoder output, Widevine / FairPlay / PlayReady-mediated content surfaces. The engine treats protected content as an honest tier, not as a degraded variant of unprotected (MEL-ENGINE-VIII).

A `protected` flag is set per resource and per pipeline — *not just per session*. Granularity:

- **Resource-level.** A protected texture cannot be mapped to CPU, cannot be exported through interop except to peer protected sinks, cannot be sampled by a non-protected pipeline. Validation enforces this at bind time, not at submit (the bind fails loudly).
- **Pipeline-level.** A pipeline created with `protected = true` requires every bound resource to be protected. Lowering: Vulkan `VK_EXT_pipeline_protected_access` + `VK_KHR_protected_memory`-derived submission discipline; D3D12 protected-resource sessions (`ID3D12ProtectedResourceSession`); Metal `MTLResourceOptions.storageModePrivate` plus the protected-content composition path; WebGPU has no protected-content surface (capability reports `none`).
- **Queue-level.** A protected queue (Vulkan `VK_QUEUE_PROTECTED_BIT`) accepts only protected command submissions; mixing protected and unprotected in the same submission is a hard error.
- **Render-target-level.** HDCP-bound render targets and DRM-bound framebuffers ride the `protected` flag so a protected pass cannot accidentally bind an unprotected resource — the pipeline-create fails, not the submit.
- **Capture / debug.** RenderDoc / PIX / Aftermath are told to stand down on protected resources; the GPU module's `caps.debug.capture_replay` is mutually exclusive with `caps.media.protected_media = process_present` at session-create time. The engine never strips protection to permit capture.

Caps report protected support separately for decode, process, and present: `caps.media.protected_media ∈ { none | decode_only | process_present }`.

---

## Color metadata

Every imported or created video frame carries the complete color descriptor. The engine never guesses (MEL-ENGINE-VIII):

- **Primaries** — `Rec_601 | Rec_709 | Rec_2020 | Display_P3 | sRGB | DCI_P3`. The shared `Mel_Color_Space` enum from `display` is consumed identically here; one enum, one meaning.
- **Transfer function** — `Linear | sRGB | PQ | HLG | Rec_709_OETF | Rec_2020_OETF | Gamma22 | Gamma28`.
- **Matrix** — `BT_601 | BT_709 | BT_2020_NCL | BT_2020_CL | Identity_RGB | Identity_YCgCo`. The YUV → RGB conversion matrix; orthogonal to primaries (a Rec.2020-primaries frame may carry a BT.709 matrix in legacy pipelines).
- **Range** — `Full | Limited`. Full = `[0, 255]` (8-bit), `[0, 1023]` (10-bit); Limited = `[16, 235]` luma, `[16, 240]` chroma (8-bit). The engine never assumes limited because "video is usually limited" — the source declares.
- **Chroma siting** — `Cosited | Midpoint | Either`. Where the chroma sample sits relative to the luma grid; load-bearing for correct upsampling.
- **Mastering metadata** — `Option<{ display_primaries[3], white_point, max_luminance, min_luminance, max_cll, max_fall }>`. Present on HDR content with SMPTE ST 2086 metadata; absent on SDR or where the source did not publish.
- **Orientation** — `Identity | Rotate90 | Rotate180 | Rotate270`, with optional horizontal / vertical flip. Camera frames frequently arrive rotated relative to the sensor; the engine carries the orientation flag rather than baking a rotation into the import.

Rendering, compute, and video-process commands consume this metadata explicitly. A compute shader sampling a `Mel_Media_Video_Frame` reads the matrix / range / primaries from a uniform the engine packs from the frame's descriptor; a `cmd_video_process` color-space-conversion op reads the source and destination descriptors. No defaulted-to-Rec.709 path exists.

---

## Codec coverage per backend

- **Vulkan.** `VK_KHR_video_queue` plus the codec set. Decode: `VK_KHR_video_decode_h264`, `VK_KHR_video_decode_h265`, `VK_KHR_video_decode_av1` (KHR), `VK_KHR_video_decode_vp9` (KHR, 1.4.317, June 2025). Encode: `VK_KHR_video_encode_h264`, `VK_KHR_video_encode_h265`, `VK_KHR_video_encode_av1` (KHR, November 2024 alongside Vulkan 1.3.302). Maintenance: `VK_KHR_video_maintenance1` / `VK_KHR_video_maintenance2` (the latter supports inline session parameters such as `VkVideoDecodeAV1InlineSessionParametersInfoKHR`), `VK_KHR_video_encode_quantization_map`. Sampler-YCbCr conversion (`VK_KHR_sampler_ycbcr_conversion`) where granted, for direct-sample paths.
- **D3D12.** Video Decode / Encode / Process command lists. AV1 encode on Win11 24H2 / WDDM 3.2 alongside H.264, HEVC, and HEVC 4:2:2 / 4:4:4. Shared DXGI resources where supported. The `ID3D12VideoDevice3` interface is the surface; `D3D12_VIDEO_DECODER_HEAP_DESC` and `D3D12_VIDEO_ENCODER_HEAP_DESC` configure heaps.
- **Metal.** CoreVideo / VideoToolbox / AVFoundation interop into Metal textures. Metal compute and Metal 4 `MTL4MachineLearningCommandEncoder` available for shader-emulated process where appropriate. Hardware encoder access through VideoToolbox compression sessions; the engine wraps the session's output `CMSampleBuffer` chain into bitstream buffers without copy.
- **WebGPU.** `importExternalTexture` + WebCodecs `VideoFrame` (Chrome 116+, Display-P3 HDR Chrome 121). This is the *primary* WebGPU video path, not a degraded fallback. No core hardware decode / encode command model exists; caps report `caps.media.video_decode = import_only` and `caps.media.video_encode = none`. The `import_only` tier is the honest constraint (MEL-ENGINE-VIII), not a stigma — a streaming app on Web uses WebCodecs for decode and the engine's compute / fragment surface for processing and presentation; no lie is told.

---

## Caps integration

Caps live under `caps.media.*` on the GPU adapter (the GPU module exposes them; `media.video` reads them):

- `caps.media.video_decode ∈ { none | import_only | hardware }`
- `caps.media.video_encode ∈ { none | import_only | hardware }`
- `caps.media.video_process ∈ { none | shader_emulated | hardware }`
- `caps.media.protected_media ∈ { none | decode_only | process_present }`

Per-codec sub-caps enumerate which codecs the `hardware` tier covers (`caps.media.codecs.decode_h264 = true | false`, etc.). Power users branch on the tier; the simple path writes one code path and the engine reports honestly what it did.

---

## P2 escape

Apps may bypass `Mel_Media_Video_Session` and import platform decoder / encoder outputs directly through the GPU module's U5 native-interop path. The primitives exposed here — frame import / export, plane views, color metadata, sync import / export, video queues, bitstream buffers, process commands — are sufficient to reimplement the session helper end-to-end. A teleconferencing app that wants to drive WebRTC's libwebrtc directly, a streaming encoder that wants vendor-private NVENC features the engine has not surfaced, an editor that wants frame-accurate seeking semantics the engine's helper does not provide — each takes the primitives, ignores the helper, and the engine is a peer not a gate (MEL-ENGINE-II).

---

## Sibling docs

`docs/provider.md` (upstream — some vendor encoders / decoders are provider-backed); `modules/sensor/spec.md` (camera sensor metadata, ambient signals); `docs/platform-surface.md`, `docs/platform-display.md` (color-space enum source, HDR capabilities); `docs/frame-pacing.md` (consumer — video-encoder pull-clock as a P2 pacing source); `docs/frame-latency.md` (camera-to-encode latency markers); `docs/render-graph.md` (peer — video pass kinds are graph pass kinds, gpu-rhi.md §8); `docs/render-reconstruction.md` (consumer — TAA / upscaling over decoded video); `docs/io-asset.md` (peer — both are media-IO-domain); `docs/xr.md` (consumer — passthrough cameras, runtime composition layers).
