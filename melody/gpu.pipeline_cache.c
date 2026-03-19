#include "gpu.pipeline_cache.h"
#include "gpu.device.h"
#include "gpu.shader.h"
#include "hash.xxh.h"
#include "allocator.h"
#include "allocator.heap.h"

#include <string.h>

static Mel_Gpu_Pipeline_State mel__pso_pack(Mel_Gpu_Pipeline_Opt* opt)
{
    Mel_Gpu_Pipeline_State s = 0;
    s |= ((u64)(opt->blend_mode   & MEL_PSO_BLEND_MASK))    << MEL_PSO_BLEND_SHIFT;
    s |= ((u64)(opt->cull_mode    & MEL_PSO_CULL_MASK))     << MEL_PSO_CULL_SHIFT;
    s |= ((u64)(opt->topology     & MEL_PSO_TOPOLOGY_MASK)) << MEL_PSO_TOPOLOGY_SHIFT;
    s |= ((u64)(opt->depth_test  ? 1 : 0))                  << MEL_PSO_DEPTH_TEST_SHIFT;
    s |= ((u64)(opt->depth_write ? 1 : 0))                  << MEL_PSO_DEPTH_WRITE_SHIFT;
    s |= ((u64)(opt->pipeline_type & MEL_PSO_PIPE_TYPE_MASK)) << MEL_PSO_PIPE_TYPE_SHIFT;
    s |= ((u64)(opt->depth_compare & MEL_PSO_DEPTH_CMP_OP_MASK)) << MEL_PSO_DEPTH_CMP_OP_SHIFT;
    s |= ((u64)(opt->dynamic_cull_mode ? 1 : 0))            << MEL_PSO_DYN_CULL_SHIFT;
    s |= ((u64)(opt->use_texture ? 1 : 0))                  << MEL_PSO_USE_TEX_SHIFT;
    return s;
}

Mel_Gpu_Pipeline_State mel_gpu_pipeline_state_pack(Mel_Gpu_Pipeline_Opt* opt)
{
    assert(opt != nullptr);
    return mel__pso_pack(opt);
}

static u64 mel__cache_hash_vertex_in(Mel_Gpu_Pipeline_Opt* opt)
{
    u64 h = 0;
    u32 topo = opt->topology;
    h = mel_xxh3_64_seeded(&topo, sizeof(topo), h);

    if (opt->binding_count > 0 && opt->bindings != nullptr)
        h = mel_xxh3_64_seeded(opt->bindings, opt->binding_count * sizeof(Mel_Gpu_Vertex_Binding), h);

    if (opt->attribute_count > 0 && opt->attributes != nullptr)
        h = mel_xxh3_64_seeded(opt->attributes, opt->attribute_count * sizeof(Mel_Gpu_Vertex_Attribute), h);

    return h;
}

static u64 mel__cache_hash_shaders(Mel_Gpu_Pipeline_Opt* opt)
{
    u64 h = 0;

    void* shader_ptr = opt->shader;
    h = mel_xxh3_64_seeded(&shader_ptr, sizeof(void*), h);

    Mel_Gpu_Pipeline_State state = mel__pso_pack(opt);
    h = mel_xxh3_64_seeded(&state, sizeof(state), h);

    h = mel_xxh3_64_seeded(&opt->push_constant_size, sizeof(u32), h);
    h = mel_xxh3_64_seeded(&opt->push_constant_stages, sizeof(Mel_Gpu_Shader_Stage), h);

    if (opt->descriptor_binding_count > 0 && opt->descriptor_bindings != nullptr)
        h = mel_xxh3_64_seeded(opt->descriptor_bindings,
            opt->descriptor_binding_count * sizeof(Mel_Gpu_Descriptor_Binding), h);

    if (opt->extra_set_layout_count > 0 && opt->extra_set_layouts != nullptr)
        h = mel_xxh3_64_seeded(opt->extra_set_layouts,
            opt->extra_set_layout_count * sizeof(void*), h);

    h = mel_xxh3_64_seeded(&opt->max_descriptor_sets, sizeof(u32), h);

    return h;
}

static u64 mel__cache_hash_fragment_out(Mel_Gpu_Pipeline_Opt* opt)
{
    u64 h = 0;

    h = mel_xxh3_64_seeded(&opt->blend_mode, sizeof(u32), h);
    h = mel_xxh3_64_seeded(&opt->color_format, sizeof(Mel_Gpu_Format), h);

    if (opt->color_format_count > 0 && opt->color_formats != nullptr)
        h = mel_xxh3_64_seeded(opt->color_formats,
            opt->color_format_count * sizeof(Mel_Gpu_Format), h);

    h = mel_xxh3_64_seeded(&opt->depth_format, sizeof(Mel_Gpu_Format), h);

    return h;
}

Mel_Gpu_Pipeline_Cache_Key mel_gpu_pipeline_cache_key(Mel_Gpu_Pipeline_Opt* opt)
{
    assert(opt != nullptr);
    return (Mel_Gpu_Pipeline_Cache_Key){
        .vertex_in_hash    = mel__cache_hash_vertex_in(opt),
        .shader_hash       = mel__cache_hash_shaders(opt),
        .fragment_out_hash = mel__cache_hash_fragment_out(opt),
    };
}

static u64 mel__cache_map_hash(const void* key)
{
    return mel_xxh3_64(key, sizeof(Mel_Gpu_Pipeline_Cache_Key));
}

static bool mel__cache_map_eq(const void* a, const void* b)
{
    return memcmp(a, b, sizeof(Mel_Gpu_Pipeline_Cache_Key)) == 0;
}

void mel_gpu_pipeline_cache_init(Mel_Gpu_Pipeline_Cache* cache, const Mel_Alloc* alloc)
{
    assert(cache != nullptr);

    const Mel_Alloc* a = alloc ? alloc : mel_alloc_heap();
    *cache = (Mel_Gpu_Pipeline_Cache){0};
    cache->alloc = a;
    mel_hashmap_init(&cache->map, mel__cache_map_hash, mel__cache_map_eq, a);
}

void mel_gpu_pipeline_cache_shutdown(Mel_Gpu_Pipeline_Cache* cache, Mel_Gpu_Device* dev)
{
    assert(cache != nullptr);
    assert(dev != nullptr);

    for (u32 i = 0; i < cache->entry_count; i++)
    {
        mel_gpu_pipeline_shutdown(cache->entries[i].pipeline, dev);
        mel_dealloc(cache->alloc, cache->entries[i].pipeline);
        mel_dealloc(cache->alloc, cache->entries[i].key);
    }

    if (cache->entries)
        mel_dealloc(cache->alloc, cache->entries);

    mel_hashmap_free(&cache->map);
    *cache = (Mel_Gpu_Pipeline_Cache){0};
}

Mel_Gpu_Pipeline* mel_gpu_pipeline_cache_get_opt(
    Mel_Gpu_Pipeline_Cache* cache,
    Mel_Gpu_Device* dev,
    Mel_Gpu_Pipeline_Opt opt)
{
    assert(cache != nullptr);
    assert(dev != nullptr);
    assert(opt.shader != nullptr);

    Mel_Gpu_Pipeline_Cache_Key key = mel_gpu_pipeline_cache_key(&opt);

    void* existing = mel_hashmap_get(&cache->map, &key);
    if (existing)
        return (Mel_Gpu_Pipeline*)existing;

    Mel_Gpu_Pipeline* pipeline = mel_alloc(cache->alloc, sizeof(Mel_Gpu_Pipeline));
    mel_gpu_pipeline_init_opt(pipeline, dev, opt);

    Mel_Gpu_Pipeline_Cache_Key* heap_key = mel_alloc(cache->alloc, sizeof(Mel_Gpu_Pipeline_Cache_Key));
    *heap_key = key;

    if (cache->entry_count >= cache->entry_capacity)
    {
        u32 new_cap = cache->entry_capacity == 0 ? 8 : cache->entry_capacity * 2;
        Mel_Gpu_Pipeline_Cache_Entry* new_entries = mel_alloc(cache->alloc,
            new_cap * sizeof(Mel_Gpu_Pipeline_Cache_Entry));

        if (cache->entry_count > 0)
            memcpy(new_entries, cache->entries, cache->entry_count * sizeof(Mel_Gpu_Pipeline_Cache_Entry));

        if (cache->entries)
            mel_dealloc(cache->alloc, cache->entries);

        cache->entries = new_entries;
        cache->entry_capacity = new_cap;
    }

    cache->entries[cache->entry_count++] = (Mel_Gpu_Pipeline_Cache_Entry){
        .pipeline = pipeline,
        .key = heap_key,
    };

    mel_hashmap_put(&cache->map, heap_key, pipeline);

    return pipeline;
}
