# Graphics & GPU compute — OS-surface atlas (finer grain)
> domains D20–D22. Consumed by 00-atlas.md. Capability ceiling, axis-neutral; backends/APIs are hints.

---

### D20 · gpu — GPU rendering & compute
def: programming the GPU for raster, compute, and ray work.
- **adapter/device**:
  · enumerate adapters & pick (discrete / integrated / software)
  · feature & limit query (tier flags, max sizes, format support)
  · device create & lost/removed recovery
  · driver/vendor/arch identification
  · power-preference & low-power selection
- **queues & submission**:
  · queue families/kinds (graphics · compute · transfer)
  · command-buffer record / submit / reuse
  · submission batching & ordering
  · GPU-side timeout / TDR awareness
- **command encoding**: render-pass encoder · compute encoder · blit/copy encoder · secondary/parallel encoders · debug markers & groups
- **render passes**:
  · color / depth / stencil attachments
  · load/store ops & clear
  · MSAA & resolve attachments
  · input attachments / programmable blending
  · attachment & framebuffer dimensions
- **pipelines & PSO**:
  · graphics PSO (vertex layout, raster, blend, depth-stencil state)
  · compute PSO
  · mesh/task pipeline (mesh shading?)
  · PSO cache / serialize / warm-up
  · dynamic state vs baked state
  · pipeline libraries / linked-shader reuse?
- **shaders & reflection**:
  · shader module from IR (SPIR-V / DXIL / MSL / WGSL)
  · entry points & specialization constants
  · reflection (bindings, vertex inputs, workgroup size)
  · subgroup / wave intrinsics
- **binding model**:
  · descriptor sets / binding groups / root signature
  · push constants / root constants
  · dynamic offsets
  · bindless / descriptor-indexing (large unbounded arrays)
  · samplers (static / dynamic)
- **ray tracing**:
  · BLAS / TLAS build & update / refit
  · RT pipelines & shader binding table
  · inline / ray-query in compute
  · opacity-micromap / displacement-micromap?
- **indirect & multi-draw**: indirect draw/dispatch · multi-draw-indirect · indirect count · GPU-driven command generation
- **queries**: timestamp · occlusion (binary / precise) · pipeline-statistics · query resolve/readback
- **synchronization**:
  · fences (CPU↔GPU)
  · semaphores (queue↔queue)
  · timeline semaphores / shared events
  · barriers & resource-state transitions
  · sub-resource & memory hazard tracking
- **on-chip / tiling**: tile/subpass memory · memoryless attachments · tile shaders · framebuffer-fetch
- **multi-GPU**: explicit device groups · cross-adapter copy · AFR/linked-node?
- **backends**: Metal · Vulkan · D3D12 · WebGPU · GLES
↑beyond: vendor extensions — mesh shading · cooperative-matrix / tensor cores · work graphs · DLSS/FSR/XeSS interop · variable-rate shading · sampler feedback · ReBAR · cooperative-vector?
apps: engines (Unreal/Unity) · DCC tools · ML trainers.
status: spawn (extensive: `design/gpu-rhi.md`, `render-graph.md`, slang/bindless).

---

### D21 · gpu-mem — GPU resources & cross-API sharing
def: allocating, residency-managing, and sharing GPU memory.
- **resources**: buffers (vertex / index / uniform / storage / indirect) · textures (1D/2D/3D/cube/array) · samplers · texel/typed views
- **heaps & allocation**:
  · memory-type / heap selection (device-local · host-visible · host-coherent · cached)
  · sub-allocation & suballocator placement
  · placed vs committed resources
  · alignment & size query
- **residency**:
  · make-resident / evict
  · residency priority & budgets
  · paging & demand fault?
- **sparse / virtual**:
  · sparse buffers & images (tiled resources)
  · partially-resident textures & feedback
  · virtual address reservation & remap
- **layout & format**:
  · mip / array / 3D layout & tiling (linear vs optimal)
  · format capability & feature query (sample / storage / blend / filter)
  · compression (block / lossless framebuffer)
  · swizzle & component mapping
- **transfer & staging**:
  · dedicated transfer queue
  · upload / readback staging rings
  · async copy overlap with render/compute
  · CPU map / unmap / flush-invalidate (persistent map)
- **zero-copy import/export**:
  · dmabuf (Linux)
  · IOSurface / CVPixelBuffer (Apple)
  · AHardwareBuffer (Android)
  · D3D shared handle / NT handle (Win32)
  · WebGPU VideoFrame / external-texture (wasm)
  · opaque external-memory handle (Vulkan)
- **cross-device sharing**: GPU↔GPU copy/import · GPU↔display scanout share · interop with compute/codec engines
- **budget & overcommit**: memory budget query · overcommit / pressure reporting · current-usage poll & demote callback
↑beyond: DMA-BUF fences · Vulkan external-memory / external-semaphore · P2P DMA · host-image-copy? · resizable-BAR direct upload.
apps: video pipelines · zero-copy capture→render→encode · compositors.
status: spawn (`design/gpu-async-resolve-transfer.md`, `gpu-bindless-growable.md`).

---

### D22 · video-codec — hardware encode / decode / process
def: the fixed-function media engine for compressed video.
- **decode**:
  · codecs (H.264 · HEVC · AV1 · VP9 · JPEG)
  · profile / level / capability query
  · output to GPU surface (zero-copy) vs system buffer
  · seek / flush / drain & reference management
- **encode**:
  · codecs / profiles / levels
  · rate control (CBR · VBR · CQP · CRF · capped-VBR)
  · bitrate / quality target & VBV/HRD buffer
  · GOP structure · B-frames · reference frames
  · low-latency / intra-refresh mode
  · forced keyframe / IDR-on-demand & dynamic bitrate
- **container boundary**: mux/demux handoff (codec engine ends at elementary stream; container is separate)
- **HDR & bit-depth**: 10-bit / 12-bit · HDR transfer (PQ / HLG) · color primaries & matrix passthrough
- **fallback**: hardware vs software path selection · capability probe · graceful degrade
- **surface in/out**: zero-copy GPU surface in/out · CSC (color-space convert) · scaling / resize · deinterlace · rotation/crop via media engine
- **sessions**: multi-stream concurrent · transcode (decode→encode) chaining · session reset & reconfigure
- **timing**: presentation/decode timestamps · encode latency reporting · frame reordering (DTS/PTS)
- **backends**: VideoToolbox · MediaCodec · Media Foundation · NVENC/NVDEC · VAAPI · WebCodecs
↑beyond: SVC / temporal layers · HW AV1 encode tiers · content-light / mastering-display metadata · ROI / region-of-interest encode? · long-term reference frames.
apps: OBS · video editors · conferencing · the camera-charter recording domain.
status: spawn (`design/media-video.md`).
