#include <test/test.h>

#if MEL_GPU_VULKAN

#include <gpu/device.h>
#include <gpu/caps.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/sampler.h>
#include <gpu/binding.h>
#include <gpu/pipeline.h>
#include <gpu/shader.h>
#include <gpu/queue.h>
#include <gpu/command.h>
#include <gpu/memory.h>
#include <gpu/format.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <time/nano.h>

#include <stdio.h>

#include "bindless_spv.h"

static Mel_Gpu_Device* bench_make_device(Mel_Gpu_Instance** out_inst, bool bindless)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-bench");
    if (!inst)
        return NULL;
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    if (n == 0)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(inst, adapters[0], .features = { .timeline_semaphores = true, .descriptor_indexing = bindless });
    if (!dr.value)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    *out_inst = inst;
    return dr.value;
}

static void bench_drain(Mel_Gpu_Device* dev, Mel_Gpu_Queue* q)
{
    (void)dev;
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    mel_gpu_future_wait(f);
    mel_gpu_future_destroy(f);
    mel_gpu_command_list_destroy(cmd);
}

static int bench_cmp_u64(const void* a, const void* b)
{
    u64 x = *(const u64*)a, y = *(const u64*)b;
    return (x > y) - (x < y);
}

typedef struct
{
    u64 median_ns;
    u64 mean_ns;
    u64 min_ns;
} Bench_Stats;

static Bench_Stats bench_stats(u64* samples, u32 count)
{
    qsort(samples, count, sizeof *samples, bench_cmp_u64);
    u64 sum = 0;
    for (u32 i = 0; i < count; i++)
        sum += samples[i];
    Bench_Stats s = { .median_ns = samples[count / 2], .mean_ns = sum / count, .min_ns = samples[0] };
    return s;
}

static void bench_report_ops(const char* name, u32 n, u64 ns_per_op)
{
    f64 us = (f64)ns_per_op / 1000.0;
    f64 kops = ns_per_op ? (1.0e6 / (f64)ns_per_op) : 0.0;
    printf("bench: %-22s N=%-7u %8.3f us/op  %10.1f k ops/s\n", name, n, us, kops);
    fflush(stdout);
}

static void bench_report_bw(const char* name, usize bytes, u64 ns)
{
    f64 mb = (f64)bytes / (1024.0 * 1024.0);
    f64 gbs = ns ? ((f64)bytes / (f64)ns) : 0.0;
    printf("bench: %-22s bytes=%-9zu %8.2f MiB  %8.3f GB/s  (%llu us)\n", name, bytes, mb, gbs, (unsigned long long)(ns / 1000));
    fflush(stdout);
}

MEL_TEST(bench, buffer_create)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = bench_make_device(&inst, false);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32        warmup = 256;
    const u32        n = 8000;
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Buffer*  live = mel_alloc_array(alloc, Mel_Gpu_Buffer, warmup + n);

    for (u32 i = 0; i < warmup; i++)
    {
        Mel_Gpu_Buffer_Create_Result r = mel_gpu_buffer_create(dev, .size = 256, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_DEVICE, .name = "warm");
        live[i] = r.value;
    }

    u64 t0 = mel_nanos_since_unspecified_epoch();
    for (u32 i = 0; i < n; i++)
    {
        Mel_Gpu_Buffer_Create_Result r = mel_gpu_buffer_create(dev, .size = 256, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_DEVICE, .name = "b");
        MEL_REQUIRE(!mel_gpu_failed(r.status));
        live[warmup + i] = r.value;
    }
    u64 dt = mel_nanos_since_unspecified_epoch() - t0;
    u64 per = dt / n;
    bench_report_ops("buffer_create", n, per);

    for (u32 i = 0; i < warmup + n; i++)
        mel_gpu_buffer_destroy(dev, live[i]);
    mel_dealloc(alloc, live);

    MEL_EXPECT_LT(per, 200000u);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(bench, buffer_create_destroy_pair)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = bench_make_device(&inst, false);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 warmup = 256;
    const u32 n = 8000;

    for (u32 i = 0; i < warmup; i++)
    {
        Mel_Gpu_Buffer_Create_Result r = mel_gpu_buffer_create(dev, .size = 256, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_DEVICE, .name = "warm");
        mel_gpu_buffer_destroy(dev, r.value);
    }

    u64 t0 = mel_nanos_since_unspecified_epoch();
    for (u32 i = 0; i < n; i++)
    {
        Mel_Gpu_Buffer_Create_Result r = mel_gpu_buffer_create(dev, .size = 256, .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_DEVICE, .name = "b");
        MEL_REQUIRE(!mel_gpu_failed(r.status));
        mel_gpu_buffer_destroy(dev, r.value);
    }
    u64 dt = mel_nanos_since_unspecified_epoch() - t0;
    u64 per = dt / n;
    bench_report_ops("buffer_create+destroy", n, per);

    MEL_EXPECT_LT(per, 400000u);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(bench, texture_create)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = bench_make_device(&inst, false);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32        warmup = 128;
    const u32        n = 3000;
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Texture* live = mel_alloc_array(alloc, Mel_Gpu_Texture, warmup + n);

    for (u32 i = 0; i < warmup; i++)
    {
        Mel_Gpu_Texture_Create_Result r = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 64, 64, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_SAMPLED, .name = "warm");
        live[i] = r.value;
    }

    u64 t0 = mel_nanos_since_unspecified_epoch();
    for (u32 i = 0; i < n; i++)
    {
        Mel_Gpu_Texture_Create_Result r = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 64, 64, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_SAMPLED, .name = "t");
        MEL_REQUIRE(!mel_gpu_failed(r.status));
        live[warmup + i] = r.value;
    }
    u64 dt = mel_nanos_since_unspecified_epoch() - t0;
    u64 per = dt / n;
    bench_report_ops("texture_create", n, per);

    for (u32 i = 0; i < warmup + n; i++)
        mel_gpu_texture_destroy(dev, live[i]);
    mel_dealloc(alloc, live);

    MEL_EXPECT_LT(per, 500000u);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(bench, texture_view_create)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = bench_make_device(&inst, false);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Texture_Create_Result tex = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 64, 64, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_SAMPLED, .name = "tex");
    MEL_REQUIRE(!mel_gpu_failed(tex.status));

    const u32             warmup = 128;
    const u32             n = 4000;
    const Mel_Alloc*      alloc = mel_alloc_heap();
    Mel_Gpu_Texture_View* live = mel_alloc_array(alloc, Mel_Gpu_Texture_View, warmup + n);

    for (u32 i = 0; i < warmup; i++)
        live[i] = mel_gpu_texture_default_view(dev, tex.value).value;

    u64 t0 = mel_nanos_since_unspecified_epoch();
    for (u32 i = 0; i < n; i++)
    {
        Mel_Gpu_Texture_View_Create_Result r = mel_gpu_texture_default_view(dev, tex.value);
        MEL_REQUIRE(!mel_gpu_failed(r.status));
        live[warmup + i] = r.value;
    }
    u64 dt = mel_nanos_since_unspecified_epoch() - t0;
    u64 per = dt / n;
    bench_report_ops("texture_view_create", n, per);

    for (u32 i = 0; i < warmup + n; i++)
        mel_gpu_texture_view_destroy(dev, live[i]);
    mel_dealloc(alloc, live);
    mel_gpu_texture_destroy(dev, tex.value);

    MEL_EXPECT_LT(per, 300000u);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(bench, sampler_create)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = bench_make_device(&inst, true);
    MEL_REQUIRE_NOT_NULL(dev);
    if (!mel_gpu_bindless_available(dev))
    {
        mel_gpu_device_destroy(dev);
        mel_gpu_instance_destroy(inst);
        MEL_SKIP("descriptor-indexing floor absent");
    }

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    u32                 cap = caps->memory.bindless.max_sampler_slots;
    MEL_REQUIRE(cap > 0);

    u32 n = cap > 4096 ? 4000 : (cap - 1);
    MEL_REQUIRE(n > 0);

    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Sampler* live = mel_alloc_array(alloc, Mel_Gpu_Sampler, n);

    u64 t0 = mel_nanos_since_unspecified_epoch();
    for (u32 i = 0; i < n; i++)
    {
        Mel_Gpu_Sampler_Create_Result r = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST, .wrap_u = MEL_GPU_WRAP_REPEAT, .lod_min = (f32)i * 0.001f, .lod_max = 64.0f, .name = "s");
        MEL_REQUIRE(!mel_gpu_failed(r.status));
        live[i] = r.value;
    }
    u64 dt = mel_nanos_since_unspecified_epoch() - t0;
    u64 per = dt / n;
    bench_report_ops("sampler_create", n, per);

    for (u32 i = 0; i < n; i++)
        mel_gpu_sampler_destroy(dev, live[i]);
    mel_dealloc(alloc, live);

    MEL_EXPECT_LT(per, 400000u);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(bench, pipeline_create)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = bench_make_device(&inst, false);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV,
                                                                          .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = BINDLESS_SOLID_FRAG_SPV,
                                                                          .spirv_fragment_size = sizeof BINDLESS_SOLID_FRAG_SPV,
                                                                          .vertex_entry = "main",
                                                                          .fragment_entry = "main",
                                                                          .name = "solid");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    const u32         warmup = 16;
    const u32         n = 300;
    const Mel_Alloc*  alloc = mel_alloc_heap();
    Mel_Gpu_Pipeline* live = mel_alloc_array(alloc, Mel_Gpu_Pipeline, warmup + n);

    for (u32 i = 0; i < warmup; i++)
        live[i] = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .push_constant_size = 8, .name = "warm").value;

    u64 t0 = mel_nanos_since_unspecified_epoch();
    for (u32 i = 0; i < n; i++)
    {
        Mel_Gpu_Pipeline_Create_Result r = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .push_constant_size = 8, .name = "p");
        MEL_REQUIRE(!mel_gpu_failed(r.status));
        live[warmup + i] = r.value;
    }
    u64 dt = mel_nanos_since_unspecified_epoch() - t0;
    u64 per = dt / n;
    bench_report_ops("pipeline_create", n, per);

    for (u32 i = 0; i < warmup + n; i++)
        mel_gpu_pipeline_destroy(dev, live[i]);
    mel_dealloc(alloc, live);
    mel_gpu_shader_destroy(dev, sh.value);

    MEL_EXPECT_LT(per, 20000000u);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static void bench_upload_buffer(Mel_Gpu_Device* dev, usize bytes)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    u8*              src = mel_alloc(alloc, bytes);
    for (usize i = 0; i < bytes; i++)
        src[i] = (u8)i;

    Mel_Gpu_Buffer_Create_Result buf = mel_gpu_buffer_create(dev, .size = bytes, .usage = MEL_GPU_BUFFER_TRANSFER_DST | MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .name = "ul");
    MEL_REQUIRE(!mel_gpu_failed(buf.status));

    mel_gpu_buffer_write(dev, buf.value, src, bytes);

    const u32 reps = bytes < (1u << 20) ? 32 : 8;
    u64       t0 = mel_nanos_since_unspecified_epoch();
    for (u32 r = 0; r < reps; r++)
        mel_gpu_buffer_write(dev, buf.value, src, bytes);
    u64 dt = (mel_nanos_since_unspecified_epoch() - t0) / reps;
    bench_report_bw("buffer_write", bytes, dt);

    mel_gpu_buffer_destroy(dev, buf.value);
    mel_dealloc(alloc, src);
}

MEL_TEST(bench, buffer_write_bandwidth)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = bench_make_device(&inst, false);
    MEL_REQUIRE_NOT_NULL(dev);

    bench_upload_buffer(dev, 64u * 1024);
    bench_upload_buffer(dev, 1u * 1024 * 1024);
    bench_upload_buffer(dev, 16u * 1024 * 1024);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static void bench_upload_texture(Mel_Gpu_Device* dev, u32 dim)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    usize            bytes = (usize)dim * dim * 4;
    u8*              src = mel_alloc(alloc, bytes);
    for (usize i = 0; i < bytes; i++)
        src[i] = (u8)i;

    Mel_Gpu_Texture_Create_Result tex = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { dim, dim, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "ut");
    MEL_REQUIRE(!mel_gpu_failed(tex.status));

    Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { dim, dim, 1 } };
    mel_gpu_texture_write(dev, tex.value, region, src, bytes);

    const u32 reps = bytes < (1u << 20) ? 16 : 4;
    u64       t0 = mel_nanos_since_unspecified_epoch();
    for (u32 r = 0; r < reps; r++)
        mel_gpu_texture_write(dev, tex.value, region, src, bytes);
    u64 dt = (mel_nanos_since_unspecified_epoch() - t0) / reps;
    bench_report_bw("texture_write", bytes, dt);

    mel_gpu_texture_destroy(dev, tex.value);
    mel_dealloc(alloc, src);
}

MEL_TEST(bench, texture_write_bandwidth)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = bench_make_device(&inst, false);
    MEL_REQUIRE_NOT_NULL(dev);

    bench_upload_texture(dev, 128);
    bench_upload_texture(dev, 512);
    bench_upload_texture(dev, 1024);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(bench, submit_latency_empty)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = bench_make_device(&inst, false);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const u32        warmup = 64;
    const u32        n = 2000;
    const Mel_Alloc* alloc = mel_alloc_heap();
    u64*             samples = mel_alloc_array(alloc, u64, n);

    for (u32 i = 0; i < warmup; i++)
    {
        Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_list_count = 0 });
        mel_gpu_future_wait(f);
        mel_gpu_future_destroy(f);
    }

    for (u32 i = 0; i < n; i++)
    {
        u64             s = mel_nanos_since_unspecified_epoch();
        Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_list_count = 0 });
        MEL_REQUIRE_NOT_NULL(f);
        u32 st = mel_gpu_future_wait(f);
        MEL_REQUIRE(mel_gpu_ok(st));
        samples[i] = mel_nanos_since_unspecified_epoch() - s;
        mel_gpu_future_destroy(f);
    }

    Bench_Stats st = bench_stats(samples, n);
    bench_report_ops("submit_latency_empty", n, st.median_ns);
    printf("bench:   submit min=%llu us  median=%llu us  mean=%llu us\n", (unsigned long long)(st.min_ns / 1000), (unsigned long long)(st.median_ns / 1000), (unsigned long long)(st.mean_ns / 1000));
    fflush(stdout);

    mel_dealloc(alloc, samples);
    mel_gpu_queue_release(q);

    MEL_EXPECT_LT(st.median_ns, 50000000u);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(bench, allocator_churn)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = bench_make_device(&inst, false);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    Mel_Gpu_Memory_Budget base = mel_gpu_memory_budget(dev);

    const u32        n = 4000;
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Buffer*  live = mel_alloc_array(alloc, Mel_Gpu_Buffer, n);

    u64 peak_usage = base.usage_bytes;
    u64 sum_sizes = 0;
    for (u32 i = 0; i < n; i++)
    {
        usize sz = 1024 + (usize)((i * 2654435761u) % 60000u);
        sum_sizes += sz;
        Mel_Gpu_Buffer_Create_Result r = mel_gpu_buffer_create(dev, .size = sz, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .name = "churn");
        MEL_REQUIRE(!mel_gpu_failed(r.status));
        live[i] = r.value;
        if ((i & 63) == 0)
        {
            Mel_Gpu_Memory_Budget b = mel_gpu_memory_budget(dev);
            if (b.usage_bytes > peak_usage)
                peak_usage = b.usage_bytes;
        }
    }

    Mel_Gpu_Memory_Budget full = mel_gpu_memory_budget(dev);
    if (full.usage_bytes > peak_usage)
        peak_usage = full.usage_bytes;

    u64 live_bytes = full.usage_bytes - base.usage_bytes;
    f64 overhead = sum_sizes ? ((f64)live_bytes / (f64)sum_sizes) : 0.0;

    printf("bench: allocator_churn       N=%-7u live=%llu MiB requested=%llu MiB overhead=%.2fx peak=%llu MiB\n",
           n,
           (unsigned long long)(live_bytes >> 20),
           (unsigned long long)(sum_sizes >> 20),
           overhead,
           (unsigned long long)(peak_usage >> 20));
    fflush(stdout);

    for (u32 i = 0; i < n; i++)
        mel_gpu_buffer_destroy(dev, live[i]);

    Mel_Gpu_Memory_Budget pre_drain = mel_gpu_memory_budget(dev);
    u64                   deferred = pre_drain.usage_bytes > base.usage_bytes ? pre_drain.usage_bytes - base.usage_bytes : 0;

    bench_drain(dev, q);
    bench_drain(dev, q);

    Mel_Gpu_Memory_Budget after = mel_gpu_memory_budget(dev);
    u64                   residual = after.usage_bytes > base.usage_bytes ? after.usage_bytes - base.usage_bytes : 0;
    printf("bench:   churn deferred-before-drain=%llu MiB  budget-residual-after-drain=%llu MiB  (base=%llu MiB budget=%llu MiB)\n",
           (unsigned long long)(deferred >> 20),
           (unsigned long long)(residual >> 20),
           (unsigned long long)(base.usage_bytes >> 20),
           (unsigned long long)(after.budget_bytes >> 20));
    printf("bench:   note: usage_bytes is the driver heapUsage high-water (VK_EXT_memory_budget); MoltenVK/Metal\n");
    printf("bench:         defers VRAM release, so the budget number lags the engine's deferred-free retirement.\n");
    fflush(stdout);

    mel_dealloc(alloc, live);
    mel_gpu_queue_release(q);

    MEL_EXPECT(overhead < 4.0);
    MEL_EXPECT(full.budget_bytes > 0);
    MEL_EXPECT_LE(residual, peak_usage - base.usage_bytes);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(bench, bindless_registration)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = bench_make_device(&inst, true);
    MEL_REQUIRE_NOT_NULL(dev);
    if (!mel_gpu_bindless_available(dev))
    {
        mel_gpu_device_destroy(dev);
        mel_gpu_instance_destroy(inst);
        MEL_SKIP("descriptor-indexing floor absent");
    }

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    u32                 cap = caps->memory.bindless.max_texture_view_slots;
    MEL_REQUIRE(cap > 0);

    Mel_Gpu_Texture_Create_Result tex = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 8, 8, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_SAMPLED, .name = "heaptex");
    MEL_REQUIRE(!mel_gpu_failed(tex.status));

    u32 n = cap > 8192 ? 8000 : (cap - 1);
    MEL_REQUIRE(n > 0);

    const Mel_Alloc*      alloc = mel_alloc_heap();
    Mel_Gpu_Texture_View* live = mel_alloc_array(alloc, Mel_Gpu_Texture_View, n);

    const u32 warmup = n < 256 ? (n / 4) : 256;
    for (u32 i = 0; i < warmup; i++)
    {
        live[i] = mel_gpu_texture_default_view(dev, tex.value).value;
        (void)mel_gpu_texture_view_bindless_slot(dev, live[i]);
    }

    u64 t0 = mel_nanos_since_unspecified_epoch();
    for (u32 i = warmup; i < n; i++)
    {
        Mel_Gpu_Texture_View_Create_Result r = mel_gpu_texture_default_view(dev, tex.value);
        MEL_REQUIRE(!mel_gpu_failed(r.status));
        live[i] = r.value;
        (void)mel_gpu_texture_view_bindless_slot(dev, r.value);
    }
    u64 dt = mel_nanos_since_unspecified_epoch() - t0;
    u64 per = dt / (n - warmup);
    bench_report_ops("bindless_register", n - warmup, per);

    u64 td0 = mel_nanos_since_unspecified_epoch();
    for (u32 i = 0; i < n; i++)
        mel_gpu_texture_view_destroy(dev, live[i]);
    u64 td = (mel_nanos_since_unspecified_epoch() - td0) / n;
    bench_report_ops("bindless_unregister", n, td);

    mel_dealloc(alloc, live);
    mel_gpu_texture_destroy(dev, tex.value);

    MEL_EXPECT_LT(per, 300000u);
    MEL_EXPECT_LT(td, 300000u);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(bench, summary)
{
    printf("\n");
    printf("bench: ==== gpu-bench summary ====\n");
    printf("bench: clock      = mel_nanos_since_unspecified_epoch (monotonic CPU wall, ns)\n");
    printf("bench: backend    = vulkan (MoltenVK on this host); submit is synchronous on the floor\n");
    printf("bench: measures   = CPU-side wall time per op; uploads are synchronous-staging cost\n");
    printf("bench: NOT-MEASURED = GPU execution time — needs timestamp query pools (caps.queries.*),\n");
    printf("bench:               not yet implemented in the RHI (U-queries follow-up slice)\n");
    printf("bench: ===========================\n");
    fflush(stdout);
}

#else

MEL_TEST(bench, unsupported_backend) { MEL_SKIP("gpu-bench requires the vulkan backend (build with --gpu=vulkan)"); }

#endif
