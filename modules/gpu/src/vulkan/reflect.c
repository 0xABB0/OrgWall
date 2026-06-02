#include "vk_backend.h"

#include <allocator/allocator.h>
#include <log/log.h>

// U12 reflection (gpu-rhi.md §6.4). A focused single-pass SPIR-V reader: enough to let U13 derive the
// pipeline layout from the shader rather than from hand-declared sizes — the push-constant block size, the
// set-0 descriptor bounds (so MissingBindlessSlot is reachable and a runtime-array set 0 is told apart from
// an app-owned classic one), the vertex-input layout, and the specialization constants. The shader is the
// source of truth for its bindings; this is the default path, with the explicit pipeline-opt fields as the
// P2 override.
//
// SPIR-V logical layout puts annotations (OpDecorate / OpMemberDecorate) before the type/constant/variable
// section, so a single linear pass resolves every decoration before the variable that consumes it, and a
// type's size from members that are, by the type-before-use rule, already defined when the type is reached.

#define SPV_MAGIC 0x07230203u

enum
{
    SpvOpDecorate = 71,
    SpvOpMemberDecorate = 72,
    SpvOpTypeBool = 20,
    SpvOpTypeInt = 21,
    SpvOpTypeFloat = 22,
    SpvOpTypeVector = 23,
    SpvOpTypeMatrix = 24,
    SpvOpTypeImage = 25,
    SpvOpTypeSampler = 26,
    SpvOpTypeSampledImage = 27,
    SpvOpTypeArray = 28,
    SpvOpTypeRuntimeArray = 29,
    SpvOpTypeStruct = 30,
    SpvOpTypePointer = 32,
    SpvOpConstant = 43,
    SpvOpSpecConstantTrue = 48,
    SpvOpSpecConstantFalse = 49,
    SpvOpSpecConstant = 50,
    SpvOpVariable = 59,
};

enum
{
    SpvDecorationSpecId = 1,
    SpvDecorationArrayStride = 6,
    SpvDecorationBuiltIn = 11,
    SpvDecorationLocation = 30,
    SpvDecorationBinding = 33,
    SpvDecorationDescriptorSet = 34,
    SpvDecorationOffset = 35,
};

enum
{
    SpvStorageClassInput = 1,
    SpvStorageClassPushConstant = 9,
};

enum
{
    SpvBaseKindNone = 0,
    SpvBaseKindFloat = 1,
    SpvBaseKindSint = 2,
    SpvBaseKindUint = 3,
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

// A 2/3/4-component float vector maps to the matching vertex format; everything else (scalar float, integer
// inputs) has no entry in the current format enum, so reflection-derived vertex input bows out loudly and
// the caller supplies an explicit layout (MEL-ENGINE-VIII, no silent default).
static Mel_Gpu_Format mel_spv__vertex_format(u8 kind, u8 comps)
{
    if (kind == SpvBaseKindFloat)
        switch (comps)
        {
        case 2: return MEL_GPU_FORMAT_RG32_FLOAT;
        case 3: return MEL_GPU_FORMAT_RGB32_FLOAT;
        case 4: return MEL_GPU_FORMAT_RGBA32_FLOAT;
        default: break;
        }
    return MEL_GPU_FORMAT_UNDEFINED;
}

static void mel_spv__push_set0(const Mel_Alloc* a, Mel_Gpu_Spirv_Reflection* r, u32 binding, u32 array_len, bool runtime)
{
    for (u32 i = 0; i < r->set0_count; i++)
        if (r->set0[i].binding == binding)
        {
            // Union across stages: keep the widest demand and the heap signature if either stage sees it.
            if (array_len > r->set0[i].array_len)
                r->set0[i].array_len = array_len;
            r->set0[i].runtime_array = r->set0[i].runtime_array || runtime;
            return;
        }
    u32 cap = r->set0_count + 1;
    r->set0 = r->set0 ? mel_realloc(a, r->set0, sizeof(*r->set0) * cap) : mel_alloc(a, sizeof(*r->set0) * cap);
    r->set0[r->set0_count++] = (Mel_Gpu_Reflect_Set0_Binding){ .binding = binding, .array_len = array_len, .runtime_array = runtime };
}

static void mel_spv__push_spec(const Mel_Alloc* a, Mel_Gpu_Spirv_Reflection* r, u32 id, u32 bytes)
{
    for (u32 i = 0; i < r->spec_constant_count; i++)
        if (r->spec_constants[i].id == id)
            return;
    u32 cap = r->spec_constant_count + 1;
    r->spec_constants = r->spec_constants ? mel_realloc(a, r->spec_constants, sizeof(*r->spec_constants) * cap) : mel_alloc(a, sizeof(*r->spec_constants) * cap);
    r->spec_constants[r->spec_constant_count++] = (Mel_Gpu_Reflect_Spec_Constant){ .id = id, .bytes = bytes };
}

static void mel_spv__push_vertex_attr(const Mel_Alloc* a, Mel_Gpu_Spirv_Reflection* r, u32 location, Mel_Gpu_Format fmt)
{
    u32 cap = r->vertex_attr_count + 1;
    r->vertex_attrs = r->vertex_attrs ? mel_realloc(a, r->vertex_attrs, sizeof(*r->vertex_attrs) * cap) : mel_alloc(a, sizeof(*r->vertex_attrs) * cap);
    r->vertex_attrs[r->vertex_attr_count++] = (Mel_Gpu_Reflect_Vertex_Attr){ .location = location, .format = fmt, .offset = 0 };
}

void mel_gpu__spirv_reflect(const u32* code, usize size_bytes, bool vertex_stage, const Mel_Alloc* alloc, Mel_Gpu_Spirv_Reflection* accum)
{
    accum->alloc = alloc;
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
    u8*  base_kind = mel_calloc(alloc, sizeof(u8) * bound);  // scalar base kind for vertex formats
    u8*  components = mel_calloc(alloc, sizeof(u8) * bound); // vector component count (1 for scalar)

    // Array shape per type id (for set-0 descriptor bounds).
    u8*  type_is_array = mel_calloc(alloc, sizeof(u8) * bound);
    u8*  type_is_runtime = mel_calloc(alloc, sizeof(u8) * bound);
    u32* type_array_len = mel_calloc(alloc, sizeof(u32) * bound);

    // Decorations carried on a result id until the defining instruction is reached.
    u8*  has_set = mel_calloc(alloc, sizeof(u8) * bound);
    u32* dec_set = mel_calloc(alloc, sizeof(u32) * bound);
    u32* dec_binding = mel_calloc(alloc, sizeof(u32) * bound);
    u8*  has_location = mel_calloc(alloc, sizeof(u8) * bound);
    u32* dec_location = mel_calloc(alloc, sizeof(u32) * bound);
    u8*  is_builtin = mel_calloc(alloc, sizeof(u8) * bound);
    u8*  has_spec_id = mel_calloc(alloc, sizeof(u8) * bound);
    u32* dec_spec_id = mel_calloc(alloc, sizeof(u32) * bound);

    Mel_Spv_Member_Offset* offs = NULL;
    u32                    off_count = 0, off_cap = 0;

    u32  pc_struct_id = 0; // struct type whose pointer is used by a PushConstant variable
    bool vertex_ok = true; // cleared if an input attribute's type has no reflectable format

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
            if (wc >= 4 && w[2] == SpvDecorationDescriptorSet)
            {
                has_set[w[1] % bound] = 1;
                dec_set[w[1] % bound] = w[3];
            }
            if (wc >= 4 && w[2] == SpvDecorationBinding)
                dec_binding[w[1] % bound] = w[3];
            if (wc >= 4 && w[2] == SpvDecorationLocation)
            {
                has_location[w[1] % bound] = 1;
                dec_location[w[1] % bound] = w[3];
            }
            if (wc >= 3 && w[2] == SpvDecorationBuiltIn)
                is_builtin[w[1] % bound] = 1;
            if (wc >= 4 && w[2] == SpvDecorationSpecId)
            {
                has_spec_id[w[1] % bound] = 1;
                dec_spec_id[w[1] % bound] = w[3];
            }
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
        case SpvOpTypeBool:
            // bool occupies 4 bytes (VkBool32) in specialization data.
            if (wc >= 2)
            {
                type_size[w[1] % bound] = 4;
                base_kind[w[1] % bound] = SpvBaseKindUint;
                components[w[1] % bound] = 1;
            }
            break;
        case SpvOpTypeInt:
            // w[1]=id, w[2]=width(bits), w[3]=signedness
            if (wc >= 4)
            {
                type_size[w[1] % bound] = w[2] / 8;
                base_kind[w[1] % bound] = w[3] ? SpvBaseKindSint : SpvBaseKindUint;
                components[w[1] % bound] = 1;
            }
            break;
        case SpvOpTypeFloat:
            // w[1]=id, w[2]=width(bits)
            if (wc >= 3)
            {
                type_size[w[1] % bound] = w[2] / 8;
                base_kind[w[1] % bound] = SpvBaseKindFloat;
                components[w[1] % bound] = 1;
            }
            break;
        case SpvOpTypeVector:
            // w[1]=id, w[2]=componentType, w[3]=count
            if (wc >= 4)
            {
                type_size[w[1] % bound] = type_size[w[2] % bound] * w[3];
                base_kind[w[1] % bound] = base_kind[w[2] % bound];
                components[w[1] % bound] = (u8)w[3];
            }
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
        case SpvOpSpecConstant:
            // w[1]=type, w[2]=id, w[3..]=value — a scalar specialization constant.
            if (wc >= 4 && has_spec_id[w[2] % bound])
                mel_spv__push_spec(alloc, accum, dec_spec_id[w[2] % bound], type_size[w[1] % bound] ? type_size[w[1] % bound] : 4);
            break;
        case SpvOpSpecConstantTrue:
        case SpvOpSpecConstantFalse:
            // w[1]=boolType, w[2]=id — a boolean specialization constant (4-byte VkBool32).
            if (wc >= 3 && has_spec_id[w[2] % bound])
                mel_spv__push_spec(alloc, accum, dec_spec_id[w[2] % bound], 4);
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
                type_is_array[w[1] % bound] = 1;
                type_array_len[w[1] % bound] = len;
            }
            break;
        }
        case SpvOpTypeRuntimeArray:
            // w[1]=id, w[2]=elementType — an unbounded (descriptor-indexing) array.
            if (wc >= 3)
                type_is_runtime[w[1] % bound] = 1;
            break;
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
        {
            // w[1]=resultType(pointer), w[2]=id, w[3]=storageClass
            if (wc < 4)
                break;
            u32 ptr = w[1] % bound;
            u32 var = w[2] % bound;
            u32 sc = w[3];
            if (sc == SpvStorageClassPushConstant)
            {
                if (is_push_ptr[ptr])
                    pc_struct_id = ptr_pointee[ptr];
            }
            else if (has_set[var] && dec_set[var] == 0)
            {
                // A descriptor declared at set 0 (the engine bindless heap). Unwrap the pointee to read its
                // array shape: runtime array = heap signature; sized array length feeds MissingBindlessSlot.
                u32  pointee = ptr_pointee[ptr];
                bool runtime = type_is_runtime[pointee] != 0;
                u32  len = type_is_array[pointee] ? type_array_len[pointee] : 1;
                mel_spv__push_set0(alloc, accum, dec_binding[var], len, runtime);
            }
            else if (vertex_stage && sc == SpvStorageClassInput && has_location[var] && !is_builtin[var])
            {
                // A vertex-stage input bound to a vertex buffer (builtins like gl_VertexIndex carry BuiltIn,
                // not Location, and are skipped). The pointee type gives the attribute format.
                u32            pointee = ptr_pointee[ptr];
                Mel_Gpu_Format fmt = mel_spv__vertex_format(base_kind[pointee], components[pointee]);
                if (fmt == MEL_GPU_FORMAT_UNDEFINED)
                {
                    mel_log_warn("gpu", "reflect: vertex input at location %u has no reflectable format (scalar/integer); supply an explicit vertex_layout", dec_location[var]);
                    vertex_ok = false;
                }
                else
                    mel_spv__push_vertex_attr(alloc, accum, dec_location[var], fmt);
            }
            break;
        }
        default:
            break;
        }
        i += wc;
    }

    if (pc_struct_id && type_size[pc_struct_id % bound] > accum->push_constant_size)
        accum->push_constant_size = type_size[pc_struct_id % bound];

    // uses_bindless_set is specifically the runtime-descriptor-array signature, so a sized/app-owned set 0
    // (the classic P2 path, §6.7) is no longer mistaken for the heap.
    for (u32 s = 0; s < accum->set0_count; s++)
        if (accum->set0[s].runtime_array)
            accum->uses_bindless_set = true;

    // Resolve the vertex layout: sort by location, tight-pack offsets, sum the stride. If any input was not
    // reflectable, withdraw the whole derived layout rather than emit a half-correct one.
    if (vertex_stage)
    {
        if (!vertex_ok && accum->vertex_attrs)
        {
            mel_dealloc(alloc, accum->vertex_attrs);
            accum->vertex_attrs = NULL;
            accum->vertex_attr_count = 0;
        }
        for (u32 a = 1; a < accum->vertex_attr_count; a++)
        {
            Mel_Gpu_Reflect_Vertex_Attr key = accum->vertex_attrs[a];
            u32                         j = a;
            while (j > 0 && accum->vertex_attrs[j - 1].location > key.location)
            {
                accum->vertex_attrs[j] = accum->vertex_attrs[j - 1];
                j--;
            }
            accum->vertex_attrs[j] = key;
        }
        u32 offset = 0;
        for (u32 a = 0; a < accum->vertex_attr_count; a++)
        {
            accum->vertex_attrs[a].offset = offset;
            offset += mel_gpu_format_bytes(accum->vertex_attrs[a].format);
        }
        accum->vertex_stride = offset;
    }

    mel_dealloc(alloc, type_size);
    mel_dealloc(alloc, ptr_pointee);
    mel_dealloc(alloc, is_push_ptr);
    mel_dealloc(alloc, const_value);
    mel_dealloc(alloc, array_stride);
    mel_dealloc(alloc, base_kind);
    mel_dealloc(alloc, components);
    mel_dealloc(alloc, type_is_array);
    mel_dealloc(alloc, type_is_runtime);
    mel_dealloc(alloc, type_array_len);
    mel_dealloc(alloc, has_set);
    mel_dealloc(alloc, dec_set);
    mel_dealloc(alloc, dec_binding);
    mel_dealloc(alloc, has_location);
    mel_dealloc(alloc, dec_location);
    mel_dealloc(alloc, is_builtin);
    mel_dealloc(alloc, has_spec_id);
    mel_dealloc(alloc, dec_spec_id);
    if (offs)
        mel_dealloc(alloc, offs);
}

void mel_gpu__reflection_free(Mel_Gpu_Spirv_Reflection* r)
{
    if (!r->alloc)
        return;
    if (r->set0)
        mel_dealloc(r->alloc, r->set0);
    if (r->vertex_attrs)
        mel_dealloc(r->alloc, r->vertex_attrs);
    if (r->spec_constants)
        mel_dealloc(r->alloc, r->spec_constants);
    r->set0 = NULL;
    r->vertex_attrs = NULL;
    r->spec_constants = NULL;
    r->set0_count = r->vertex_attr_count = r->spec_constant_count = 0;
}
