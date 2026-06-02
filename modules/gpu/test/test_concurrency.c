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
#include <gpu/state.h>
#include <gpu/threading.h>
#include <gpu/future.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <thread/thread.h>
#include <thread/barrier.h>
#include <thread/mutex.h>
#include <time/nano.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>
#include <stdio.h>

static Mel_Gpu_Device* conc_make_device(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-concurrency", .debug = { .enabled = true });
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

static Mel_Gpu_Device* conc_make_device_bindless(Mel_Gpu_Instance** out_inst)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-concurrency", .debug = { .enabled = true });
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

static u32 conc_threads(void)
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
    Mel_Gpu_Device* dev;
    Mel_Barrier*    start;
    u32             per_thread;

    Mel_Gpu_Buffer*  buffers;
    Mel_Gpu_Texture* textures;
    Mel_Gpu_Sampler* samplers;
    u32              made;
    _Atomic(u32)     failures;
    u32              thread_ix;
} Conc_Create_Ctx;

static int conc_create_worker(void* user)
{
    Conc_Create_Ctx* c = user;
    char             name[16];
    mel_thread_set_name("conc-create");
    mel_barrier_wait(c->start);

    for (u32 i = 0; i < c->per_thread; i++)
    {
        usize sz = 64 + (usize)(c->thread_ix * 131 + i * 17) % 8192;
        snprintf(name, sizeof name, "b%u-%u", c->thread_ix, i);
        Mel_Gpu_Buffer_Create_Result b = mel_gpu_buffer_create(c->dev, .size = sz, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_TRANSFER_DST,
                                                               .memory = (i & 1) ? MEL_GPU_MEMORY_UPLOAD : MEL_GPU_MEMORY_DEVICE, .name = name);
        if (mel_gpu_failed(b.status) || !mel_gpu_buffer_alive(c->dev, b.value))
        {
            atomic_fetch_add(&c->failures, 1);
            continue;
        }

        Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(c->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 4 + (i % 12), 4, 1 },
                                                                 .format = MEL_GPU_FORMAT_RGBA8_UNORM, .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = name);
        if (mel_gpu_failed(t.status) || !mel_gpu_texture_alive(c->dev, t.value))
        {
            atomic_fetch_add(&c->failures, 1);
            mel_gpu_buffer_destroy(c->dev, b.value);
            continue;
        }

        Mel_Gpu_Sampler_Create_Result s = mel_gpu_sampler_create(c->dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                                 .wrap_u = (Mel_Gpu_Wrap)((c->thread_ix + i) % 4), .wrap_v = (Mel_Gpu_Wrap)(i % 4),
                                                                 .lod_max = (f32)(c->thread_ix * 64 + i + 1), .name = name);
        if (mel_gpu_failed(s.status) || !mel_gpu_sampler_alive(c->dev, s.value))
        {
            atomic_fetch_add(&c->failures, 1);
            mel_gpu_texture_destroy(c->dev, t.value);
            mel_gpu_buffer_destroy(c->dev, b.value);
            continue;
        }

        c->buffers[c->made] = b.value;
        c->textures[c->made] = t.value;
        c->samplers[c->made] = s.value;
        c->made++;
    }
    return 0;
}

static bool conc_indices_collide(u32* indices, u32 n)
{
    for (u32 i = 0; i < n; i++)
        for (u32 j = i + 1; j < n; j++)
            if (indices[i] == indices[j])
                return true;
    return false;
}

MEL_TEST(conc_create, distinct_handles_no_slot_collision)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = conc_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 T = conc_threads();
    const u32 PER = 96;

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, T));

    Mel_Thread*      threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Conc_Create_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Conc_Create_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Conc_Create_Ctx){ .dev = dev, .start = &start, .per_thread = PER, .thread_ix = i };
        ctx[i].buffers = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Buffer, PER);
        ctx[i].textures = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Texture, PER);
        ctx[i].samplers = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Sampler, PER);
        atomic_store(&ctx[i].failures, 0);
        MEL_REQUIRE(mel_thread_spawn(&threads[i], conc_create_worker, &ctx[i], .name = "conc-create"));
    }
    for (u32 i = 0; i < T; i++)
        mel_thread_join(&threads[i], NULL);

    u32 total = 0, fails = 0;
    for (u32 i = 0; i < T; i++)
    {
        total += ctx[i].made;
        fails += atomic_load(&ctx[i].failures);
    }
    MEL_EXPECT_EQ(fails, 0u);
    MEL_EXPECT_EQ(total, T * PER);

    u32* bidx = mel_alloc_array(mel_alloc_heap(), u32, total);
    u32* tidx = mel_alloc_array(mel_alloc_heap(), u32, total);
    u32* sidx = mel_alloc_array(mel_alloc_heap(), u32, total);
    u32  k = 0;
    for (u32 i = 0; i < T; i++)
        for (u32 j = 0; j < ctx[i].made; j++)
        {
            bidx[k] = ctx[i].buffers[j].slot.index;
            tidx[k] = ctx[i].textures[j].slot.index;
            sidx[k] = ctx[i].samplers[j].slot.index;
            k++;
        }
    MEL_EXPECT(!conc_indices_collide(bidx, total));
    MEL_EXPECT(!conc_indices_collide(tidx, total));
    MEL_EXPECT(!conc_indices_collide(sidx, total));

    for (u32 i = 0; i < T; i++)
    {
        for (u32 j = 0; j < ctx[i].made; j++)
        {
            mel_gpu_sampler_destroy(dev, ctx[i].samplers[j]);
            mel_gpu_texture_destroy(dev, ctx[i].textures[j]);
            mel_gpu_buffer_destroy(dev, ctx[i].buffers[j]);
        }
        mel_dealloc(mel_alloc_heap(), ctx[i].buffers);
        mel_dealloc(mel_alloc_heap(), ctx[i].textures);
        mel_dealloc(mel_alloc_heap(), ctx[i].samplers);
    }
    mel_dealloc(mel_alloc_heap(), bidx);
    mel_dealloc(mel_alloc_heap(), tidx);
    mel_dealloc(mel_alloc_heap(), sidx);
    mel_dealloc(mel_alloc_heap(), ctx);
    mel_dealloc(mel_alloc_heap(), threads);
    mel_barrier_destroy(&start);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

typedef struct
{
    Mel_Gpu_Device* dev;
    Mel_Barrier*    start;
    u32             per_thread;
    u32             thread_ix;

    Mel_Gpu_Texture*      textures;
    Mel_Gpu_Texture_View* views;
    Mel_Gpu_Sampler*      samplers;
    u32                   made;
    _Atomic(u32)          failures;
    _Atomic(u32)          slot_breaks;
} Conc_Bindless_Ctx;

static int conc_bindless_worker(void* user)
{
    Conc_Bindless_Ctx* c = user;
    mel_barrier_wait(c->start);
    for (u32 i = 0; i < c->per_thread; i++)
    {
        Mel_Gpu_Texture_Create_Result t = mel_gpu_texture_create(c->dev, .kind = MEL_GPU_TEXTURE_2D, .extent = { 4, 4, 1 }, .format = MEL_GPU_FORMAT_RGBA8_UNORM,
                                                                 .usage = MEL_GPU_TEXTURE_SAMPLED | MEL_GPU_TEXTURE_COPY_DST, .name = "cbt");
        if (mel_gpu_failed(t.status))
        {
            atomic_fetch_add(&c->failures, 1);
            continue;
        }
        Mel_Gpu_Texture_View_Create_Result v = mel_gpu_texture_default_view(c->dev, t.value);
        if (mel_gpu_failed(v.status))
        {
            atomic_fetch_add(&c->failures, 1);
            mel_gpu_texture_destroy(c->dev, t.value);
            continue;
        }
        Mel_Gpu_Sampler_Create_Result s = mel_gpu_sampler_create(c->dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                                 .wrap_u = (Mel_Gpu_Wrap)((c->thread_ix + i) % 4), .lod_max = (f32)(c->thread_ix * 97 + i + 1), .name = "cbs");
        if (mel_gpu_failed(s.status))
        {
            atomic_fetch_add(&c->failures, 1);
            mel_gpu_texture_view_destroy(c->dev, v.value);
            mel_gpu_texture_destroy(c->dev, t.value);
            continue;
        }

        if (mel_gpu_texture_view_bindless_slot(c->dev, v.value) != v.value.slot.index)
            atomic_fetch_add(&c->slot_breaks, 1);
        if (mel_gpu_sampler_bindless_slot(c->dev, s.value) != s.value.slot.index)
            atomic_fetch_add(&c->slot_breaks, 1);

        c->textures[c->made] = t.value;
        c->views[c->made] = v.value;
        c->samplers[c->made] = s.value;
        c->made++;
    }
    return 0;
}

MEL_TEST(conc_create, bindless_slot_equals_index_under_contention)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = conc_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const u32 T = conc_threads();
    const u32 PER = 64;

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, T));
    Mel_Thread*        threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Conc_Bindless_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Conc_Bindless_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Conc_Bindless_Ctx){ .dev = dev, .start = &start, .per_thread = PER, .thread_ix = i };
        ctx[i].textures = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Texture, PER);
        ctx[i].views = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Texture_View, PER);
        ctx[i].samplers = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Sampler, PER);
        atomic_store(&ctx[i].failures, 0);
        atomic_store(&ctx[i].slot_breaks, 0);
        MEL_REQUIRE(mel_thread_spawn(&threads[i], conc_bindless_worker, &ctx[i], .name = "conc-bindless"));
    }
    for (u32 i = 0; i < T; i++)
        mel_thread_join(&threads[i], NULL);

    u32 total = 0, fails = 0, breaks = 0;
    for (u32 i = 0; i < T; i++)
    {
        total += ctx[i].made;
        fails += atomic_load(&ctx[i].failures);
        breaks += atomic_load(&ctx[i].slot_breaks);
    }
    MEL_EXPECT_EQ(fails, 0u);
    MEL_EXPECT_EQ(breaks, 0u);
    MEL_EXPECT_EQ(total, T * PER);

    u32* vidx = mel_alloc_array(mel_alloc_heap(), u32, total);
    u32* sidx = mel_alloc_array(mel_alloc_heap(), u32, total);
    u32  k = 0;
    for (u32 i = 0; i < T; i++)
        for (u32 j = 0; j < ctx[i].made; j++)
        {
            vidx[k] = ctx[i].views[j].slot.index;
            sidx[k] = ctx[i].samplers[j].slot.index;
            k++;
        }
    MEL_EXPECT(!conc_indices_collide(vidx, total));
    MEL_EXPECT(!conc_indices_collide(sidx, total));

    for (u32 i = 0; i < T; i++)
    {
        for (u32 j = 0; j < ctx[i].made; j++)
        {
            mel_gpu_sampler_destroy(dev, ctx[i].samplers[j]);
            mel_gpu_texture_view_destroy(dev, ctx[i].views[j]);
            mel_gpu_texture_destroy(dev, ctx[i].textures[j]);
        }
        mel_dealloc(mel_alloc_heap(), ctx[i].textures);
        mel_dealloc(mel_alloc_heap(), ctx[i].views);
        mel_dealloc(mel_alloc_heap(), ctx[i].samplers);
    }
    mel_dealloc(mel_alloc_heap(), vidx);
    mel_dealloc(mel_alloc_heap(), sidx);
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
    _Atomic(u32)    submits_ok;
} Conc_Record_Ctx;

static int conc_record_worker(void* user)
{
    Conc_Record_Ctx* c = user;
    mel_barrier_wait(c->start);
    for (u32 r = 0; r < c->rounds; r++)
    {
        Mel_Gpu_Buffer_Create_Result b =
            mel_gpu_buffer_create(c->dev, .size = 256, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_DEVICE, .name = "rec");
        if (mel_gpu_failed(b.status))
        {
            atomic_fetch_add(&c->failures, 1);
            continue;
        }

        Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(c->queue);
        if (!cmd)
        {
            atomic_fetch_add(&c->failures, 1);
            mel_gpu_buffer_destroy(c->dev, b.value);
            continue;
        }
        mel_gpu_command_list_begin(cmd);
        mel_gpu_cmd_buffer_barrier(cmd, b.value, MEL_GPU_STATE_COPY_DEST, MEL_GPU_STATE_UNORDERED_ACCESS);
        mel_gpu_command_list_end(cmd);

        Mel_Gpu_Future* f = mel_gpu_queue_submit(c->queue, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
        if (f && mel_gpu_ok(mel_gpu_future_status(f)))
            atomic_fetch_add(&c->submits_ok, 1);
        else
            atomic_fetch_add(&c->failures, 1);

        mel_gpu_buffer_destroy(c->dev, b.value);
        mel_gpu_future_destroy(f);
        mel_gpu_command_list_destroy(cmd);
    }
    return 0;
}

MEL_TEST(conc_record, per_thread_cl_record_and_submit)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = conc_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    Mel_Gpu_Queue* q = mel_gpu_queue_request(dev, MEL_GPU_QUEUE_GRAPHICS);
    MEL_REQUIRE_NOT_NULL(q);

    const u32 T = conc_threads();
    const u32 ROUNDS = 32;

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, T));
    Mel_Thread*      threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Conc_Record_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Conc_Record_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Conc_Record_Ctx){ .dev = dev, .queue = q, .start = &start, .rounds = ROUNDS, .thread_ix = i };
        atomic_store(&ctx[i].failures, 0);
        atomic_store(&ctx[i].submits_ok, 0);
        MEL_REQUIRE(mel_thread_spawn(&threads[i], conc_record_worker, &ctx[i], .name = "conc-record"));
    }
    for (u32 i = 0; i < T; i++)
        mel_thread_join(&threads[i], NULL);

    u32 fails = 0, ok = 0;
    for (u32 i = 0; i < T; i++)
    {
        fails += atomic_load(&ctx[i].failures);
        ok += atomic_load(&ctx[i].submits_ok);
    }
    MEL_EXPECT_EQ(fails, 0u);
    MEL_EXPECT_EQ(ok, T * ROUNDS);

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
    u32             per_thread;
    u32             thread_ix;
    _Atomic(u32)    failures;
    _Atomic(u32)    mismatches;
} Conc_Write_Ctx;

static int conc_write_worker(void* user)
{
    Conc_Write_Ctx* c = user;
    mel_barrier_wait(c->start);

    Mel_Gpu_Buffer* bufs = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Buffer, c->per_thread);
    usize*          sizes = mel_alloc_array(mel_alloc_heap(), usize, c->per_thread);
    u8*             tags = mel_alloc_array(mel_alloc_heap(), u8, c->per_thread);
    u32             made = 0;

    for (u32 i = 0; i < c->per_thread; i++)
    {
        usize sz = 32 + (usize)(c->thread_ix * 257 + i * 53) % 4000;
        u8    tag = (u8)(c->thread_ix * 37 + i + 1);
        Mel_Gpu_Buffer_Create_Result b = mel_gpu_buffer_create(c->dev, .size = sz, .usage = MEL_GPU_BUFFER_TRANSFER_SRC, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "cw");
        if (mel_gpu_failed(b.status))
        {
            atomic_fetch_add(&c->failures, 1);
            continue;
        }
        u8* pattern = mel_alloc_array(mel_alloc_heap(), u8, sz);
        memset(pattern, tag, sz);
        mel_gpu_buffer_write(c->dev, b.value, pattern, sz);
        mel_dealloc(mel_alloc_heap(), pattern);

        bufs[made] = b.value;
        sizes[made] = sz;
        tags[made] = tag;
        made++;
    }

    for (u32 i = 0; i < made; i++)
    {
        const u8* p = mel_gpu_buffer_mapped(c->dev, bufs[i]);
        if (!p)
        {
            atomic_fetch_add(&c->mismatches, 1);
            continue;
        }
        for (usize j = 0; j < sizes[i]; j++)
            if (p[j] != tags[i])
            {
                atomic_fetch_add(&c->mismatches, 1);
                break;
            }
    }
    for (u32 i = 0; i < made; i++)
        mel_gpu_buffer_destroy(c->dev, bufs[i]);
    mel_dealloc(mel_alloc_heap(), bufs);
    mel_dealloc(mel_alloc_heap(), sizes);
    mel_dealloc(mel_alloc_heap(), tags);
    return 0;
}

MEL_TEST(conc_write, distinct_resource_sentinels)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = conc_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 T = conc_threads();
    const u32 PER = 96;

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, T));
    Mel_Thread*     threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Conc_Write_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Conc_Write_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Conc_Write_Ctx){ .dev = dev, .start = &start, .per_thread = PER, .thread_ix = i };
        atomic_store(&ctx[i].failures, 0);
        atomic_store(&ctx[i].mismatches, 0);
        MEL_REQUIRE(mel_thread_spawn(&threads[i], conc_write_worker, &ctx[i], .name = "conc-write"));
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
} Conc_Bench_Ctx;

static int conc_bench_worker(void* user)
{
    Conc_Bench_Ctx* c = user;
    Mel_Gpu_Buffer* bufs = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Buffer, c->count);
    u32             made = 0;
    mel_barrier_wait(c->start);
    for (u32 i = 0; i < c->count; i++)
    {
        Mel_Gpu_Buffer_Create_Result b = mel_gpu_buffer_create(c->dev, .size = 256, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .name = "bn");
        if (mel_gpu_failed(b.status))
            atomic_fetch_add(&c->failures, 1);
        else
            bufs[made++] = b.value;
    }
    for (u32 i = 0; i < made; i++)
        mel_gpu_buffer_destroy(c->dev, bufs[i]);
    mel_dealloc(mel_alloc_heap(), bufs);
    return 0;
}

static u64 conc_bench_run(Mel_Gpu_Device* dev, u32 T, u32 total)
{
    u32         per = total / T;
    Mel_Barrier start;
    mel_barrier_init(&start, T);
    Mel_Thread*     threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Conc_Bench_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Conc_Bench_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Conc_Bench_Ctx){ .dev = dev, .start = &start, .count = per, .thread_ix = i };
        atomic_store(&ctx[i].failures, 0);
        mel_thread_spawn(&threads[i], conc_bench_worker, &ctx[i], .name = "conc-bench");
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

MEL_TEST(conc_slotmap, serialization_is_measured)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = conc_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 TOTAL = 2048;
    const u32 T = conc_threads();

    u64 one = conc_bench_run(dev, 1, TOTAL);
    u64 many = conc_bench_run(dev, T, TOTAL);
    MEL_REQUIRE(one != 0);
    MEL_REQUIRE(many != 0);

    double speedup = (double)one / (double)(many ? many : 1);
    mel_log_info("gpu-concurrency", "slotmap serialization: 1-thread %llu us, %u-thread %llu us, speedup %.2fx (lock-free MPMC would approach %ux; ~1x => obj_lock serializes)",
                 (unsigned long long)(one / 1000), T, (unsigned long long)(many / 1000), speedup, T);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

typedef struct
{
    Mel_Gpu_Thread_Tracker* tracker;
    Mel_Barrier*            start;
    int                     object;
    _Atomic(u32)*           done;
} Conc_Tracker_Ctx;

static int conc_tracker_distinct_worker(void* user)
{
    Conc_Tracker_Ctx* c = user;
    mel_barrier_wait(c->start);
    for (u32 r = 0; r < 200; r++)
    {
        mel_gpu_thread_tracker_enter(c->tracker, &c->object, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
        mel_gpu_thread_tracker_enter(c->tracker, &c->object, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
        mel_gpu_thread_tracker_exit(c->tracker, &c->object);
        mel_gpu_thread_tracker_exit(c->tracker, &c->object);
    }
    atomic_fetch_add(c->done, 1);
    return 0;
}

static int conc_tracker_concurrent_worker(void* user)
{
    Conc_Tracker_Ctx* c = user;
    mel_barrier_wait(c->start);
    for (u32 r = 0; r < 200; r++)
    {
        mel_gpu_thread_tracker_enter(c->tracker, &c->object, MEL_GPU_CONCURRENCY_CONCURRENT);
        mel_gpu_thread_tracker_exit(c->tracker, &c->object);
    }
    atomic_fetch_add(c->done, 1);
    return 0;
}

MEL_TEST(conc_tracker, distinct_objects_and_concurrent_class)
{
    Mel_Gpu_Thread_Tracker* tracker = mel_gpu_thread_tracker_create();
    MEL_REQUIRE_NOT_NULL(tracker);

    const u32    T = conc_threads();
    _Atomic(u32) done;
    atomic_store(&done, 0);

    Mel_Barrier start;
    MEL_REQUIRE(mel_barrier_init(&start, T));

    Mel_Thread*       threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Conc_Tracker_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Conc_Tracker_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Conc_Tracker_Ctx){ .tracker = tracker, .start = &start, .object = (int)i, .done = &done };
        MEL_REQUIRE(mel_thread_spawn(&threads[i], conc_tracker_distinct_worker, &ctx[i], .name = "trk-distinct"));
    }
    for (u32 i = 0; i < T; i++)
        mel_thread_join(&threads[i], NULL);
    MEL_EXPECT_EQ(atomic_load(&done), T);

    atomic_store(&done, 0);
    mel_barrier_destroy(&start);
    MEL_REQUIRE(mel_barrier_init(&start, T));
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Conc_Tracker_Ctx){ .tracker = tracker, .start = &start, .object = (int)i, .done = &done };
        MEL_REQUIRE(mel_thread_spawn(&threads[i], conc_tracker_concurrent_worker, &ctx[i], .name = "trk-conc"));
    }
    for (u32 i = 0; i < T; i++)
        mel_thread_join(&threads[i], NULL);
    MEL_EXPECT_EQ(atomic_load(&done), T);

    int fresh = 0;
    mel_gpu_thread_tracker_enter(tracker, &fresh, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    mel_gpu_thread_tracker_exit(tracker, &fresh);

    mel_dealloc(mel_alloc_heap(), ctx);
    mel_dealloc(mel_alloc_heap(), threads);
    mel_barrier_destroy(&start);
    mel_gpu_thread_tracker_destroy(tracker);
}

MEL_TEST(conc_tracker, device_accepts_flag_but_tracker_is_unwired)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-concurrency", .debug = { .enabled = true });
    MEL_REQUIRE_NOT_NULL(inst);
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    MEL_REQUIRE(n > 0);

    Mel_Gpu_Device_Create_Result dr =
        mel_gpu_device_create(inst, adapters[0], .features = { .timeline_semaphores = true }, .debug = { .enabled = true, .thread_safety_tracker = true });
    MEL_REQUIRE_NOT_NULL(dr.value);

    Mel_Gpu_Buffer_Create_Result b = mel_gpu_buffer_create(dr.value, .size = 256, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .name = "unwired");
    MEL_EXPECT(!mel_gpu_failed(b.status));
    MEL_EXPECT(mel_gpu_buffer_alive(dr.value, b.value));
    mel_gpu_buffer_destroy(dr.value, b.value);

    mel_gpu_device_destroy(dr.value);
    mel_gpu_instance_destroy(inst);
}

#else

#include <test/test.h>
MEL_TEST(concurrency, backend_absent) { MEL_SKIP("gpu-concurrency requires the Vulkan backend (--gpu=vulkan)"); }

#endif
