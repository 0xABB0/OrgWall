#include "anim.registry.h"
#include "allocator.h"
#include "hash.xxh.h"
#include "math.quat.h"

#include <string.h>

typedef struct {
    Mel_Track_Type_Def* items;
    u32 count;
    u32 capacity;
    const Mel_Alloc* alloc;
} Mel__Registry;

static Mel__Registry g_registry;

static void mel_batch_f32_lerp(const void* restrict a, const void* restrict b,
                               void* restrict out, const f32* restrict t, u32 count)
{
    const f32* fa = a;
    const f32* fb = b;
    f32* fo = out;
    for (u32 i = 0; i < count; i++)
        fo[i] = fa[i] + (fb[i] - fa[i]) * t[i];
}

static void mel_batch_f32_additive(const void* restrict base, const void* restrict additive,
                                   const void* restrict reference, void* restrict out,
                                   const f32* restrict weight, u32 count)
{
    const f32* fb = base;
    const f32* fa = additive;
    const f32* fr = reference;
    f32* fo = out;
    for (u32 i = 0; i < count; i++)
        fo[i] = fb[i] + (fa[i] - fr[i]) * weight[i];
}

static void mel_batch_vec2_lerp(const void* restrict a, const void* restrict b,
                                void* restrict out, const f32* restrict t, u32 count)
{
    const f32* fa = a;
    const f32* fb = b;
    f32* fo = out;
    for (u32 i = 0; i < count; i++)
    {
        u32 off = i * 2;
        fo[off + 0] = fa[off + 0] + (fb[off + 0] - fa[off + 0]) * t[i];
        fo[off + 1] = fa[off + 1] + (fb[off + 1] - fa[off + 1]) * t[i];
    }
}

static void mel_batch_vec2_additive(const void* restrict base, const void* restrict additive,
                                    const void* restrict reference, void* restrict out,
                                    const f32* restrict weight, u32 count)
{
    const f32* fb = base;
    const f32* fa = additive;
    const f32* fr = reference;
    f32* fo = out;
    for (u32 i = 0; i < count; i++)
    {
        u32 off = i * 2;
        fo[off + 0] = fb[off + 0] + (fa[off + 0] - fr[off + 0]) * weight[i];
        fo[off + 1] = fb[off + 1] + (fa[off + 1] - fr[off + 1]) * weight[i];
    }
}

static void mel_batch_vec3_lerp(const void* restrict a, const void* restrict b,
                                void* restrict out, const f32* restrict t, u32 count)
{
    const f32* fa = a;
    const f32* fb = b;
    f32* fo = out;
    for (u32 i = 0; i < count; i++)
    {
        u32 off = i * 3;
        fo[off + 0] = fa[off + 0] + (fb[off + 0] - fa[off + 0]) * t[i];
        fo[off + 1] = fa[off + 1] + (fb[off + 1] - fa[off + 1]) * t[i];
        fo[off + 2] = fa[off + 2] + (fb[off + 2] - fa[off + 2]) * t[i];
    }
}

static void mel_batch_vec3_additive(const void* restrict base, const void* restrict additive,
                                    const void* restrict reference, void* restrict out,
                                    const f32* restrict weight, u32 count)
{
    const f32* fb = base;
    const f32* fa = additive;
    const f32* fr = reference;
    f32* fo = out;
    for (u32 i = 0; i < count; i++)
    {
        u32 off = i * 3;
        fo[off + 0] = fb[off + 0] + (fa[off + 0] - fr[off + 0]) * weight[i];
        fo[off + 1] = fb[off + 1] + (fa[off + 1] - fr[off + 1]) * weight[i];
        fo[off + 2] = fb[off + 2] + (fa[off + 2] - fr[off + 2]) * weight[i];
    }
}

static void mel_batch_vec4_lerp(const void* restrict a, const void* restrict b,
                                void* restrict out, const f32* restrict t, u32 count)
{
    const f32* fa = a;
    const f32* fb = b;
    f32* fo = out;
    for (u32 i = 0; i < count; i++)
    {
        u32 off = i * 4;
        fo[off + 0] = fa[off + 0] + (fb[off + 0] - fa[off + 0]) * t[i];
        fo[off + 1] = fa[off + 1] + (fb[off + 1] - fa[off + 1]) * t[i];
        fo[off + 2] = fa[off + 2] + (fb[off + 2] - fa[off + 2]) * t[i];
        fo[off + 3] = fa[off + 3] + (fb[off + 3] - fa[off + 3]) * t[i];
    }
}

static void mel_batch_vec4_additive(const void* restrict base, const void* restrict additive,
                                    const void* restrict reference, void* restrict out,
                                    const f32* restrict weight, u32 count)
{
    const f32* fb = base;
    const f32* fa = additive;
    const f32* fr = reference;
    f32* fo = out;
    for (u32 i = 0; i < count; i++)
    {
        u32 off = i * 4;
        fo[off + 0] = fb[off + 0] + (fa[off + 0] - fr[off + 0]) * weight[i];
        fo[off + 1] = fb[off + 1] + (fa[off + 1] - fr[off + 1]) * weight[i];
        fo[off + 2] = fb[off + 2] + (fa[off + 2] - fr[off + 2]) * weight[i];
        fo[off + 3] = fb[off + 3] + (fa[off + 3] - fr[off + 3]) * weight[i];
    }
}

static void mel_batch_quat_slerp(const void* restrict a, const void* restrict b,
                                 void* restrict out, const f32* restrict t, u32 count)
{
    const f32* fa = a;
    const f32* fb = b;
    f32* fo = out;
    for (u32 i = 0; i < count; i++)
    {
        Mel_Quat qa, qb;
        memcpy(&qa, &fa[i * 4], sizeof(Mel_Quat));
        memcpy(&qb, &fb[i * 4], sizeof(Mel_Quat));
        Mel_Quat result = mel_quat_slerp(qa, qb, t[i]);
        memcpy(&fo[i * 4], &result, sizeof(Mel_Quat));
    }
}

static void mel_batch_quat_additive(const void* restrict base, const void* restrict additive,
                                    const void* restrict reference, void* restrict out,
                                    const f32* restrict weight, u32 count)
{
    const f32* fb = base;
    const f32* fa = additive;
    const f32* fr = reference;
    f32* fo = out;
    for (u32 i = 0; i < count; i++)
    {
        Mel_Quat qbase, qadd, qref;
        memcpy(&qbase, &fb[i * 4], sizeof(Mel_Quat));
        memcpy(&qadd, &fa[i * 4], sizeof(Mel_Quat));
        memcpy(&qref, &fr[i * 4], sizeof(Mel_Quat));

        Mel_Quat diff = mel_quat_mul(mel_quat_inverse(qref), qadd);
        Mel_Quat blended = mel_quat_slerp(MEL_QUAT_IDENTITY, diff, weight[i]);
        Mel_Quat result = mel_quat_mul(qbase, blended);
        memcpy(&fo[i * 4], &result, sizeof(Mel_Quat));
    }
}

static void mel__registry_push(Mel_Track_Type_Def def)
{
    if (g_registry.count >= g_registry.capacity)
    {
        u32 new_cap = g_registry.capacity == 0 ? 8 : g_registry.capacity * 2;
        usize new_size = sizeof(Mel_Track_Type_Def) * new_cap;
        if (g_registry.items == NULL)
            g_registry.items = mel_alloc(g_registry.alloc, new_size);
        else
            g_registry.items = mel_realloc(g_registry.alloc, g_registry.items, new_size);
        g_registry.capacity = new_cap;
    }
    g_registry.items[g_registry.count++] = def;
}

void mel_anim_registry_init(const Mel_Alloc* alloc)
{
    assert(alloc != NULL);

    g_registry = (Mel__Registry){0};
    g_registry.alloc = alloc;

    mel_anim_registry_register(MEL_ANIM_TYPE_F32,  sizeof(f32),     mel_batch_f32_lerp,   mel_batch_f32_additive);
    mel_anim_registry_register(MEL_ANIM_TYPE_VEC2, sizeof(f32) * 2, mel_batch_vec2_lerp,  mel_batch_vec2_additive);
    mel_anim_registry_register(MEL_ANIM_TYPE_VEC3, sizeof(f32) * 3, mel_batch_vec3_lerp,  mel_batch_vec3_additive);
    mel_anim_registry_register(MEL_ANIM_TYPE_VEC4, sizeof(f32) * 4, mel_batch_vec4_lerp,  mel_batch_vec4_additive);
    mel_anim_registry_register(MEL_ANIM_TYPE_QUAT, sizeof(f32) * 4, mel_batch_quat_slerp, mel_batch_quat_additive);
}

void mel_anim_registry_register(u64 type_hash, u32 stride,
                                Mel_Batch_Lerp_Fn lerp_fn,
                                Mel_Batch_Additive_Fn additive_fn)
{
    assert(g_registry.alloc != NULL);
    assert(stride > 0);
    assert(lerp_fn != NULL);

    mel__registry_push((Mel_Track_Type_Def){
        .type_hash = type_hash,
        .stride = stride,
        .lerp_fn = lerp_fn,
        .additive_fn = additive_fn,
    });
}

Mel_Track_Type_Def* mel_anim_registry_get(u64 type_hash)
{
    for (u32 i = 0; i < g_registry.count; i++)
    {
        if (g_registry.items[i].type_hash == type_hash)
            return &g_registry.items[i];
    }

    assert(false && "animation type not registered");
    return NULL;
}
