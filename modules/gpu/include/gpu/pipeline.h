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

typedef enum
{
    MEL_GPU_FRONT_FACE_CCW = 0,
    MEL_GPU_FRONT_FACE_CW,
} Mel_Gpu_Front_Face;

typedef enum
{
    MEL_GPU_FILL_SOLID = 0,
    MEL_GPU_FILL_WIREFRAME,
    MEL_GPU_FILL_POINT,
} Mel_Gpu_Fill;

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

typedef enum
{
    MEL_GPU_BLEND_OP_ADD = 0,
    MEL_GPU_BLEND_OP_SUBTRACT,
    MEL_GPU_BLEND_OP_REVERSE_SUBTRACT,
    MEL_GPU_BLEND_OP_MIN,
    MEL_GPU_BLEND_OP_MAX,
} Mel_Gpu_Blend_Op;

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

typedef u8 Mel_Gpu_Color_Write_Mask;
enum
{
    MEL_GPU_COLOR_WRITE_R = 1u << 0,
    MEL_GPU_COLOR_WRITE_G = 1u << 1,
    MEL_GPU_COLOR_WRITE_B = 1u << 2,
    MEL_GPU_COLOR_WRITE_A = 1u << 3,
    MEL_GPU_COLOR_WRITE_ALL = MEL_GPU_COLOR_WRITE_R | MEL_GPU_COLOR_WRITE_G | MEL_GPU_COLOR_WRITE_B | MEL_GPU_COLOR_WRITE_A,
};

typedef struct
{
    bool                     enable;
    Mel_Gpu_Blend_Factor     src_color, dst_color;
    Mel_Gpu_Blend_Op         color_op;
    Mel_Gpu_Blend_Factor     src_alpha, dst_alpha;
    Mel_Gpu_Blend_Op         alpha_op;
    Mel_Gpu_Color_Write_Mask write_mask;
} Mel_Gpu_Blend;

#define MEL_GPU_BLEND_OPAQUE ((Mel_Gpu_Blend){ .write_mask = MEL_GPU_COLOR_WRITE_ALL })
#define MEL_GPU_BLEND_ALPHA                                                                                                  \
    ((Mel_Gpu_Blend){ .enable = true, .src_color = MEL_GPU_BLEND_SRC_ALPHA, .dst_color = MEL_GPU_BLEND_ONE_MINUS_SRC_ALPHA,  \
                      .color_op = MEL_GPU_BLEND_OP_ADD, .src_alpha = MEL_GPU_BLEND_ONE, .dst_alpha = MEL_GPU_BLEND_ONE_MINUS_SRC_ALPHA, \
                      .alpha_op = MEL_GPU_BLEND_OP_ADD, .write_mask = MEL_GPU_COLOR_WRITE_ALL })

typedef struct
{
    Mel_Gpu_Format format;
    Mel_Gpu_Blend  blend;
} Mel_Gpu_Color_Target;

typedef struct
{
    Mel_Gpu_Stencil_Op fail;
    Mel_Gpu_Stencil_Op pass;
    Mel_Gpu_Stencil_Op depth_fail;
    Mel_Gpu_Compare_Op compare;
    u32                compare_mask;
    u32                write_mask;
    u32                reference;
} Mel_Gpu_Stencil_Face;

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
    u32            buffer_slot;
} Mel_Gpu_Vertex_Element;

typedef struct
{
    u32  slot;
    u32  stride;
    bool per_instance;
} Mel_Gpu_Vertex_Buffer_Layout;

typedef struct
{
    u32 id;
    u32 value;
} Mel_Gpu_Spec_Constant;

typedef enum
{
    MEL_GPU_PIPELINE_CREATE_OK = MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK),
    MEL_GPU_PIPELINE_CREATE_BACKEND_FAILED = MEL_GPU_STATUS(1, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_PIPELINE_CREATE_NO_SHADER = MEL_GPU_STATUS(2, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_PIPELINE_CREATE_MISSING_FEATURE = MEL_GPU_STATUS(3, MEL_GPU_SEVERITY_ERROR),
    MEL_GPU_PIPELINE_CREATE_MISSING_BINDLESS_SLOT = MEL_GPU_STATUS(4, MEL_GPU_SEVERITY_ERROR),
} Mel_Gpu_Pipeline_Create_Status;

typedef struct
{
    Mel_Gpu_Shader                shader;
    Mel_Gpu_Topology              topology;
    Mel_Gpu_Cull                  cull;
    Mel_Gpu_Front_Face            front_face;
    Mel_Gpu_Fill                  fill;
    Mel_Gpu_Format                color_format;
    const Mel_Gpu_Color_Target*   color_targets;
    u32                           color_target_count;
    f32                           blend_constants[4];
    Mel_Gpu_Format                depth_format;
    const Mel_Gpu_Depth_Stencil*  depth_stencil;
    bool                          depth_bias;
    f32                           depth_bias_constant, depth_bias_clamp, depth_bias_slope;
    u32                           samples;
    bool                          alpha_to_coverage;
    bool                          sample_shading;
    f32                           min_sample_shading;
    const Mel_Gpu_Vertex_Element* vertex_layout;
    u32                           vertex_layout_count;
    u32                           vertex_stride;
    const Mel_Gpu_Vertex_Buffer_Layout* vertex_buffers;
    u32                                 vertex_buffer_count;
    u32                           push_constant_size;
    bool                          bindless;
    const Mel_Gpu_Bind_Group_Layout* set_layouts;
    u32                              set_layout_count;
    const Mel_Gpu_Static_Sampler* static_samplers;
    u32                           static_sampler_count;
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

typedef struct
{
    Mel_Gpu_Shader                   shader;
    u32                              push_constant_size;
    bool                             bindless;
    const Mel_Gpu_Bind_Group_Layout* set_layouts;
    u32                              set_layout_count;
    const Mel_Gpu_Spec_Constant*     spec_constants;
    u32                              spec_constant_count;
    u32                              threadgroup[3];
    const char*                      name;
} Mel_Gpu_Pipeline_Compute_Opt;

Mel_Gpu_Pipeline_Create_Result mel_gpu_pipeline_compute_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Compute_Opt opt);
#define mel_gpu_pipeline_compute_create(dev, ...) mel_gpu_pipeline_compute_create_opt((dev), (Mel_Gpu_Pipeline_Compute_Opt){ __VA_ARGS__ })

typedef struct
{
    const char*                         source;
    const char*                         vertex_entry;
    const char*                         fragment_entry;
    Mel_Gpu_Topology                    topology;
    Mel_Gpu_Cull                        cull;
    Mel_Gpu_Front_Face                  front_face;
    Mel_Gpu_Fill                        fill;
    Mel_Gpu_Format                      color_format;
    const Mel_Gpu_Color_Target*         color_targets;
    u32                                 color_target_count;
    f32                                 blend_constants[4];
    Mel_Gpu_Format                      depth_format;
    const Mel_Gpu_Depth_Stencil*        depth_stencil;
    bool                                depth_bias;
    f32                                 depth_bias_constant, depth_bias_clamp, depth_bias_slope;
    u32                                 samples;
    bool                                alpha_to_coverage;
    bool                                sample_shading;
    f32                                 min_sample_shading;
    const Mel_Gpu_Vertex_Buffer_Layout* vertex_buffers;
    u32                                 vertex_buffer_count;
    bool                                bindless;
    const Mel_Gpu_Bind_Group_Layout*    set_layouts;
    u32                                 set_layout_count;
    const Mel_Gpu_Static_Sampler*       static_samplers;
    u32                                 static_sampler_count;
    const Mel_Gpu_Spec_Constant*        spec_constants;
    u32                                 spec_constant_count;
    const char*                         name;
} Mel_Gpu_Pipeline_Slang_Opt;

typedef struct
{
    Mel_Gpu_Pipeline               value;
    Mel_Gpu_Shader                 shader;
    Mel_Gpu_Pipeline_Create_Status status;
} Mel_Gpu_Pipeline_From_Slang_Result;

Mel_Gpu_Pipeline_From_Slang_Result mel_gpu_pipeline_create_from_slang_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Slang_Opt opt);
#define mel_gpu_pipeline_create_from_slang(dev, ...) mel_gpu_pipeline_create_from_slang_opt((dev), (Mel_Gpu_Pipeline_Slang_Opt){ __VA_ARGS__ })

typedef struct
{
    const char*                      source;
    const char*                      compute_entry;
    u32                              push_constant_size;
    bool                             bindless;
    const Mel_Gpu_Bind_Group_Layout* set_layouts;
    u32                              set_layout_count;
    const Mel_Gpu_Spec_Constant*     spec_constants;
    u32                              spec_constant_count;
    const char*                      name;
} Mel_Gpu_Pipeline_Compute_Slang_Opt;

Mel_Gpu_Pipeline_From_Slang_Result mel_gpu_pipeline_compute_create_from_slang_opt(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline_Compute_Slang_Opt opt);
#define mel_gpu_pipeline_compute_create_from_slang(dev, ...) mel_gpu_pipeline_compute_create_from_slang_opt((dev), (Mel_Gpu_Pipeline_Compute_Slang_Opt){ __VA_ARGS__ })

void mel_gpu_pipeline_destroy(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe);
bool mel_gpu_pipeline_alive(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe);
