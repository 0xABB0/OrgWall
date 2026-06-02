#include "vk_backend.h"

#include <allocator/allocator.h>

// U12 reflection-lite (gpu-rhi.md §6.4). A focused SPIR-V reader: enough to let U13 derive the pipeline
// layout from the shader rather than from hand-declared sizes — the push-constant block size and whether
// the shader references the engine's bindless heap at descriptor set 0. The shader is the source of truth
// for its bindings; this is the default path, with the explicit pipeline-opt fields as the P2 override.
//
// SPIR-V logical layout puts annotations (OpMemberDecorate Offset, OpDecorate DescriptorSet) before the
// type/constant/variable section, so a single linear pass resolves a struct's size from member types that
// are, by the type-before-use rule, already defined when the struct is reached.

#define SPV_MAGIC 0x07230203u

enum
{
    SpvOpDecorate = 71,
    SpvOpMemberDecorate = 72,
    SpvOpTypeInt = 21,
    SpvOpTypeFloat = 22,
    SpvOpTypeVector = 23,
    SpvOpTypeMatrix = 24,
    SpvOpTypeArray = 28,
    SpvOpTypeStruct = 30,
    SpvOpTypePointer = 32,
    SpvOpConstant = 43,
    SpvOpVariable = 59,
};

enum
{
    SpvDecorationDescriptorSet = 34,
    SpvDecorationOffset = 35,
    SpvDecorationArrayStride = 6,
};

enum
{
    SpvStorageClassPushConstant = 9,
};

typedef struct
{
    u32 struct_id;
    u32 member;
    u32 offset;
} Mel_Spv_Member_Offset;

static u32 mel_spv__member_offset(const Mel_Spv_Member_Offset* offs, u32 n, u32 struct_id, u32 member)
{
    for (u32 i = 0; i < n; i++)
        if (offs[i].struct_id == struct_id && offs[i].member == member)
            return offs[i].offset;
    return 0;
}

void mel_gpu__spirv_reflect(const u32* code, usize size_bytes, const Mel_Alloc* alloc, Mel_Gpu_Spirv_Reflection* out)
{
    *out = (Mel_Gpu_Spirv_Reflection){ 0 };
    u32 word_count = (u32)(size_bytes / 4);
    if (!code || word_count < 5 || code[0] != SPV_MAGIC)
        return;

    u32 bound = code[3];
    if (bound == 0 || bound > (1u << 24))
        return;

    // Per-result-id tables, indexed by SPIR-V id (the id bound caps them).
    u32* type_size = mel_calloc(alloc, sizeof(u32) * bound); // byte size of a type id (0 = unknown)
    u32* ptr_pointee = mel_calloc(alloc, sizeof(u32) * bound);
    u8*  is_push_ptr = mel_calloc(alloc, sizeof(u8) * bound); // pointer type with PushConstant storage class
    u32* const_value = mel_calloc(alloc, sizeof(u32) * bound);
    u32* array_stride = mel_calloc(alloc, sizeof(u32) * bound);

    Mel_Spv_Member_Offset* offs = NULL;
    u32                    off_count = 0, off_cap = 0;

    u32 pc_struct_id = 0; // struct type whose pointer is used by a PushConstant variable

    u32 i = 5;
    while (i < word_count)
    {
        u32 first = code[i];
        u32 wc = first >> 16;
        u32 op = first & 0xffffu;
        if (wc == 0 || i + wc > word_count)
            break;
        const u32* w = &code[i];

        switch (op)
        {
        case SpvOpDecorate:
            // w[1]=target, w[2]=decoration, w[3..]=operands
            if (wc >= 4 && w[2] == SpvDecorationDescriptorSet && w[3] == 0)
                out->uses_bindless_set = true;
            if (wc >= 4 && w[2] == SpvDecorationArrayStride)
                array_stride[w[1] % bound] = w[3];
            break;
        case SpvOpMemberDecorate:
            // w[1]=structType, w[2]=member, w[3]=decoration, w[4..]=operands
            if (wc >= 5 && w[3] == SpvDecorationOffset)
            {
                if (off_count == off_cap)
                {
                    u32 cap = off_cap ? off_cap * 2 : 16;
                    offs = offs ? mel_realloc(alloc, offs, sizeof(Mel_Spv_Member_Offset) * cap) : mel_alloc(alloc, sizeof(Mel_Spv_Member_Offset) * cap);
                    off_cap = cap;
                }
                offs[off_count++] = (Mel_Spv_Member_Offset){ .struct_id = w[1], .member = w[2], .offset = w[4] };
            }
            break;
        case SpvOpTypeInt:
        case SpvOpTypeFloat:
            // w[1]=id, w[2]=width(bits)
            if (wc >= 3)
                type_size[w[1] % bound] = w[2] / 8;
            break;
        case SpvOpTypeVector:
            // w[1]=id, w[2]=componentType, w[3]=count
            if (wc >= 4)
                type_size[w[1] % bound] = type_size[w[2] % bound] * w[3];
            break;
        case SpvOpTypeMatrix:
            // w[1]=id, w[2]=columnType, w[3]=columnCount
            if (wc >= 4)
                type_size[w[1] % bound] = type_size[w[2] % bound] * w[3];
            break;
        case SpvOpConstant:
            // w[1]=type, w[2]=id, w[3]=value (low word)
            if (wc >= 4)
                const_value[w[2] % bound] = w[3];
            break;
        case SpvOpTypeArray:
        {
            // w[1]=id, w[2]=elementType, w[3]=lengthConstantId
            if (wc >= 4)
            {
                u32 len = const_value[w[3] % bound];
                u32 stride = array_stride[w[1] % bound];
                if (stride == 0)
                    stride = type_size[w[2] % bound];
                type_size[w[1] % bound] = stride * len;
            }
            break;
        }
        case SpvOpTypeStruct:
        {
            // w[1]=id, w[2..]=member type ids
            u32 members = wc - 2;
            u32 size = 0;
            for (u32 m = 0; m < members; m++)
            {
                u32 mt = w[2 + m];
                u32 moff = mel_spv__member_offset(offs, off_count, w[1], m);
                u32 end = moff + type_size[mt % bound];
                if (end > size)
                    size = end;
            }
            type_size[w[1] % bound] = size;
            break;
        }
        case SpvOpTypePointer:
            // w[1]=id, w[2]=storageClass, w[3]=type
            if (wc >= 4)
            {
                ptr_pointee[w[1] % bound] = w[3];
                if (w[2] == SpvStorageClassPushConstant)
                    is_push_ptr[w[1] % bound] = 1;
            }
            break;
        case SpvOpVariable:
            // w[1]=resultType(pointer), w[2]=id, w[3]=storageClass
            if (wc >= 4 && w[3] == SpvStorageClassPushConstant)
            {
                u32 ptr = w[1] % bound;
                if (is_push_ptr[ptr])
                    pc_struct_id = ptr_pointee[ptr];
            }
            break;
        default:
            break;
        }
        i += wc;
    }

    if (pc_struct_id)
        out->push_constant_size = type_size[pc_struct_id % bound];

    mel_dealloc(alloc, type_size);
    mel_dealloc(alloc, ptr_pointee);
    mel_dealloc(alloc, is_push_ptr);
    mel_dealloc(alloc, const_value);
    mel_dealloc(alloc, array_stride);
    if (offs)
        mel_dealloc(alloc, offs);
}
