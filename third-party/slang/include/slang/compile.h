#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MEL_SLANG_TARGET_SPIRV = 0,
    MEL_SLANG_TARGET_MSL = 1,
    MEL_SLANG_TARGET_DXIL = 2,
    MEL_SLANG_TARGET_WGSL = 3,
} Mel_Slang_Target;

typedef enum
{
    MEL_SLANG_STAGE_VERTEX = 0,
    MEL_SLANG_STAGE_FRAGMENT = 1,
    MEL_SLANG_STAGE_COMPUTE = 2,
} Mel_Slang_Stage;

typedef enum
{
    MEL_SLANG_FORMAT_UNKNOWN = 0,
    MEL_SLANG_FORMAT_F32 = 1,
    MEL_SLANG_FORMAT_F32X2 = 2,
    MEL_SLANG_FORMAT_F32X3 = 3,
    MEL_SLANG_FORMAT_F32X4 = 4,
    MEL_SLANG_FORMAT_I32 = 5,
    MEL_SLANG_FORMAT_I32X2 = 6,
    MEL_SLANG_FORMAT_I32X3 = 7,
    MEL_SLANG_FORMAT_I32X4 = 8,
    MEL_SLANG_FORMAT_U32 = 9,
    MEL_SLANG_FORMAT_U32X2 = 10,
    MEL_SLANG_FORMAT_U32X3 = 11,
    MEL_SLANG_FORMAT_U32X4 = 12,
} Mel_Slang_Vertex_Format;

typedef enum
{
    MEL_SLANG_RESOURCE_UNKNOWN = 0,
    MEL_SLANG_RESOURCE_UNIFORM_BUFFER = 1,
    MEL_SLANG_RESOURCE_SAMPLED_TEXTURE = 2,
    MEL_SLANG_RESOURCE_STORAGE_TEXTURE = 3,
    MEL_SLANG_RESOURCE_SAMPLER = 4,
    MEL_SLANG_RESOURCE_STORAGE_BUFFER = 5,
} Mel_Slang_Resource_Kind;

typedef struct
{
    char*                   semantic;
    uint32_t                location;
    Mel_Slang_Vertex_Format format;
    uint32_t                offset;
    uint32_t                size;
} Mel_Slang_Vertex_Attr;

typedef struct
{
    char*                   name;
    Mel_Slang_Resource_Kind kind;
    uint32_t                set;
    uint32_t                slot;
    uint32_t                count;
    uint32_t                size;
} Mel_Slang_Resource_Binding;

/* MEL_FLAG(metal-bindless-reflection): additive Metal-only argument-buffer layout.
   When a push-constant struct authored with a DescriptorHandle field is compiled to
   the Metal target, Slang lowers the WHOLE struct to ONE mixed argument buffer at the
   entry point's buffer(0): DescriptorHandle fields become inlined argument-buffer
   resources, scalar fields become inline uniform members. The host must build that
   argument buffer per dispatch. These fields describe, in source declaration order,
   each member's role so the Metal RHI resolves resource slots and copies uniforms.

   `host_offset` is the member's byte offset within the host-supplied push-constant
   struct (the consumer ABI: a DescriptorHandle field occupies a 4-byte slot index,
   matching the Vulkan/WGSL lane where the same field is a `uint`). `arg_index` is the
   Metal argument-buffer member index used with MTLArgumentEncoder
   (setTexture:/setBuffer:atIndex: for resources, constantDataAtIndex: for uniforms).
   `is_uniform` selects between the two roles; `size` is the host byte span consumed.
   This is populated only for MEL_SLANG_TARGET_MSL; other targets leave it empty. */
typedef struct
{
    Mel_Slang_Resource_Kind kind;
    int                     is_uniform;
    uint32_t                host_offset;
    uint32_t                arg_index;
    uint32_t                size;
} Mel_Slang_Metal_Arg_Field;

typedef struct
{
    char*           entry;
    Mel_Slang_Stage stage;

    Mel_Slang_Vertex_Attr* vertex_attrs;
    uint32_t               vertex_attr_count;
    uint32_t               vertex_stride;

    Mel_Slang_Resource_Binding* bindings;
    uint32_t                    binding_count;

    Mel_Slang_Metal_Arg_Field* metal_arg_fields;
    uint32_t                   metal_arg_field_count;
    int                        metal_arg_buffer;

    uint32_t push_constant_size;

    uint32_t workgroup[3];
    int      is_compute;
} Mel_Slang_Reflection;

typedef struct
{
    void*  data;
    size_t size;
    char*  diagnostics;
} Mel_Slang_Blob;

Mel_Slang_Blob mel_slang_compile(const char* source, const char* entry, Mel_Slang_Stage stage, Mel_Slang_Target target);
Mel_Slang_Blob mel_slang_compile_reflect(const char* source, const char* entry, Mel_Slang_Stage stage, Mel_Slang_Target target, Mel_Slang_Reflection* out_reflection);

void mel_slang_blob_free(Mel_Slang_Blob* blob);
void mel_slang_reflection_free(Mel_Slang_Reflection* reflection);

#ifdef __cplusplus
}
#endif
