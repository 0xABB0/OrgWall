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
#include <gpu/rendering.h>
#include <gpu/state.h>
#include <gpu/memory.h>
#include <gpu/future.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>

#include <thread/thread.h>
#include <thread/barrier.h>
#include <thread/mutex.h>

#include <time/nano.h>

#include <log/log.h>

#include "bindless_spv.h"

#include <string.h>
#include <stdatomic.h>

static u32 stress_threads(void);

static Mel_Gpu_Device* stress_make_device(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-stress", .debug = { .enabled = true });
    if (!inst)
        return NULL;
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    if (n == 0)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(inst, adapters[0], .features = { .timeline_semaphores = true });
    if (!dr.value)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    *out_inst = inst;
    return dr.value;
}

static Mel_Gpu_Device* stress_make_device_bindless(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-stress", .debug = { .enabled = true });
    if (!inst)
        return NULL;
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    if (n == 0)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    Mel_Gpu_Device_Create_Result dr =
        mel_gpu_device_create(inst, adapters[0], .features = { .timeline_semaphores = true, .descriptor_indexing = true, .buffer_device_address = true });
    if (!dr.value)
    {
        mel_gpu_instance_destroy(inst);
        return NULL;
    }
    *out_inst = inst;
    return dr.value;
}

static void stress_drain(Mel_Gpu_Device* dev, Mel_Gpu_Queue* q)
{
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    mel_gpu_future_destroy(f);
    mel_gpu_command_list_destroy(cmd);
}

MEL_TEST(stress_churn, buffers_across_frames)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const u32 FRAMES = 64;
    const u32 PER_FRAME = 48;
    for (u32 frame = 0; frame < FRAMES; frame++)
    {
        Mel_Gpu_Buffer live[PER_FRAME];
        for (u32 i = 0; i < PER_FRAME; i++)
        {
            usize                        sz = 64 + ((frame * 7 + i * 13) % 4096);
            Mel_Gpu_Buffer_Create_Result r = mel_gpu_buffer_create(dev, .size = sz, .usage = MEL_GPU_BUFFER_VERTEX | MEL_GPU_BUFFER_TRANSFER_DST,
                                                                    .memory = (i & 1) ? MEL_GPU_MEMORY_UPLOAD : MEL_GPU_MEMORY_DEVICE, .name = "churn");
            MEL_REQUIRE(!mel_gpu_failed(r.status));
            live[i] = r.value;
        }
        stress_drain(dev, q);
        Mel_Gpu_Buffer stale = live[0];
        for (u32 i = 0; i < PER_FRAME; i++)
            mel_gpu_buffer_destroy(dev, live[i]);
        MEL_EXPECT(!mel_gpu_buffer_alive(dev, stale));
        stress_drain(dev, q);
    }

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(stress_churn, mixed_resources)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const u32 ROUNDS = 40;
    for (u32 r = 0; r < ROUNDS; r++)
    {
        Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 8 + (r % 16), 8, 1 },
                                                                 .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "ct");
        MEL_REQUIRE(!mel_gpu_failed(t.status));
        Mel_Gpu_Texture_View_Create_Result v = mel_gpu_texture_default_view(dev, t.value);
        MEL_REQUIRE(!mel_gpu_failed(v.status));
        Mel_Gpu_Sampler_Create_Result s = mel_gpu_sampler_create(dev, .min_filter = (r & 1) ? MEL_GPU_FILTER_LINEAR : MEL_GPU_FILTER_NEAREST,
                                                                 .mag_filter = MEL_GPU_FILTER_NEAREST, .wrap_u = MEL_GPU_WRAP_REPEAT, .name = "cs");
        MEL_REQUIRE(!mel_gpu_failed(s.status));

        MEL_EXPECT_EQ(mel_gpu_texture_view_bindless_slot(dev, v.value), v.value.slot.index);
        MEL_EXPECT_EQ(mel_gpu_sampler_bindless_slot(dev, s.value), s.value.slot.index);

        stress_drain(dev, q);
        mel_gpu_sampler_destroy(dev, s.value);
        mel_gpu_texture_view_destroy(dev, v.value);
        mel_gpu_texture_destroy(dev, t.value);
        stress_drain(dev, q);
    }

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(stress_churn, view_slot_reclaim_reuse)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 4, 4, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                             .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "rt");
    MEL_REQUIRE(!mel_gpu_failed(t.status));

    Mel_Gpu_Texture_View_Create_Result a = mel_gpu_texture_default_view(dev, t.value);
    MEL_REQUIRE(!mel_gpu_failed(a.status));
    u32 slot_a = a.value.slot.index;

    stress_drain(dev, q);
    mel_gpu_texture_view_destroy(dev, a.value);
    stress_drain(dev, q);

    Mel_Gpu_Texture_View_Create_Result b = mel_gpu_texture_default_view(dev, t.value);
    MEL_REQUIRE(!mel_gpu_failed(b.status));
    MEL_EXPECT_EQ(b.value.slot.index, slot_a);
    MEL_EXPECT_EQ(mel_gpu_texture_view_bindless_slot(dev, b.value), slot_a);

    stress_drain(dev, q);
    mel_gpu_texture_view_destroy(dev, b.value);
    mel_gpu_texture_destroy(dev, t.value);
    stress_drain(dev, q);

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(stress_bindless, fill_sampler_class_under_cap)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    u32                 cap = caps->memory.bindless.max_sampler_slots;
    MEL_REQUIRE(cap >= 64);

    u32 n = cap / 4 < 256 ? cap / 4 : 256;
    Mel_Gpu_Sampler* samps = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Sampler, n);
    for (u32 i = 0; i < n; i++)
    {
        Mel_Gpu_Sampler_Create_Result s = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                                 .wrap_u = (Mel_Gpu_Wrap)(i % 4), .wrap_v = (Mel_Gpu_Wrap)((i / 4) % 4),
                                                                 .lod_max = (f32)(i + 1), .name = "fill");
        MEL_REQUIRE(!mel_gpu_failed(s.status));
        samps[i] = s.value;
        MEL_EXPECT_EQ(mel_gpu_sampler_bindless_slot(dev, s.value), s.value.slot.index);
        MEL_EXPECT(s.value.slot.index < cap);
    }
    for (u32 i = 0; i < n; i++)
        mel_gpu_sampler_destroy(dev, samps[i]);
    mel_dealloc(mel_alloc_heap(), samps);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(stress_bindless, oversize_pipeline_is_graceful)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_from_bytecode(dev,
                                                                          .spirv_vertex = BINDLESS_VERT_SPV, .spirv_vertex_size = sizeof BINDLESS_VERT_SPV,
                                                                          .spirv_fragment = BINDLESS_OVERSIZE_FRAG_SPV, .spirv_fragment_size = sizeof BINDLESS_OVERSIZE_FRAG_SPV,
                                                                          .vertex_entry = "main", .fragment_entry = "main", .name = "oversize");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));

    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_create(dev, .shader = sh.value, .color_format = MEL_GPU_FORMAT_RGBA8_UNORM, .name = "oversize");
    MEL_EXPECT_EQ((u32)pipe.status, (u32)MEL_GPU_PIPELINE_CREATE_MISSING_BINDLESS_SLOT);
    MEL_EXPECT(!mel_gpu_pipeline_alive(dev, pipe.value));

    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(stress_bindless, sampler_dedup_refcount)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32       CLAIMS = 200;
    Mel_Gpu_Sampler claims[CLAIMS];
    for (u32 i = 0; i < CLAIMS; i++)
    {
        Mel_Gpu_Sampler_Create_Result s = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR,
                                                                 .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .name = "dup");
        MEL_REQUIRE(!mel_gpu_failed(s.status));
        claims[i] = s.value;
        MEL_EXPECT(mel_gpu_handle_eq(claims[i].slot, claims[0].slot));
    }
    for (u32 i = 1; i < CLAIMS; i++)
        mel_gpu_sampler_destroy(dev, claims[i]);
    MEL_EXPECT(mel_gpu_sampler_alive(dev, claims[0]));
    mel_gpu_sampler_destroy(dev, claims[0]);
    MEL_EXPECT(!mel_gpu_sampler_alive(dev, claims[0]));

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

typedef struct
{
    Mel_Gpu_Device* dev;
    Mel_Barrier*    made_all;
    Mel_Barrier*    start;
    u32             per_thread;
    u32             thread_ix;
    Mel_Gpu_Sampler canonical;
    _Atomic(u32)    failures;
    _Atomic(u32)    handle_breaks;
    _Atomic(u32)    dead_while_claimed;
} Stress_Dedup_Ctx;

static int stress_dedup_worker(void* user)
{
    Stress_Dedup_Ctx* c = user;
    Mel_Gpu_Sampler*  mine = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Sampler, c->per_thread);
    u32               made = 0;
    mel_barrier_wait(c->start);

    for (u32 i = 0; i < c->per_thread; i++)
    {
        Mel_Gpu_Sampler_Create_Result s = mel_gpu_sampler_create(c->dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR,
                                                                 .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "tdup");
        if (mel_gpu_failed(s.status))
        {
            atomic_fetch_add(&c->failures, 1);
            continue;
        }
        if (!mel_gpu_handle_eq(s.value.slot, c->canonical.slot))
            atomic_fetch_add(&c->handle_breaks, 1);
        mine[made++] = s.value;
    }

    mel_barrier_wait(c->made_all);

    if (!mel_gpu_sampler_alive(c->dev, c->canonical))
        atomic_fetch_add(&c->dead_while_claimed, 1);

    for (u32 i = 0; i < made; i++)
        mel_gpu_sampler_destroy(c->dev, mine[i]);
    mel_dealloc(mel_alloc_heap(), mine);
    return 0;
}

MEL_TEST(stress_bindless, threaded_sampler_dedup_refcount)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Sampler_Create_Result anchor = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_LINEAR, .mag_filter = MEL_GPU_FILTER_LINEAR,
                                                                  .wrap_u = MEL_GPU_WRAP_CLAMP_EDGE, .wrap_v = MEL_GPU_WRAP_CLAMP_EDGE, .name = "tdup");
    MEL_REQUIRE(!mel_gpu_failed(anchor.status));

    const u32 T = stress_threads();
    const u32 PER = 256;

    Mel_Barrier start, made_all;
    MEL_REQUIRE(mel_barrier_init(&start, T));
    MEL_REQUIRE(mel_barrier_init(&made_all, T));
    Mel_Thread*       threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Stress_Dedup_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Stress_Dedup_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Stress_Dedup_Ctx){ .dev = dev, .made_all = &made_all, .start = &start, .per_thread = PER, .thread_ix = i, .canonical = anchor.value };
        atomic_store(&ctx[i].failures, 0);
        atomic_store(&ctx[i].handle_breaks, 0);
        atomic_store(&ctx[i].dead_while_claimed, 0);
        MEL_REQUIRE(mel_thread_spawn(&threads[i], stress_dedup_worker, &ctx[i], .name = "dedup"));
    }
    for (u32 i = 0; i < T; i++)
        mel_thread_join(&threads[i], NULL);

    u32 fails = 0, breaks = 0, dead = 0;
    for (u32 i = 0; i < T; i++)
    {
        fails += atomic_load(&ctx[i].failures);
        breaks += atomic_load(&ctx[i].handle_breaks);
        dead += atomic_load(&ctx[i].dead_while_claimed);
    }
    MEL_EXPECT_EQ(fails, 0u);
    MEL_EXPECT_EQ(breaks, 0u);
    MEL_EXPECT_EQ(dead, 0u);

    MEL_EXPECT(mel_gpu_sampler_alive(dev, anchor.value));
    mel_gpu_sampler_destroy(dev, anchor.value);
    MEL_EXPECT(!mel_gpu_sampler_alive(dev, anchor.value));

    mel_dealloc(mel_alloc_heap(), ctx);
    mel_dealloc(mel_alloc_heap(), threads);
    mel_barrier_destroy(&start);
    mel_barrier_destroy(&made_all);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(stress_alloc, no_overlap_sentinels)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 N = 256;
    struct
    {
        Mel_Gpu_Buffer buf;
        usize          size;
        u8             tag;
        bool           live;
    } slots[N];
    memset(slots, 0, sizeof slots);

    u32 rng = 0x1234567u;
    for (u32 step = 0; step < 1500; step++)
    {
        rng = rng * 1664525u + 1013904223u;
        u32 idx = rng % N;
        if (slots[idx].live)
        {
            const u8* p = mel_gpu_buffer_mapped(dev, slots[idx].buf);
            MEL_REQUIRE_NOT_NULL(p);
            bool ok = true;
            for (usize j = 0; j < slots[idx].size; j++)
                if (p[j] != slots[idx].tag)
                    ok = false;
            MEL_EXPECT(ok);
            mel_gpu_buffer_destroy(dev, slots[idx].buf);
            slots[idx].live = false;
        }
        else
        {
            usize sz = 17 + ((rng >> 8) % 8191);
            Mel_Gpu_Buffer_Create_Result r = mel_gpu_buffer_create(dev, .size = sz, .usage = MEL_GPU_BUFFER_TRANSFER_SRC, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "sent");
            MEL_REQUIRE(!mel_gpu_failed(r.status));
            u8*   p = mel_gpu_buffer_mapped(dev, r.value);
            MEL_REQUIRE_NOT_NULL(p);
            u8    tag = (u8)(rng >> 16);
            memset(p, tag, sz);
            slots[idx].buf = r.value;
            slots[idx].size = sz;
            slots[idx].tag = tag;
            slots[idx].live = true;
        }
    }
    for (u32 i = 0; i < N; i++)
        if (slots[i].live)
        {
            const u8* p = mel_gpu_buffer_mapped(dev, slots[i].buf);
            MEL_REQUIRE_NOT_NULL(p);
            bool ok = true;
            for (usize j = 0; j < slots[i].size; j++)
                if (p[j] != slots[i].tag)
                    ok = false;
            MEL_EXPECT(ok);
            mel_gpu_buffer_destroy(dev, slots[i].buf);
        }

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(stress_alloc, dedicated_interleaved)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32      SMALL = 64;
    Mel_Gpu_Buffer small[SMALL];
    for (u32 i = 0; i < SMALL; i++)
    {
        Mel_Gpu_Buffer_Create_Result r = mel_gpu_buffer_create(dev, .size = 1000 + i * 37, .usage = MEL_GPU_BUFFER_TRANSFER_SRC, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "s");
        MEL_REQUIRE(!mel_gpu_failed(r.status));
        u8* p = mel_gpu_buffer_mapped(dev, r.value);
        MEL_REQUIRE_NOT_NULL(p);
        memset(p, (u8)(i + 1), 1000 + i * 37);
        small[i] = r.value;
    }

    Mel_Gpu_Buffer_Create_Result big = mel_gpu_buffer_create(dev, .size = 40ull * 1024 * 1024, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .name = "big");
    MEL_REQUIRE(!mel_gpu_failed(big.status));

    for (u32 i = 0; i < SMALL; i++)
    {
        const u8* p = mel_gpu_buffer_mapped(dev, small[i]);
        MEL_REQUIRE_NOT_NULL(p);
        bool ok = true;
        for (usize j = 0; j < 1000 + i * 37; j++)
            if (p[j] != (u8)(i + 1))
                ok = false;
        MEL_EXPECT(ok);
    }

    mel_gpu_buffer_destroy(dev, big.value);
    for (u32 i = 0; i < SMALL; i++)
        mel_gpu_buffer_destroy(dev, small[i]);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(stress_io, buffer_write_readback_fuzz)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const usize sizes[] = { 1, 4, 17, 256, 257, 4096, 65535 };
    for (u32 s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
    {
        usize sz = sizes[s];
        u8*   src = mel_alloc_array(mel_alloc_heap(), u8, sz);
        for (usize i = 0; i < sz; i++)
            src[i] = (u8)(i * 31 + s * 7 + 1);

        Mel_Gpu_Buffer_Create_Result dv = mel_gpu_buffer_create(dev, .size = sz, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_TRANSFER_SRC | MEL_GPU_BUFFER_TRANSFER_DST,
                                                                .memory = MEL_GPU_MEMORY_DEVICE, .name = "wfuzz-dev");
        MEL_REQUIRE(!mel_gpu_failed(dv.status));
        mel_gpu_buffer_write(dev, dv.value, src, sz);

        Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = sz, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "wfuzz-rb");
        MEL_REQUIRE(!mel_gpu_failed(rb.status));

        Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
        mel_gpu_command_list_begin(cmd);
        mel_gpu_cmd_buffer_barrier(cmd, dv.value, MEL_GPU_STATE_COPY_DEST, MEL_GPU_STATE_COPY_SOURCE);
        mel_gpu_cmd_copy_buffer(cmd, dv.value, rb.value, sz);
        mel_gpu_command_list_end(cmd);
        Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
        MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
        mel_gpu_future_destroy(f);

        const u8* got = mel_gpu_buffer_mapped(dev, rb.value);
        MEL_REQUIRE_NOT_NULL(got);
        bool match = true;
        for (usize i = 0; i < sz; i++)
            if (got[i] != src[i])
                match = false;
        MEL_EXPECT(match);

        mel_gpu_command_list_destroy(cmd);
        mel_gpu_buffer_destroy(dev, rb.value);
        mel_gpu_buffer_destroy(dev, dv.value);
        mel_dealloc(mel_alloc_heap(), src);
    }

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(stress_io, texture_write_readback_fuzz)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const u32 dims[][2] = { { 1, 1 }, { 2, 3 }, { 4, 4 }, { 7, 5 }, { 16, 16 }, { 31, 17 } };
    for (u32 d = 0; d < sizeof dims / sizeof dims[0]; d++)
    {
        u32   W = dims[d][0], H = dims[d][1];
        usize bytes = (usize)W * H * 4;
        u8*   src = mel_alloc_array(mel_alloc_heap(), u8, bytes);
        for (usize i = 0; i < bytes; i++)
            src[i] = (u8)(i * 5 + d * 11 + 3);

        Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { W, H, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                 .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_SRC | MEL_GPU_TEXTURE_COPY_DST, .name = "tfuzz");
        MEL_REQUIRE(!mel_gpu_failed(t.status));
        Mel_Gpu_Texture_Region region = { .subresource = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 }, .offset = { 0, 0, 0 }, .extent = { W, H, 1 } };
        mel_gpu_texture_write(dev, t.value, region, src, bytes);

        Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(dev, .size = bytes, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "tfuzz-rb");
        MEL_REQUIRE(!mel_gpu_failed(rb.status));

        Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
        mel_gpu_command_list_begin(cmd);
        Mel_Gpu_Subresource_Range range = { MEL_GPU_ASPECT_COLOR, 0, 1, 0, 1 };
        mel_gpu_cmd_texture_barrier(cmd, t.value, range, MEL_GPU_STATE_SHADER_RESOURCE, MEL_GPU_STATE_COPY_SOURCE);
        mel_gpu_cmd_copy_texture_to_buffer(cmd, t.value, range, rb.value);
        mel_gpu_command_list_end(cmd);
        Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
        MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
        mel_gpu_future_destroy(f);

        const u8* got = mel_gpu_buffer_mapped(dev, rb.value);
        MEL_REQUIRE_NOT_NULL(got);
        bool match = true;
        for (usize i = 0; i < bytes; i++)
            if (got[i] != src[i])
                match = false;
        MEL_EXPECT(match);

        mel_gpu_command_list_destroy(cmd);
        mel_gpu_buffer_destroy(dev, rb.value);
        mel_gpu_texture_destroy(dev, t.value);
        mel_dealloc(mel_alloc_heap(), src);
    }

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(stress_command, list_churn_destroy_after_submit)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const u32 ITERS = 300;
    for (u32 i = 0; i < ITERS; i++)
    {
        Mel_Gpu_Buffer_Create_Result b =
            mel_gpu_buffer_create(dev, .size = 256, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_DEVICE, .name = "ccl");
        MEL_REQUIRE(!mel_gpu_failed(b.status));

        Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
        MEL_REQUIRE_NOT_NULL(cmd);
        mel_gpu_command_list_begin(cmd);
        mel_gpu_cmd_buffer_barrier(cmd, b.value, MEL_GPU_STATE_COPY_DEST, MEL_GPU_STATE_UNORDERED_ACCESS);
        mel_gpu_command_list_end(cmd);
        Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
        MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
        mel_gpu_buffer_destroy(dev, b.value);
        mel_gpu_future_destroy(f);
        mel_gpu_command_list_destroy(cmd);
    }

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(stress_compute, storage_buffer_bindless_rounds)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    Mel_Gpu_Shader_Create_Result sh = mel_gpu_shader_create_compute_from_bytecode(dev, .spirv = ADD_COMP_SPV, .spirv_size = sizeof ADD_COMP_SPV, .entry = "main", .name = "add");
    MEL_REQUIRE(!mel_gpu_failed(sh.status));
    Mel_Gpu_Pipeline_Create_Result pipe = mel_gpu_pipeline_compute_create(dev, .shader = sh.value, .name = "add");
    MEL_REQUIRE(!mel_gpu_failed(pipe.status));

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const u32 N = 64;
    const u32 ROUNDS = 24;
    for (u32 r = 0; r < ROUNDS; r++)
    {
        u32 in_data[N];
        for (u32 i = 0; i < N; i++)
            in_data[i] = r * 1000u + i;

        Mel_Gpu_Buffer_Create_Result in = mel_gpu_buffer_create(dev, .size = sizeof in_data, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_TRANSFER_DST,
                                                                .memory = MEL_GPU_MEMORY_UPLOAD, .data = in_data, .name = "in");
        MEL_REQUIRE(!mel_gpu_failed(in.status));
        Mel_Gpu_Buffer_Create_Result out = mel_gpu_buffer_create(dev, .size = sizeof in_data, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_TRANSFER_SRC,
                                                                 .memory = MEL_GPU_MEMORY_READBACK, .name = "out");
        MEL_REQUIRE(!mel_gpu_failed(out.status));

        struct
        {
            u32 in_buf, out_buf, n;
        } root = { mel_gpu_buffer_bindless_slot(dev, in.value), mel_gpu_buffer_bindless_slot(dev, out.value), N };

        Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
        mel_gpu_command_list_begin(cmd);
        mel_gpu_cmd_bind_pipeline(cmd, pipe.value);
        mel_gpu_cmd_push_constants(cmd, 0, sizeof root, &root);
        mel_gpu_cmd_dispatch(cmd, (N + 63) / 64, 1, 1);
        mel_gpu_command_list_end(cmd);
        Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
        MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
        mel_gpu_future_destroy(f);

        const u32* got = mel_gpu_buffer_mapped(dev, out.value);
        MEL_REQUIRE_NOT_NULL(got);
        bool ok = true;
        for (u32 i = 0; i < N; i++)
            if (got[i] != in_data[i] + 1u)
                ok = false;
        MEL_EXPECT(ok);

        mel_gpu_command_list_destroy(cmd);
        mel_gpu_buffer_destroy(dev, out.value);
        mel_gpu_buffer_destroy(dev, in.value);
        stress_drain(dev, q);
    }

    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

MEL_TEST(stress_future, many_inflight_submits)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const u32 BATCH = 256;
    for (u32 i = 0; i < BATCH; i++)
    {
        Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
        mel_gpu_command_list_begin(cmd);
        mel_gpu_command_list_end(cmd);
        Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
        MEL_REQUIRE_NOT_NULL(f);
        MEL_EXPECT(mel_gpu_future_resolved(f));
        MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
        mel_gpu_future_destroy(f);
        mel_gpu_command_list_destroy(cmd);
    }

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

static void stress_future_then_noop(Mel_Gpu_Future* f, void* user)
{
    (void)f;
    *(u32*)user += 1;
}

MEL_TEST(stress_future, many_futures_resolve_once_each)
{
    Mel_Gpu_Completion_Pump* pump = mel_gpu_pump_create_opt(NULL, (Mel_Gpu_Pump_Opt){ 0 });
    MEL_REQUIRE_NOT_NULL(pump);

    const u32       N = 32;
    Mel_Gpu_Future* futs[N];
    u32             delivered = 0;
    for (u32 i = 0; i < N; i++)
    {
        futs[i] = mel_gpu_future_create(pump, NULL);
        mel_gpu_future_then(futs[i], stress_future_then_noop, &delivered);
    }
    for (u32 i = 0; i < N; i++)
    {
        mel_gpu_future_resolve(futs[i], NULL, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK));
        mel_gpu_future_resolve(futs[i], NULL, MEL_GPU_STATUS(9, MEL_GPU_SEVERITY_ERROR));
    }
    mel_gpu_pump_tick(pump);
    MEL_EXPECT_EQ(delivered, N);
    for (u32 i = 0; i < N; i++)
        MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(futs[i])));

    for (u32 i = 0; i < N; i++)
        mel_gpu_future_destroy(futs[i]);
    mel_gpu_pump_destroy(pump);
}

static u32 stress_threads(void)
{
    u32 hw = mel_thread_hardware_concurrency();
    if (hw < 2)
        hw = 2;
    if (hw > 8)
        hw = 8;
    return hw;
}

typedef struct
{
    const u8* p;
    usize     size;
    u32       owner;
} Stress_Live_Range;

typedef struct
{
    Mel_Mutex          lock;
    Stress_Live_Range* ranges;
    u32                count;
    u32                cap;
    _Atomic(u32)       overlaps;
} Stress_Overlap_Ledger;

static void stress_ledger_add(Stress_Overlap_Ledger* L, const u8* p, usize size, u32 owner)
{
    mel_mutex_lock(&L->lock);
    for (u32 i = 0; i < L->count; i++)
    {
        const u8* a0 = L->ranges[i].p;
        const u8* a1 = a0 + L->ranges[i].size;
        const u8* b0 = p;
        const u8* b1 = p + size;
        if (a0 < b1 && b0 < a1)
            atomic_fetch_add(&L->overlaps, 1);
    }
    if (L->count == L->cap)
    {
        L->cap = L->cap ? L->cap * 2 : 256;
        L->ranges = L->ranges ? mel_realloc(mel_alloc_heap(), L->ranges, sizeof(Stress_Live_Range) * L->cap) : mel_alloc(mel_alloc_heap(), sizeof(Stress_Live_Range) * L->cap);
    }
    L->ranges[L->count++] = (Stress_Live_Range){ p, size, owner };
    mel_mutex_unlock(&L->lock);
}

static void stress_ledger_remove(Stress_Overlap_Ledger* L, const u8* p)
{
    mel_mutex_lock(&L->lock);
    for (u32 i = 0; i < L->count; i++)
        if (L->ranges[i].p == p)
        {
            L->ranges[i] = L->ranges[--L->count];
            break;
        }
    mel_mutex_unlock(&L->lock);
}

typedef struct
{
    Mel_Gpu_Device*        dev;
    Mel_Barrier*           start;
    Stress_Overlap_Ledger* ledger;
    u32                    steps;
    u32                    slots;
    u32                    thread_ix;
    _Atomic(u32)           failures;
    _Atomic(u32)           corruptions;
} Stress_Alloc_Ctx;

static int stress_alloc_storm_worker(void* user)
{
    Stress_Alloc_Ctx* c = user;
    struct Slot
    {
        Mel_Gpu_Buffer buf;
        const u8*      mapped;
        usize          size;
        u8             tag;
        bool           live;
    };
    struct Slot* slots = mel_alloc_array(mel_alloc_heap(), struct Slot, c->slots);
    memset(slots, 0, sizeof(struct Slot) * c->slots);

    u32 rng = 0x9E3779B9u ^ (c->thread_ix * 2654435761u);
    mel_barrier_wait(c->start);

    for (u32 step = 0; step < c->steps; step++)
    {
        rng = rng * 1664525u + 1013904223u;
        u32 idx = rng % c->slots;
        if (slots[idx].live)
        {
            const u8* p = mel_gpu_buffer_mapped(c->dev, slots[idx].buf);
            if (!p)
                atomic_fetch_add(&c->failures, 1);
            else
            {
                for (usize j = 0; j < slots[idx].size; j++)
                    if (p[j] != slots[idx].tag)
                    {
                        atomic_fetch_add(&c->corruptions, 1);
                        break;
                    }
            }
            stress_ledger_remove(c->ledger, slots[idx].mapped);
            mel_gpu_buffer_destroy(c->dev, slots[idx].buf);
            slots[idx].live = false;
        }
        else
        {
            usize sz = 17 + ((rng >> 7) % 8191);
            Mel_Gpu_Buffer_Create_Result r = mel_gpu_buffer_create(c->dev, .size = sz, .usage = MEL_GPU_BUFFER_TRANSFER_SRC, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "tstorm");
            if (mel_gpu_failed(r.status))
            {
                atomic_fetch_add(&c->failures, 1);
                continue;
            }
            u8* p = mel_gpu_buffer_mapped(c->dev, r.value);
            if (!p)
            {
                atomic_fetch_add(&c->failures, 1);
                mel_gpu_buffer_destroy(c->dev, r.value);
                continue;
            }
            stress_ledger_add(c->ledger, p, sz, c->thread_ix);
            u8 tag = (u8)((c->thread_ix << 4) ^ (rng >> 16) ^ 0xA5);
            memset(p, tag, sz);
            slots[idx] = (struct Slot){ .buf = r.value, .mapped = p, .size = sz, .tag = tag, .live = true };
        }
    }
    for (u32 i = 0; i < c->slots; i++)
        if (slots[i].live)
        {
            const u8* p = mel_gpu_buffer_mapped(c->dev, slots[i].buf);
            if (!p)
                atomic_fetch_add(&c->failures, 1);
            else
                for (usize j = 0; j < slots[i].size; j++)
                    if (p[j] != slots[i].tag)
                    {
                        atomic_fetch_add(&c->corruptions, 1);
                        break;
                    }
            stress_ledger_remove(c->ledger, slots[i].mapped);
            mel_gpu_buffer_destroy(c->dev, slots[i].buf);
        }
    mel_dealloc(mel_alloc_heap(), slots);
    return 0;
}

MEL_TEST(stress_alloc, threaded_overlap_storm)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 T = stress_threads();
    const u32 STEPS = 8000;
    const u32 SLOTS = 96;

    Stress_Overlap_Ledger ledger = { 0 };
    MEL_REQUIRE(mel_mutex_init(&ledger.lock, MEL_MUTEX_PLAIN));
    atomic_store(&ledger.overlaps, 0);

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, T));
    Mel_Thread*       threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Stress_Alloc_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Stress_Alloc_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Stress_Alloc_Ctx){ .dev = dev, .start = &start, .ledger = &ledger, .steps = STEPS, .slots = SLOTS, .thread_ix = i };
        atomic_store(&ctx[i].failures, 0);
        atomic_store(&ctx[i].corruptions, 0);
        MEL_REQUIRE(mel_thread_spawn(&threads[i], stress_alloc_storm_worker, &ctx[i], .name = "alloc-storm"));
    }
    for (u32 i = 0; i < T; i++)
        mel_thread_join(&threads[i], NULL);

    u32 fails = 0, corrupt = 0;
    for (u32 i = 0; i < T; i++)
    {
        fails += atomic_load(&ctx[i].failures);
        corrupt += atomic_load(&ctx[i].corruptions);
    }
    u32 overlaps = atomic_load(&ledger.overlaps);
    if (overlaps || corrupt)
        mel_log_error("gpu-stress",
                      "BUG-1 reproduced: %u overlapping live mapped-ranges, %u sentinel corruptions under concurrent "
                      "buffer_create/destroy. Root cause: post-unlock interior-pointer race on the packed slotmap "
                      "(table_get returns data+packed_idx*item_size; concurrent insert/remove relocates it). See writeup.",
                      overlaps, corrupt);

    MEL_EXPECT_EQ(fails, 0u);
    MEL_EXPECT_EQ(overlaps, 0u);
    MEL_EXPECT_EQ(corrupt, 0u);

    if (ledger.ranges)
        mel_dealloc(mel_alloc_heap(), ledger.ranges);
    mel_mutex_destroy(&ledger.lock);
    mel_dealloc(mel_alloc_heap(), ctx);
    mel_dealloc(mel_alloc_heap(), threads);
    mel_barrier_destroy(&start);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

typedef struct
{
    Mel_Gpu_Device* dev;
    Mel_Gpu_Queue*  queue;
    Mel_Barrier*    start;
    u32             rounds;
    u32             thread_ix;
    _Atomic(u32)    failures;
    _Atomic(u32)    mismatches;
} Stress_Defer_Ctx;

static int stress_defer_worker(void* user)
{
    Stress_Defer_Ctx* c = user;
    mel_barrier_wait(c->start);
    for (u32 r = 0; r < c->rounds; r++)
    {
        u32 pattern = (c->thread_ix << 24) | (r & 0xFFFFFF);
        u32 src_data[16];
        for (u32 i = 0; i < 16; i++)
            src_data[i] = pattern + i;

        Mel_Gpu_Buffer_Create_Result src = mel_gpu_buffer_create(c->dev, .size = sizeof src_data, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_TRANSFER_SRC | MEL_GPU_BUFFER_TRANSFER_DST,
                                                                 .memory = MEL_GPU_MEMORY_DEVICE, .data = src_data, .name = "defsrc");
        Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(c->dev, .size = sizeof src_data, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "defrb");
        if (mel_gpu_failed(src.status) || mel_gpu_failed(rb.status))
        {
            atomic_fetch_add(&c->failures, 1);
            if (!mel_gpu_failed(src.status))
                mel_gpu_buffer_destroy(c->dev, src.value);
            if (!mel_gpu_failed(rb.status))
                mel_gpu_buffer_destroy(c->dev, rb.value);
            continue;
        }

        Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(c->queue);
        mel_gpu_command_list_begin(cmd);
        mel_gpu_cmd_buffer_barrier(cmd, src.value, MEL_GPU_STATE_COPY_DEST, MEL_GPU_STATE_COPY_SOURCE);
        mel_gpu_cmd_copy_buffer(cmd, src.value, rb.value, sizeof src_data);
        mel_gpu_command_list_end(cmd);
        Mel_Gpu_Future* f = mel_gpu_queue_submit(c->queue, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
        bool ok = f && mel_gpu_ok(mel_gpu_future_status(f));

        mel_gpu_buffer_destroy(c->dev, src.value);

        if (ok)
        {
            const u32* got = mel_gpu_buffer_mapped(c->dev, rb.value);
            if (!got)
                atomic_fetch_add(&c->mismatches, 1);
            else
                for (u32 i = 0; i < 16; i++)
                    if (got[i] != src_data[i])
                    {
                        atomic_fetch_add(&c->mismatches, 1);
                        break;
                    }
        }
        else
            atomic_fetch_add(&c->failures, 1);

        mel_gpu_future_destroy(f);
        mel_gpu_command_list_destroy(cmd);
        mel_gpu_buffer_destroy(c->dev, rb.value);
    }
    return 0;
}

MEL_TEST(stress_command, threaded_deferred_free_under_submit)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const u32 T = stress_threads();
    const u32 ROUNDS = 24;

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, T));
    Mel_Thread*       threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Stress_Defer_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Stress_Defer_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Stress_Defer_Ctx){ .dev = dev, .queue = q, .start = &start, .rounds = ROUNDS, .thread_ix = i };
        atomic_store(&ctx[i].failures, 0);
        atomic_store(&ctx[i].mismatches, 0);
        MEL_REQUIRE(mel_thread_spawn(&threads[i], stress_defer_worker, &ctx[i], .name = "defer"));
    }
    for (u32 i = 0; i < T; i++)
        mel_thread_join(&threads[i], NULL);

    u32 fails = 0, mism = 0;
    for (u32 i = 0; i < T; i++)
    {
        fails += atomic_load(&ctx[i].failures);
        mism += atomic_load(&ctx[i].mismatches);
    }
    MEL_EXPECT_EQ(fails, 0u);
    MEL_EXPECT_EQ(mism, 0u);

    mel_dealloc(mel_alloc_heap(), ctx);
    mel_dealloc(mel_alloc_heap(), threads);
    mel_barrier_destroy(&start);
    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

typedef struct
{
    Mel_Gpu_Device*        dev;
    Mel_Barrier*           start;
    Stress_Overlap_Ledger* ledger;
    u32                    steps;
    u32                    slots;
    u32                    thread_ix;
    _Atomic(u32)           failures;
    _Atomic(u32)           corruptions;
    _Atomic(u32)           type_breaks;
} Stress_Mixed_Ctx;

static int stress_mixed_storm_worker(void* user)
{
    Stress_Mixed_Ctx* c = user;
    struct Slot
    {
        Mel_Gpu_Buffer  buf;
        Mel_Gpu_Texture tex;
        Mel_Gpu_Sampler samp;
        const u8*       mapped;
        usize           size;
        u8              tag;
        bool            live;
    };
    struct Slot* slots = mel_alloc_array(mel_alloc_heap(), struct Slot, c->slots);
    memset(slots, 0, sizeof(struct Slot) * c->slots);

    u32 rng = 0x85EBCA6Bu ^ (c->thread_ix * 0x27D4EB2Fu);
    mel_barrier_wait(c->start);

    for (u32 step = 0; step < c->steps; step++)
    {
        rng = rng * 1664525u + 1013904223u;
        u32 idx = rng % c->slots;
        if (slots[idx].live)
        {
            if (!mel_gpu_buffer_alive(c->dev, slots[idx].buf) || !mel_gpu_texture_alive(c->dev, slots[idx].tex) || !mel_gpu_sampler_alive(c->dev, slots[idx].samp))
                atomic_fetch_add(&c->type_breaks, 1);
            const u8* p = mel_gpu_buffer_mapped(c->dev, slots[idx].buf);
            if (!p)
                atomic_fetch_add(&c->failures, 1);
            else
                for (usize j = 0; j < slots[idx].size; j++)
                    if (p[j] != slots[idx].tag)
                    {
                        atomic_fetch_add(&c->corruptions, 1);
                        break;
                    }
            stress_ledger_remove(c->ledger, slots[idx].mapped);
            mel_gpu_sampler_destroy(c->dev, slots[idx].samp);
            mel_gpu_texture_destroy(c->dev, slots[idx].tex);
            mel_gpu_buffer_destroy(c->dev, slots[idx].buf);
            slots[idx].live = false;
        }
        else
        {
            usize sz = 17 + ((rng >> 7) % 8191);
            Mel_Gpu_Buffer_Create_Result b = mel_gpu_buffer_create(c->dev, .size = sz, .usage = MEL_GPU_BUFFER_TRANSFER_SRC, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "mstorm-b");
            if (mel_gpu_failed(b.status))
            {
                atomic_fetch_add(&c->failures, 1);
                continue;
            }
            u8* p = mel_gpu_buffer_mapped(c->dev, b.value);
            if (!p)
            {
                atomic_fetch_add(&c->failures, 1);
                mel_gpu_buffer_destroy(c->dev, b.value);
                continue;
            }
            Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(c->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 4 + (rng % 24), 4, 1 },
                                                                     .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "mstorm-t");
            if (mel_gpu_failed(t.status))
            {
                atomic_fetch_add(&c->failures, 1);
                mel_gpu_buffer_destroy(c->dev, b.value);
                continue;
            }
            Mel_Gpu_Sampler_Create_Result s = mel_gpu_sampler_create(c->dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                                     .wrap_u = (Mel_Gpu_Wrap)(rng % 4), .lod_max = (f32)((rng >> 3) % 64 + 1), .name = "mstorm-s");
            if (mel_gpu_failed(s.status))
            {
                atomic_fetch_add(&c->failures, 1);
                mel_gpu_texture_destroy(c->dev, t.value);
                mel_gpu_buffer_destroy(c->dev, b.value);
                continue;
            }
            stress_ledger_add(c->ledger, p, sz, c->thread_ix);
            u8 tag = (u8)((c->thread_ix << 4) ^ (rng >> 17) ^ 0x5A);
            memset(p, tag, sz);
            slots[idx] = (struct Slot){ .buf = b.value, .tex = t.value, .samp = s.value, .mapped = p, .size = sz, .tag = tag, .live = true };
        }
    }
    for (u32 i = 0; i < c->slots; i++)
        if (slots[i].live)
        {
            const u8* p = mel_gpu_buffer_mapped(c->dev, slots[i].buf);
            if (!p)
                atomic_fetch_add(&c->failures, 1);
            else
                for (usize j = 0; j < slots[i].size; j++)
                    if (p[j] != slots[i].tag)
                    {
                        atomic_fetch_add(&c->corruptions, 1);
                        break;
                    }
            stress_ledger_remove(c->ledger, slots[i].mapped);
            mel_gpu_sampler_destroy(c->dev, slots[i].samp);
            mel_gpu_texture_destroy(c->dev, slots[i].tex);
            mel_gpu_buffer_destroy(c->dev, slots[i].buf);
        }
    mel_dealloc(mel_alloc_heap(), slots);
    return 0;
}

MEL_TEST(stress_alloc, mixed_type_churn_storm)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 T = stress_threads();
    const u32 STEPS = 16000;
    const u32 SLOTS = 64;

    Stress_Overlap_Ledger ledger = { 0 };
    MEL_REQUIRE(mel_mutex_init(&ledger.lock, MEL_MUTEX_PLAIN));
    atomic_store(&ledger.overlaps, 0);

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, T));
    Mel_Thread*       threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Stress_Mixed_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Stress_Mixed_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Stress_Mixed_Ctx){ .dev = dev, .start = &start, .ledger = &ledger, .steps = STEPS, .slots = SLOTS, .thread_ix = i };
        atomic_store(&ctx[i].failures, 0);
        atomic_store(&ctx[i].corruptions, 0);
        atomic_store(&ctx[i].type_breaks, 0);
        MEL_REQUIRE(mel_thread_spawn(&threads[i], stress_mixed_storm_worker, &ctx[i], .name = "mixed-storm"));
    }
    for (u32 i = 0; i < T; i++)
        mel_thread_join(&threads[i], NULL);

    u32 fails = 0, corrupt = 0, type_breaks = 0;
    for (u32 i = 0; i < T; i++)
    {
        fails += atomic_load(&ctx[i].failures);
        corrupt += atomic_load(&ctx[i].corruptions);
        type_breaks += atomic_load(&ctx[i].type_breaks);
    }
    u32 overlaps = atomic_load(&ledger.overlaps);
    if (overlaps || corrupt || type_breaks)
        mel_log_error("gpu-stress",
                      "copy-under-lock regression: %u overlaps, %u corruptions, %u liveness breaks across buffer/texture/sampler churn. "
                      "A reopened BUG-1 (interior-pointer escape on any converted table_get_copy site) would surface here.",
                      overlaps, corrupt, type_breaks);

    MEL_EXPECT_EQ(fails, 0u);
    MEL_EXPECT_EQ(overlaps, 0u);
    MEL_EXPECT_EQ(corrupt, 0u);
    MEL_EXPECT_EQ(type_breaks, 0u);

    if (ledger.ranges)
        mel_dealloc(mel_alloc_heap(), ledger.ranges);
    mel_mutex_destroy(&ledger.lock);
    mel_dealloc(mel_alloc_heap(), ctx);
    mel_dealloc(mel_alloc_heap(), threads);
    mel_barrier_destroy(&start);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

typedef struct
{
    Mel_Gpu_Device*        dev;
    Mel_Gpu_Queue*         queue;
    Mel_Barrier*           start;
    Stress_Overlap_Ledger* ledger;
    u32                    rounds;
    u32                    thread_ix;
    bool                   submitter;
    _Atomic(u32)           failures;
    _Atomic(u32)           corruptions;
} Stress_Mixed_Submit_Ctx;

static int stress_submit_vs_destroy_worker(void* user)
{
    Stress_Mixed_Submit_Ctx* c = user;
    u32                      rng = 0xC2B2AE35u ^ (c->thread_ix * 0x9E3779B1u);
    mel_barrier_wait(c->start);
    for (u32 r = 0; r < c->rounds; r++)
    {
        rng = rng * 1664525u + 1013904223u;
        if (c->submitter)
        {
            u32                          pattern = (c->thread_ix << 24) | (r & 0xFFFFFF);
            u32                          src_data[16];
            for (u32 i = 0; i < 16; i++)
                src_data[i] = pattern + i;
            Mel_Gpu_Buffer_Create_Result src = mel_gpu_buffer_create(c->dev, .size = sizeof src_data, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_TRANSFER_SRC | MEL_GPU_BUFFER_TRANSFER_DST,
                                                                     .memory = MEL_GPU_MEMORY_DEVICE, .data = src_data, .name = "svs-src");
            Mel_Gpu_Buffer_Create_Result rb = mel_gpu_buffer_create(c->dev, .size = sizeof src_data, .usage = MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_READBACK, .name = "svs-rb");
            if (mel_gpu_failed(src.status) || mel_gpu_failed(rb.status))
            {
                atomic_fetch_add(&c->failures, 1);
                if (!mel_gpu_failed(src.status))
                    mel_gpu_buffer_destroy(c->dev, src.value);
                if (!mel_gpu_failed(rb.status))
                    mel_gpu_buffer_destroy(c->dev, rb.value);
                continue;
            }
            Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(c->queue);
            mel_gpu_command_list_begin(cmd);
            mel_gpu_cmd_buffer_barrier(cmd, src.value, MEL_GPU_STATE_COPY_DEST, MEL_GPU_STATE_COPY_SOURCE);
            mel_gpu_cmd_copy_buffer(cmd, src.value, rb.value, sizeof src_data);
            mel_gpu_command_list_end(cmd);
            Mel_Gpu_Future* f = mel_gpu_queue_submit(c->queue, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
            bool            ok = f && mel_gpu_ok(mel_gpu_future_status(f));
            mel_gpu_buffer_destroy(c->dev, src.value);
            if (ok)
            {
                const u32* got = mel_gpu_buffer_mapped(c->dev, rb.value);
                if (!got)
                    atomic_fetch_add(&c->corruptions, 1);
                else
                    for (u32 i = 0; i < 16; i++)
                        if (got[i] != src_data[i])
                        {
                            atomic_fetch_add(&c->corruptions, 1);
                            break;
                        }
            }
            else
                atomic_fetch_add(&c->failures, 1);
            mel_gpu_future_destroy(f);
            mel_gpu_command_list_destroy(cmd);
            mel_gpu_buffer_destroy(c->dev, rb.value);
        }
        else
        {
            usize sz = 17 + ((rng >> 7) % 8191);
            Mel_Gpu_Buffer_Create_Result u = mel_gpu_buffer_create(c->dev, .size = sz, .usage = MEL_GPU_BUFFER_TRANSFER_SRC, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "svs-churn");
            if (mel_gpu_failed(u.status))
            {
                atomic_fetch_add(&c->failures, 1);
                continue;
            }
            u8* p = mel_gpu_buffer_mapped(c->dev, u.value);
            if (!p)
            {
                atomic_fetch_add(&c->failures, 1);
                mel_gpu_buffer_destroy(c->dev, u.value);
                continue;
            }
            u8 tag = (u8)((c->thread_ix << 4) ^ (rng >> 19) ^ 0x3C);
            memset(p, tag, sz);
            stress_ledger_add(c->ledger, p, sz, c->thread_ix);
            for (usize j = 0; j < sz; j++)
                if (p[j] != tag)
                {
                    atomic_fetch_add(&c->corruptions, 1);
                    break;
                }
            stress_ledger_remove(c->ledger, p);
            mel_gpu_buffer_destroy(c->dev, u.value);
        }
    }
    return 0;
}

MEL_TEST(stress_command, submit_and_destroy_storm_interleaved)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const u32 T = stress_threads();
    const u32 SUBMIT_ROUNDS = 64;
    const u32 CHURN_ROUNDS = 6000;

    Stress_Overlap_Ledger ledger = { 0 };
    MEL_REQUIRE(mel_mutex_init(&ledger.lock, MEL_MUTEX_PLAIN));
    atomic_store(&ledger.overlaps, 0);

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, T));
    Mel_Thread*              threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Stress_Mixed_Submit_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Stress_Mixed_Submit_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        bool submitter = (i % 2) == 0;
        ctx[i] = (Stress_Mixed_Submit_Ctx){ .dev = dev, .queue = q, .start = &start, .ledger = &ledger, .rounds = submitter ? SUBMIT_ROUNDS : CHURN_ROUNDS, .thread_ix = i, .submitter = submitter };
        atomic_store(&ctx[i].failures, 0);
        atomic_store(&ctx[i].corruptions, 0);
        MEL_REQUIRE(mel_thread_spawn(&threads[i], stress_submit_vs_destroy_worker, &ctx[i], .name = "submit-vs-destroy"));
    }
    for (u32 i = 0; i < T; i++)
        mel_thread_join(&threads[i], NULL);

    u32 fails = 0, corrupt = 0;
    for (u32 i = 0; i < T; i++)
    {
        fails += atomic_load(&ctx[i].failures);
        corrupt += atomic_load(&ctx[i].corruptions);
    }
    u32 overlaps = atomic_load(&ledger.overlaps);
    if (overlaps || corrupt)
        mel_log_error("gpu-stress", "submit-vs-destroy storm: %u overlaps, %u corruptions; deferred-free record read racing a concurrent destroy storm", overlaps, corrupt);

    MEL_EXPECT_EQ(fails, 0u);
    MEL_EXPECT_EQ(overlaps, 0u);
    MEL_EXPECT_EQ(corrupt, 0u);

    if (ledger.ranges)
        mel_dealloc(mel_alloc_heap(), ledger.ranges);
    mel_mutex_destroy(&ledger.lock);
    mel_dealloc(mel_alloc_heap(), ctx);
    mel_dealloc(mel_alloc_heap(), threads);
    mel_barrier_destroy(&start);
    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

typedef struct
{
    Mel_Gpu_Device* dev;
    Mel_Barrier*    start;
    u32             count;
    u32             thread_ix;
    _Atomic(u32)    failures;
} Stress_Perf_Ctx;

static int stress_perf_worker(void* user)
{
    Stress_Perf_Ctx* c = user;
    Mel_Gpu_Buffer*  bufs = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Buffer, c->count);
    u32              made = 0;
    mel_barrier_wait(c->start);
    for (u32 i = 0; i < c->count; i++)
    {
        Mel_Gpu_Buffer_Create_Result b = mel_gpu_buffer_create(c->dev, .size = 256, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .name = "perf");
        if (mel_gpu_failed(b.status))
            atomic_fetch_add(&c->failures, 1);
        else
        {
            if (!mel_gpu_buffer_alive(c->dev, b.value))
                atomic_fetch_add(&c->failures, 1);
            bufs[made++] = b.value;
        }
    }
    for (u32 i = 0; i < made; i++)
        mel_gpu_buffer_destroy(c->dev, bufs[i]);
    mel_dealloc(mel_alloc_heap(), bufs);
    return 0;
}

static u64 stress_perf_run(Mel_Gpu_Device* dev, u32 T, u32 total)
{
    u32         per = total / T;
    Mel_Barrier start;
    mel_barrier_init(&start, T);
    Mel_Thread*      threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Stress_Perf_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Stress_Perf_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Stress_Perf_Ctx){ .dev = dev, .start = &start, .count = per, .thread_ix = i };
        atomic_store(&ctx[i].failures, 0);
        mel_thread_spawn(&threads[i], stress_perf_worker, &ctx[i], .name = "perf");
    }
    u64 t0 = mel_nanos_since_unspecified_epoch();
    for (u32 i = 0; i < T; i++)
        mel_thread_join(&threads[i], NULL);
    u64 t1 = mel_nanos_since_unspecified_epoch();
    u32 fails = 0;
    for (u32 i = 0; i < T; i++)
        fails += atomic_load(&ctx[i].failures);
    mel_dealloc(mel_alloc_heap(), ctx);
    mel_dealloc(mel_alloc_heap(), threads);
    mel_barrier_destroy(&start);
    return fails ? 0 : (t1 - t0);
}

MEL_TEST(stress_perf, copy_under_lock_no_new_cliff)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 TOTAL = 4096;
    const u32 T = stress_threads();

    u64 warm = stress_perf_run(dev, 1, 512);
    MEL_REQUIRE(warm != 0);

    u64 best_one = 0, best_many = 0;
    for (u32 rep = 0; rep < 3; rep++)
    {
        u64 one = stress_perf_run(dev, 1, TOTAL);
        u64 many = stress_perf_run(dev, T, TOTAL);
        MEL_REQUIRE(one != 0);
        MEL_REQUIRE(many != 0);
        if (best_one == 0 || one < best_one)
            best_one = one;
        if (best_many == 0 || many < best_many)
            best_many = many;
    }

    double speedup = (double)best_one / (double)best_many;
    mel_log_info("gpu-stress",
                 "copy-under-lock perf sanity: 1-thread %llu us, %u-thread %llu us, speedup %.2fx "
                 "(round-2 obj_lock baseline ~0.81x; this only guards against a NEW cliff from copy-under-lock)",
                 (unsigned long long)(best_one / 1000), T, (unsigned long long)(best_many / 1000), speedup);

    MEL_EXPECT(speedup >= 0.35);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

#else

#include <test/test.h>
MEL_TEST(stress, backend_absent) { MEL_SKIP("gpu-stress requires the Vulkan backend (--gpu=vulkan)"); }

#endif
