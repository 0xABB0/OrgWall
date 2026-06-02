#include "d3d_backend.h"

#include <log/log.h>

#include <string.h>

// U12 reflection (gpu-rhi.md §6.4) — a self-contained DXIL container reader. D3D12's in-process reflection
// facility (dxcompiler.dll IDxcUtils::CreateReflection) needs dxcapi.h, which is not on the in-box SDK
// INCLUDE path; injecting the DXC SDK would violate the floor's "in-box only" constraint. So the engine
// reads the input signature straight out of the DXIL container — the documented "DXIL container reader"
// path from §6.4 — to supply the reflection-default vertex layout. Push-constant size and the bindless flag
// stay explicit on the pipeline opt (the §6.5 manual-layout P2 peer); deriving them from the b0 root-constant
// cbuffer would need the full reflection blob and is the additive DXC-reflection tier (flagged).
//
// Container layout (Microsoft DxilContainer.h):
//   DxilContainerHeader { u32 FourCC='DXBC'; u8 Hash[16]; u16 Major; u16 Minor; u32 Size; u32 PartCount; }
//   followed by u32 PartOffset[PartCount]; each part is { u32 FourCC; u32 PartSize; } + PartSize bytes.
// The input-signature part is 'ISG1' (SM6, 32-byte elements) or legacy 'ISGN' (24-byte elements):
//   DxilProgramSignature { u32 ParamCount; u32 ParamOffset; } + ParamCount elements.

#define MEL_DXIL_FOURCC(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

static u32 mel_dxil__rd32(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }

// SystemValue codes: 0 = D3D_NAME_UNDEFINED (a real user vertex attribute). Anything else is a system value
// (SV_VertexID / SV_InstanceID etc.) and must not appear in the input-assembler layout.
// CompType codes: 3 = float32. Only float vec2/3/4 are reflectable (the Mel_Gpu_Format enum's vertex subset),
// matching the Vulkan reflection floor; anything else withdraws the whole derived layout (no half layout).
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
        return; // not a DXIL container (e.g. raw bytecode without a header) — reflection yields nothing

    // header: FourCC(4) + Hash(16) + Major(2) + Minor(2) + Size(4) + PartCount(4) = 32 bytes
    u32 part_count = mel_dxil__rd32(data + 28);
    if ((usize)32 + (usize)part_count * 4 > bytes)
        return;

    const u8* sig = NULL;
    usize     sig_size = 0;
    u32       elem_stride = 0; // 32 for ISG1, 24 for ISGN
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
        // ISG1 prepends a u32 Stream; ISGN starts at SemanticName.
        u32 base = elem_stride == 32 ? 4 : 0;
        u32 name_off = mel_dxil__rd32(e + base + 0);
        u32 sem_index = mel_dxil__rd32(e + base + 4);
        u32 sys_value = mel_dxil__rd32(e + base + 8);
        u32 comp_type = mel_dxil__rd32(e + base + 12);
        u32 reg = mel_dxil__rd32(e + base + 16);
        u8  mask = *(e + base + 20);

        if (sys_value != 0)
            continue; // a system value (SV_VertexID, ...) is not an input-assembler attribute

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
