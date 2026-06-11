#include "d3d_backend.h"

#include <log/log.h>

#include <string.h>

#define MEL_DXIL_FOURCC(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

static u32 mel_dxil__rd32(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }

static Mel_Gpu_Format mel_dxil__format(u32 comp_type, u32 mask)
{
    u32 components = 0;
    for (u32 i = 0; i < 4; i++)
        if (mask & (1u << i))
            components++;
    if (comp_type != 3)
        return MEL_GPU_FORMAT_UNDEFINED;
    switch (components)
    {
    case 2:
        return MEL_GPU_FORMAT_RG32_FLOAT;
    case 3:
        return MEL_GPU_FORMAT_RGB32_FLOAT;
    case 4:
        return MEL_GPU_FORMAT_RGBA32_FLOAT;
    default:
        return MEL_GPU_FORMAT_UNDEFINED;
    }
}

void mel_gpu__dxil_inputs_free(const Mel_Alloc* alloc, Mel_Gpu_Dxil_Input* inputs, u32 count)
{
    if (!inputs)
        return;
    for (u32 i = 0; i < count; i++)
        if (inputs[i].semantic)
            mel_dealloc(alloc, inputs[i].semantic);
    mel_dealloc(alloc, inputs);
}

void mel_gpu__dxil_reflect_inputs(const void* dxil, usize bytes, const Mel_Alloc* alloc, Mel_Gpu_Dxil_Input** out, u32* out_count, u32* out_stride)
{
    *out = NULL;
    *out_count = 0;
    *out_stride = 0;

    const u8* data = dxil;
    if (!data || bytes < 32 || mel_dxil__rd32(data) != MEL_DXIL_FOURCC('D', 'X', 'B', 'C'))
        return;

    u32 part_count = mel_dxil__rd32(data + 28);
    if ((usize)32 + (usize)part_count * 4 > bytes)
        return;

    const u8* sig = NULL;
    usize     sig_size = 0;
    u32       elem_stride = 0;
    for (u32 i = 0; i < part_count; i++)
    {
        u32 off = mel_dxil__rd32(data + 32 + i * 4);
        if ((usize)off + 8 > bytes)
            continue;
        u32 fourcc = mel_dxil__rd32(data + off);
        u32 psize = mel_dxil__rd32(data + off + 4);
        if ((usize)off + 8 + psize > bytes)
            continue;
        if (fourcc == MEL_DXIL_FOURCC('I', 'S', 'G', '1'))
        {
            sig = data + off + 8;
            sig_size = psize;
            elem_stride = 32;
            break;
        }
        if (fourcc == MEL_DXIL_FOURCC('I', 'S', 'G', 'N'))
        {
            sig = data + off + 8;
            sig_size = psize;
            elem_stride = 24;
        }
    }
    if (!sig || sig_size < 8)
        return;

    u32 param_count = mel_dxil__rd32(sig);
    u32 param_off = mel_dxil__rd32(sig + 4);
    if (param_count == 0 || (usize)param_off + (usize)param_count * elem_stride > sig_size)
        return;

    Mel_Gpu_Dxil_Input* inputs = mel_alloc(alloc, sizeof(Mel_Gpu_Dxil_Input) * param_count);
    u32                 count = 0;
    u32                 offset = 0;
    bool                ok = true;
    for (u32 i = 0; i < param_count && ok; i++)
    {
        const u8* e = sig + param_off + (usize)i * elem_stride;
        u32 base = elem_stride == 32 ? 4 : 0;
        u32 name_off = mel_dxil__rd32(e + base + 0);
        u32 sem_index = mel_dxil__rd32(e + base + 4);
        u32 sys_value = mel_dxil__rd32(e + base + 8);
        u32 comp_type = mel_dxil__rd32(e + base + 12);
        u32 reg = mel_dxil__rd32(e + base + 16);
        u8  mask = *(e + base + 20);

        if (sys_value != 0)
            continue;

        Mel_Gpu_Format fmt = mel_dxil__format(comp_type, mask);
        if (fmt == MEL_GPU_FORMAT_UNDEFINED)
        {
            mel_log_warn("gpu", "dxil_reflect: input has non-float or scalar format (comp=%u mask=0x%x); withdrawing derived vertex layout", comp_type, (unsigned)mask);
            ok = false;
            break;
        }
        if ((usize)name_off >= sig_size)
        {
            ok = false;
            break;
        }
        const char* name = (const char*)(sig + name_off);
        usize       maxn = sig_size - name_off;
        usize       nlen = 0;
        while (nlen < maxn && name[nlen] != 0)
            nlen++;

        char* dup = mel_alloc(alloc, nlen + 1);
        memcpy(dup, name, nlen);
        dup[nlen] = 0;

        u32 comps = (fmt == MEL_GPU_FORMAT_RG32_FLOAT ? 2 : fmt == MEL_GPU_FORMAT_RGB32_FLOAT ? 3 : 4);
        inputs[count++] = (Mel_Gpu_Dxil_Input){ .semantic = dup, .semantic_index = sem_index, .format = fmt, .input_register = reg, .offset = offset };
        offset += comps * 4;
    }

    if (!ok || count == 0)
    {
        mel_gpu__dxil_inputs_free(alloc, inputs, count);
        return;
    }
    *out = inputs;
    *out_count = count;
    *out_stride = offset;
}

u32 mel_gpu__dxil_reflect_test(const void* dxil, usize bytes, const Mel_Alloc* alloc, char (*semantics)[32], u32* sem_indices, i32* formats, u32* offsets, u32 max, u32* out_stride)
{
    Mel_Gpu_Dxil_Input* inputs = NULL;
    u32                 count = 0;
    u32                 stride = 0;
    mel_gpu__dxil_reflect_inputs(dxil, bytes, alloc, &inputs, &count, &stride);
    u32 n = count < max ? count : max;
    for (u32 i = 0; i < n; i++)
    {
        usize len = inputs[i].semantic ? strlen(inputs[i].semantic) : 0;
        if (len > 30)
            len = 30;
        if (inputs[i].semantic)
            memcpy(semantics[i], inputs[i].semantic, len);
        semantics[i][len] = 0;
        sem_indices[i] = inputs[i].semantic_index;
        formats[i] = (i32)inputs[i].format;
        offsets[i] = inputs[i].offset;
    }
    if (out_stride)
        *out_stride = stride;
    mel_gpu__dxil_inputs_free(alloc, inputs, count);
    return count;
}
