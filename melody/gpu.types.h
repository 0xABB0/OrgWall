#pragma once

#include "core.types.h"

typedef u32 Mel_Gpu_Format;

#define MEL_GPU_FORMAT_UNDEFINED                   0
#define MEL_GPU_FORMAT_R8_UNORM                    9
#define MEL_GPU_FORMAT_R8_SNORM                    10
#define MEL_GPU_FORMAT_R8_UINT                     13
#define MEL_GPU_FORMAT_R8_SINT                     14
#define MEL_GPU_FORMAT_R8G8_UNORM                  16
#define MEL_GPU_FORMAT_R8G8_SNORM                  17
#define MEL_GPU_FORMAT_R8G8_UINT                   20
#define MEL_GPU_FORMAT_R8G8_SINT                   21
#define MEL_GPU_FORMAT_R8G8B8_UNORM                23
#define MEL_GPU_FORMAT_R8G8B8_SNORM                24
#define MEL_GPU_FORMAT_R8G8B8_UINT                 27
#define MEL_GPU_FORMAT_R8G8B8_SINT                 28
#define MEL_GPU_FORMAT_B8G8R8_UNORM                30
#define MEL_GPU_FORMAT_R8G8B8A8_UNORM              37
#define MEL_GPU_FORMAT_R8G8B8A8_SNORM              38
#define MEL_GPU_FORMAT_R8G8B8A8_UINT               41
#define MEL_GPU_FORMAT_R8G8B8A8_SINT               42
#define MEL_GPU_FORMAT_R8G8B8A8_SRGB               43
#define MEL_GPU_FORMAT_B8G8R8A8_UNORM              44
#define MEL_GPU_FORMAT_B8G8R8A8_SRGB               50
#define MEL_GPU_FORMAT_A2B10G10R10_UNORM_PACK32    64
#define MEL_GPU_FORMAT_R16_UNORM                   70
#define MEL_GPU_FORMAT_R16_SNORM                   71
#define MEL_GPU_FORMAT_R16_UINT                    74
#define MEL_GPU_FORMAT_R16_SINT                    75
#define MEL_GPU_FORMAT_R16_SFLOAT                  76
#define MEL_GPU_FORMAT_R16G16_UNORM                77
#define MEL_GPU_FORMAT_R16G16_SNORM                78
#define MEL_GPU_FORMAT_R16G16_UINT                 81
#define MEL_GPU_FORMAT_R16G16_SINT                 82
#define MEL_GPU_FORMAT_R16G16_SFLOAT               83
#define MEL_GPU_FORMAT_R16G16B16_UNORM             84
#define MEL_GPU_FORMAT_R16G16B16_SFLOAT            90
#define MEL_GPU_FORMAT_R16G16B16A16_UNORM          91
#define MEL_GPU_FORMAT_R16G16B16A16_SNORM          92
#define MEL_GPU_FORMAT_R16G16B16A16_UINT           95
#define MEL_GPU_FORMAT_R16G16B16A16_SINT           96
#define MEL_GPU_FORMAT_R16G16B16A16_SFLOAT         97
#define MEL_GPU_FORMAT_R32_UINT                    98
#define MEL_GPU_FORMAT_R32_SINT                    99
#define MEL_GPU_FORMAT_R32_SFLOAT                  100
#define MEL_GPU_FORMAT_R32G32_UINT                 101
#define MEL_GPU_FORMAT_R32G32_SINT                 102
#define MEL_GPU_FORMAT_R32G32_SFLOAT               103
#define MEL_GPU_FORMAT_R32G32B32_UINT              104
#define MEL_GPU_FORMAT_R32G32B32_SINT              105
#define MEL_GPU_FORMAT_R32G32B32_SFLOAT            106
#define MEL_GPU_FORMAT_R32G32B32A32_UINT           107
#define MEL_GPU_FORMAT_R32G32B32A32_SINT           108
#define MEL_GPU_FORMAT_R32G32B32A32_SFLOAT         109
#define MEL_GPU_FORMAT_R64_SFLOAT                  112
#define MEL_GPU_FORMAT_R64G64_SFLOAT               115
#define MEL_GPU_FORMAT_D16_UNORM                   124
#define MEL_GPU_FORMAT_X8_D24_UNORM_PACK32         125
#define MEL_GPU_FORMAT_D32_SFLOAT                  126
#define MEL_GPU_FORMAT_S8_UINT                     127
#define MEL_GPU_FORMAT_D16_UNORM_S8_UINT           128
#define MEL_GPU_FORMAT_D24_UNORM_S8_UINT           129
#define MEL_GPU_FORMAT_D32_SFLOAT_S8_UINT          130
#define MEL_GPU_FORMAT_BC1_RGB_UNORM_BLOCK         131
#define MEL_GPU_FORMAT_BC1_RGB_SRGB_BLOCK          132
#define MEL_GPU_FORMAT_BC1_RGBA_UNORM_BLOCK        133
#define MEL_GPU_FORMAT_BC1_RGBA_SRGB_BLOCK         134
#define MEL_GPU_FORMAT_BC2_UNORM_BLOCK             135
#define MEL_GPU_FORMAT_BC2_SRGB_BLOCK              136
#define MEL_GPU_FORMAT_BC3_UNORM_BLOCK             137
#define MEL_GPU_FORMAT_BC3_SRGB_BLOCK              138
#define MEL_GPU_FORMAT_BC4_UNORM_BLOCK             139
#define MEL_GPU_FORMAT_BC4_SNORM_BLOCK             140
#define MEL_GPU_FORMAT_BC5_UNORM_BLOCK             141
#define MEL_GPU_FORMAT_BC5_SNORM_BLOCK             142
#define MEL_GPU_FORMAT_BC6H_UFLOAT_BLOCK           143
#define MEL_GPU_FORMAT_BC6H_SFLOAT_BLOCK           144
#define MEL_GPU_FORMAT_BC7_UNORM_BLOCK             145
#define MEL_GPU_FORMAT_BC7_SRGB_BLOCK              146
#define MEL_GPU_FORMAT_ETC2_R8G8B8_UNORM_BLOCK     147
#define MEL_GPU_FORMAT_ETC2_R8G8B8_SRGB_BLOCK      148
#define MEL_GPU_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK   149
#define MEL_GPU_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK    150
#define MEL_GPU_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK   151
#define MEL_GPU_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK    152
#define MEL_GPU_FORMAT_ASTC_4x4_UNORM_BLOCK        157
#define MEL_GPU_FORMAT_ASTC_4x4_SRGB_BLOCK         158

typedef u32 Mel_Gpu_Buffer_Usage;

#define MEL_GPU_BUFFER_USAGE_TRANSFER_SRC          (1u << 0)
#define MEL_GPU_BUFFER_USAGE_TRANSFER_DST          (1u << 1)
#define MEL_GPU_BUFFER_USAGE_UNIFORM               (1u << 4)
#define MEL_GPU_BUFFER_USAGE_STORAGE               (1u << 5)
#define MEL_GPU_BUFFER_USAGE_INDEX                  (1u << 6)
#define MEL_GPU_BUFFER_USAGE_VERTEX                (1u << 7)
#define MEL_GPU_BUFFER_USAGE_INDIRECT              (1u << 8)
#define MEL_GPU_BUFFER_USAGE_DEVICE_ADDRESS        (1u << 17)

typedef u32 Mel_Gpu_Image_Usage;

#define MEL_GPU_IMAGE_USAGE_TRANSFER_SRC           (1u << 0)
#define MEL_GPU_IMAGE_USAGE_TRANSFER_DST           (1u << 1)
#define MEL_GPU_IMAGE_USAGE_SAMPLED                (1u << 2)
#define MEL_GPU_IMAGE_USAGE_STORAGE                (1u << 3)
#define MEL_GPU_IMAGE_USAGE_COLOR_ATTACHMENT       (1u << 4)
#define MEL_GPU_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT (1u << 5)

typedef u32 Mel_Gpu_Stage;

#define MEL_GPU_STAGE_NONE                         0u
#define MEL_GPU_STAGE_TOP_OF_PIPE                  (1u << 0)
#define MEL_GPU_STAGE_DRAW_INDIRECT                (1u << 1)
#define MEL_GPU_STAGE_VERTEX_INPUT                 (1u << 2)
#define MEL_GPU_STAGE_VERTEX_SHADER                (1u << 3)
#define MEL_GPU_STAGE_FRAGMENT_SHADER              (1u << 4)
#define MEL_GPU_STAGE_EARLY_FRAGMENT_TESTS         (1u << 5)
#define MEL_GPU_STAGE_LATE_FRAGMENT_TESTS          (1u << 6)
#define MEL_GPU_STAGE_COLOR_ATTACHMENT_OUTPUT      (1u << 7)
#define MEL_GPU_STAGE_COMPUTE_SHADER               (1u << 8)
#define MEL_GPU_STAGE_TRANSFER                     (1u << 9)
#define MEL_GPU_STAGE_BOTTOM_OF_PIPE               (1u << 10)
#define MEL_GPU_STAGE_ALL_GRAPHICS                 (1u << 11)
#define MEL_GPU_STAGE_ALL_COMMANDS                 (1u << 12)

typedef u32 Mel_Gpu_Access;

#define MEL_GPU_ACCESS_NONE                        0u
#define MEL_GPU_ACCESS_INDIRECT_COMMAND_READ        (1u << 0)
#define MEL_GPU_ACCESS_INDEX_READ                  (1u << 1)
#define MEL_GPU_ACCESS_VERTEX_ATTRIBUTE_READ       (1u << 2)
#define MEL_GPU_ACCESS_UNIFORM_READ                (1u << 3)
#define MEL_GPU_ACCESS_SHADER_READ                 (1u << 4)
#define MEL_GPU_ACCESS_SHADER_WRITE                (1u << 5)
#define MEL_GPU_ACCESS_COLOR_ATTACHMENT_READ        (1u << 6)
#define MEL_GPU_ACCESS_COLOR_ATTACHMENT_WRITE       (1u << 7)
#define MEL_GPU_ACCESS_DEPTH_STENCIL_READ          (1u << 8)
#define MEL_GPU_ACCESS_DEPTH_STENCIL_WRITE         (1u << 9)
#define MEL_GPU_ACCESS_TRANSFER_READ               (1u << 10)
#define MEL_GPU_ACCESS_TRANSFER_WRITE              (1u << 11)
#define MEL_GPU_ACCESS_MEMORY_READ                 (1u << 12)
#define MEL_GPU_ACCESS_MEMORY_WRITE                (1u << 13)

typedef u32 Mel_Gpu_Image_Layout;

#define MEL_GPU_IMAGE_LAYOUT_UNDEFINED                     0
#define MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT              2
#define MEL_GPU_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT      3
#define MEL_GPU_IMAGE_LAYOUT_SHADER_READ_ONLY              5
#define MEL_GPU_IMAGE_LAYOUT_TRANSFER_SRC                  6
#define MEL_GPU_IMAGE_LAYOUT_TRANSFER_DST                  7
#define MEL_GPU_IMAGE_LAYOUT_PRESENT                       1000001002

typedef u32 Mel_Gpu_Aspect;

#define MEL_GPU_ASPECT_COLOR                       (1u << 0)
#define MEL_GPU_ASPECT_DEPTH                       (1u << 1)
#define MEL_GPU_ASPECT_STENCIL                     (1u << 2)

typedef u32 Mel_Gpu_Load_Op;

#define MEL_GPU_LOAD_OP_LOAD                       0
#define MEL_GPU_LOAD_OP_CLEAR                      1
#define MEL_GPU_LOAD_OP_DONT_CARE                  2

typedef u32 Mel_Gpu_Store_Op;

#define MEL_GPU_STORE_OP_STORE                     0
#define MEL_GPU_STORE_OP_DONT_CARE                 1

typedef u32 Mel_Gpu_Index_Type;

#define MEL_GPU_INDEX_TYPE_U16                     0
#define MEL_GPU_INDEX_TYPE_U32                     1

typedef u32 Mel_Gpu_Shader_Stage;

#define MEL_GPU_SHADER_STAGE_VERTEX                (1u << 0)
#define MEL_GPU_SHADER_STAGE_FRAGMENT              (1u << 4)
#define MEL_GPU_SHADER_STAGE_COMPUTE               (1u << 5)
#define MEL_GPU_SHADER_STAGE_TASK                  (1u << 6)
#define MEL_GPU_SHADER_STAGE_MESH                  (1u << 7)
#define MEL_GPU_SHADER_STAGE_ALL_GRAPHICS          0x0000001Fu

typedef u32 Mel_Gpu_Memory_Usage;

#define MEL_GPU_MEMORY_USAGE_AUTO                  0
#define MEL_GPU_MEMORY_USAGE_GPU_ONLY              1
#define MEL_GPU_MEMORY_USAGE_CPU_TO_GPU            2
#define MEL_GPU_MEMORY_USAGE_GPU_TO_CPU            3

typedef u32 Mel_Gpu_Present_Mode;

#define MEL_GPU_PRESENT_MODE_IMMEDIATE             0
#define MEL_GPU_PRESENT_MODE_MAILBOX               1
#define MEL_GPU_PRESENT_MODE_FIFO                  2
#define MEL_GPU_PRESENT_MODE_FIFO_RELAXED          3
