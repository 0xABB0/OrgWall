#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/status.h>
#include <gpu/format.h>
#include <gpu/shader.h>
#include <gpu/sampler.h>
#include <gpu/bind_group.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;

MEL_GPU_HANDLE(Mel_Gpu_Pipeline);

// U11 immutable/static sampler baked into the pipeline layout (gpu-rhi.md §6.3 / §6.7). The referenced
// sampler must stay alive for the pipeline's lifetime.
typedef struct
{
    Mel_Gpu_Sampler sampler;
    u32             binding;
} Mel_Gpu_Static_Sampler;

typedef enum
{
    MEL_GPU_TOPOLOGY_TRIANGLE_LIST = 0,
    MEL_GPU_TOPOLOGY_TRIANGLE_STRIP,
    MEL_GPU_TOPOLOGY_LINE_LIST,
    MEL_GPU_TOPOLOGY_POINT_LIST,
} Mel_Gpu_Topology;

typedef enum
{
    MEL_GPU_CULL_NONE = 0,
    MEL_GPU_CULL_FRONT,
    MEL_GPU_CULL_BACK,
} Mel_Gpu_Cull;

// Winding the rasterizer treats as front-facing. Zero = counter-clockwise, the engine's Y-up convention (§1).
typedef enum
{
    MEL_GPU_FRONT_FACE_CCW = 0,
    MEL_GPU_FRONT_FACE_CW,
} Mel_Gpu_Front_Face;

// Polygon fill mode (gpu-rhi.md §6.5). Wireframe / point need the device fill-mode-non-solid feature; an
// ungranted request degrades to solid with a warning (MEL-CODE-007), never a silent miscompile.
typedef enum
{
    MEL_GPU_FILL_SOLID = 0,
    MEL_GPU_FILL_WIREFRAME,
    MEL_GPU_FILL_POINT,
} Mel_Gpu_Fill;

// Blend factor — protocol map onto VkBlendFactor / D3D12_BLEND (MEL-CODE-001 protocol carve-out). Dual-source
// factors are cap-gated and deferred to the dual_source_blend slice (gpu-rhi.md §6.5).
typedef enum
{
    MEL_GPU_BLEND_ZERO = 0,
    MEL_GPU_BLEND_ONE,
    MEL_GPU_BLEND_SRC_COLOR,
    MEL_GPU_BLEND_ONE_MINUS_SRC_COLOR,
    MEL_GPU_BLEND_DST_COLOR,
    MEL_GPU_BLEND_ONE_MINUS_DST_COLOR,
    MEL_GPU_BLEND_SRC_ALPHA,
    MEL_GPU_BLEND_ONE_MINUS_SRC_ALPHA,
    MEL_GPU_BLEND_DST_ALPHA,
    MEL_GPU_BLEND_ONE_MINUS_DST_ALPHA,
    MEL_GPU_BLEND_CONSTANT_COLOR,
    MEL_GPU_BLEND_ONE_MINUS_CONSTANT_COLOR,
    MEL_GPU_BLEND_CONSTANT_ALPHA,
    MEL_GPU_BLEND_ONE_MINUS_CONSTANT_ALPHA,
    MEL_GPU_BLEND_SRC_ALPHA_SATURATE,
} Mel_Gpu_Blend_Factor;

// Blend equation — protocol map onto VkBlendOp / D3D12_BLEND_OP (MEL-CODE-001 protocol carve-out).
typedef enum
{
    MEL_GPU_BLEND_OP_ADD = 0,
    MEL_GPU_BLEND_OP_SUBTRACT,
    MEL_GPU_BLEND_OP_REVERSE_SUBTRACT,
    MEL_GPU_BLEND_OP_MIN,
    MEL_GPU_BLEND_OP_MAX,
} Mel_Gpu_Blend_Op;

// Stencil operation — protocol map onto VkStencilOp / D3D12_STENCIL_OP (MEL-CODE-001 protocol carve-out).
typedef enum
{
    MEL_GPU_STENCIL_KEEP = 0,
    MEL_GPU_STENCIL_ZERO,
    MEL_GPU_STENCIL_REPLACE,
    MEL_GPU_STENCIL_INCREMENT_CLAMP,
    MEL_GPU_STENCIL_DECREMENT_CLAMP,
    MEL_GPU_STENCIL_INVERT,
    MEL_GPU_STENCIL_INCREMENT_WRAP,
    MEL_GPU_STENCIL_DECREMENT_WRAP,
} Mel_Gpu_Stencil_Op;

// Per-component color write mask (gpu-rhi.md §6.5). A zero mask writes no color — the Vulkan/D3D12 convention,
// deliberately not reinterpreted (MEL-CODE-007). The MEL_GPU_BLEND_OPAQUE / _ALPHA initializers and the scalar
// `color_format` shortcut set it to ALL; an explicit `color_targets[]` entry must set it itself.
typedef u8 Mel_Gpu_Color_Write_Mask;
enum
{
    MEL_GPU_COLOR_WRITE_R = 1u << 0,
    MEL_GPU_COLOR_WRITE_G = 1u << 1,
    MEL_GPU_COLOR_WRITE_B = 1u << 2,
    MEL_GPU_COLOR_WRITE_A = 1u << 3,
    MEL_GPU_COLOR_WRITE_ALL = MEL_GPU_COLOR_WRITE_R | MEL_GPU_COLOR_WRITE_G | MEL_GPU_COLOR_WRITE_B | MEL_GPU_COLOR_WRITE_A,
};

// Per-attachment blend state (gpu-rhi.md §6.5 per-attachment blend). When `enable` is false the source passes
// through unblended and only `write_mask` applies. Constant-factor blends read Mel_Gpu_Pipeline_Opt.blend_constants.
typedef struct
{
    bool                     enable;
    Mel_Gpu_Blend_Factor     src_color, dst_color;
    Mel_Gpu_Blend_Op         color_op;
    Mel_Gpu_Blend_Factor     src_alpha, dst_alpha;
    Mel_Gpu_Blend_Op         alpha_op;
    Mel_Gpu_Color_Write_Mask write_mask;
} Mel_Gpu_Blend;

// Convenience blend initializers. OPAQUE: no blend, write RGBA. ALPHA: standard src-over with premultiplied output.
#define MEL_GPU_BLEND_OPAQUE ((Mel_Gpu_Blend){ .write_mask = MEL_GPU_COLOR_WRITE_ALL })
#define MEL_GPU_BLEND_ALPHA                                                                                                  \
    ((Mel_Gpu_Blend){ .enable = true, .src_color = MEL_GPU_BLEND_SRC_ALPHA, .dst_color = MEL_GPU_BLEND_ONE_MINUS_SRC_ALPHA,  \
                      .color_op = MEL_GPU_BLEND_OP_ADD, .src_alpha = MEL_GPU_BLEND_ONE, .dst_alpha = MEL_GPU_BLEND_ONE_MINUS_SRC_ALPHA, \
                      .alpha_op = MEL_GPU_BLEND_OP_ADD, .write_mask = MEL_GPU_COLOR_WRITE_ALL })

// One color render target: its attachment format and per-attachment blend (gpu-rhi.md §6.5 MRT). Drive MRT with
// Mel_Gpu_Pipeline_Opt.color_targets[]; the scalar `color_format` is the single-opaque-target shortcut.
typedef struct
{
    Mel_Gpu_Format format;
    Mel_Gpu_Blend  blend;
} Mel_Gpu_Color_Target;

// Per-face stencil state (gpu-rhi.md §6.5 per-face stencil ops). Zero = KEEP/KEEP/KEEP, compare NONE, masks 0.
typedef struct
{
    Mel_Gpu_Stencil_Op fail;       // on stencil-test fail
    Mel_Gpu_Stencil_Op pass;       // on stencil pass and depth pass
    Mel_Gpu_Stencil_Op depth_fail; // on stencil pass, depth fail
    Mel_Gpu_Compare_Op compare;
    u32                compare_mask;
    u32                write_mask;
    u32                reference;
} Mel_Gpu_Stencil_Face;

// Full depth/stencil state (gpu-rhi.md §6.5). Opt-in: when Mel_Gpu_Pipeline_Opt.depth_stencil is NULL the engine
// derives the default — with a depth_format present, depth test + write + LESS compare, no stencil/bounds.
// Supplying this struct takes full control: depth_compare NONE while depth_test is set warns and falls back to
// LESS; depth_bounds_test needs the device depth-bounds feature; stencil needs a depth format with a stencil aspect.
typedef struct
{
    bool                 depth_test;
    bool                 depth_write;
    Mel_Gpu_Compare_Op   depth_compare;
    bool                 depth_bounds_test;
    f32                  depth_bounds_min, depth_bounds_max;
    bool                 stencil_test;
    Mel_Gpu_Stencil_Face front, back;
} Mel_Gpu_Depth_Stencil;

typedef struct
{
    u32            location;
    Mel_Gpu_Format format;
    u32            offset;
} Mel_Gpu_Vertex_Element;

// U12 specialization constant override (gpu-rhi.md §6.4). `id` is the Slang/SPIR-V constant_id; `value` the
// 4-byte scalar (uint / int / float / bool-as-VkBool32) baked at pipeline create. Constants the shader does
// not declare are ignored with a warning; reflection records the declared set so the warning is precise.
typedef struct
{
    u32 id;
    u32 value;
} Mel_Gpu_Spec_Constant;

typedef enum
{
    MEL_GPU_PIPELINE_CREATE_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_PIPELINE_CREATE_VK_FAILED = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_PIPELINE_CREATE_NO_SHADER = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
    // gpu-rhi.md §6.7: a pipeline whose layout demands a bindless heap the device cannot provide vs. a
    // requested heap slot beyond the heap's capacity — distinct remedies, never conflated (MEL-ENGINE-VIII).
    MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE = MEL_GPU_STATUS(3, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_PIPELINE_CREATE_MISSING_BINDLESS_SLOT = MEL_GPU_STATUS(4, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Pipeline_Create_Status;

typedef struct
{
    Mel_Gpu_Shader                shader;
    Mel_Gpu_Topology              topology;
    Mel_Gpu_Cull                  cull;
    Mel_Gpu_Front_Face            front_face; // zero = CCW
    Mel_Gpu_Fill                  fill;       // zero = SOLID
    // Single-target shortcut: one opaque (no-blend, RGBA-write) color target of this format. Ignored when
    // `color_targets` below is set. Preserves every existing call site.
    Mel_Gpu_Format                color_format;
    // MRT + per-attachment blend (gpu-rhi.md §6.5). When color_target_count > 0 these define the color targets
    // in order and `color_format` is ignored.
    const Mel_Gpu_Color_Target*   color_targets;
    u32                           color_target_count;
    // Static blend constants for the CONSTANT_* blend factors (gpu-rhi.md §6.5).
    f32                           blend_constants[4];
    Mel_Gpu_Format                depth_format;
    // Full depth/stencil control; NULL = the documented default derived from `depth_format` (see Mel_Gpu_Depth_Stencil).
    const Mel_Gpu_Depth_Stencil*  depth_stencil;
    // Rasterization depth bias (gpu-rhi.md §6.5). `depth_bias_clamp` needs the device depth-bias-clamp feature.
    bool                          depth_bias;
    f32                           depth_bias_constant, depth_bias_clamp, depth_bias_slope;
    // MSAA (gpu-rhi.md §6.5). 0/1 = single-sampled; >1 is validated against the device framebuffer sample-count
    // limit and the target formats (an unsupported count warns and falls back to 1). `sample_shading` and
    // `min_sample_shading` need the device sample-rate-shading feature.
    u32                           samples;
    bool                          alpha_to_coverage;
    bool                          sample_shading;
    f32                           min_sample_shading;
    const Mel_Gpu_Vertex_Element* vertex_layout;
    u32                           vertex_layout_count;
    u32                           vertex_stride;
    u32                           push_constant_size;
    // U14: when true, set 0 of the layout is the device bindless heap, so the shader can index its texture /
    // sampler / buffer arrays. The per-draw root record rides the push-constant range above.
    bool                          bindless;
    // U14 classic path (gpu-rhi.md §6.7): app-owned descriptor-set layouts at set indices 0..N-1 for a
    // non-bindless pipeline. Mutually exclusive with `bindless` (set 0 cannot be both heap and app-owned).
    const Mel_Gpu_Bind_Group_Layout* set_layouts;
    u32                              set_layout_count;
    const Mel_Gpu_Static_Sampler* static_samplers;
    u32                           static_sampler_count;
    // U12: specialization-constant overrides baked at create (gpu-rhi.md §6.4). Reflection records the
    // shader's declared constants; supplying a value for one not declared warns rather than silently no-ops.
    const Mel_Gpu_Spec_Constant*  spec_constants;
    u32                           spec_constant_count;
    const char*                   name;
} Mel_Gpu_Pipeline_Opt;

typedef struct
{
    Mel_Gpu_Pipeline               value;
    Mel_Gpu_Pipeline_Create_Status status;
} Mel_Gpu_Pipeline_Create_Result;

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Opt opt);
#define mel_gpu_pipeline_create(dev, ...) mel_gpu_pipeline_create_opt((dev), (Mel_Gpu_Pipeline_Opt){ __VA_ARGS__ })

// U13 compute pipeline (gpu-rhi.md §6.5) — per-type create, a distinct state space (no vertex/raster/blend).
// Reflection drives the layout exactly as for graphics: bindless set 0, push-constant size, spec constants;
// the binding-model gates (MissingFeature / MissingBindlessSlot) apply identically. The first consumer of
// storage-buffer bindless and the seam for GPU-generated root records.
typedef struct
{
    Mel_Gpu_Shader                   shader; // a compute shader (mel_gpu_shader_create_compute_from_bytecode)
    u32                              push_constant_size;
    bool                             bindless;
    const Mel_Gpu_Bind_Group_Layout* set_layouts;
    u32                              set_layout_count;
    const Mel_Gpu_Spec_Constant*     spec_constants;
    u32                              spec_constant_count;
    const char*                      name;
} Mel_Gpu_Pipeline_Compute_Opt;

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_compute_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Compute_Opt opt);
#define mel_gpu_pipeline_compute_create(dev, ...) mel_gpu_pipeline_compute_create_opt((dev), (Mel_Gpu_Pipeline_Compute_Opt){ __VA_ARGS__ })

void mel_gpu_pipeline_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe);
bool mel_gpu_pipeline_alive(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe);
