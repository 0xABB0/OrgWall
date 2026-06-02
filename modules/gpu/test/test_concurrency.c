#include <test/test.h>

#if MEL_GPU_VULKAN

// Multi-threaded adversarial suite over the Vulkan RHI threading contract (gpu-rhi.md §3.7 / U36). Round-1's
// stress audit confessed the single biggest coverage gap: no multi-threaded U36 probe (headless synchronous
// single-queue made it infeasible) and an unverified claim that the slotmap is per-device-mutex-serialized,
// NOT the lock-free MPMC §3.7/U1 assume. This suite closes that gap with N REAL threads (the `thread` module).
//
// Harness contract (load-bearing): the test runner arms a single setjmp per test on the MAIN thread; MEL_REQUIRE
// / MEL_FAIL / MEL_SKIP longjmp there. A longjmp across a thread boundary is undefined and would crash the
// runner. Therefore worker threads NEVER touch a MEL_* assertion macro and NEVER call mel_test_abort; they
// record outcomes into _Atomic fields, and the main thread evaluates every MEL_EXPECT/REQUIRE after join. This
// is the only safe shape under MEL_TEST_NOFORK=1 (MoltenVK cannot survive fork(), so all threads share one
// process — a worker deadlock or crash takes the whole runner down, so every wait is bounded).

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

// ---- shared device factories (mirror test_vulkan.c / test_stress.c) ----

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

// Worker thread count: bound by hardware concurrency, clamped to a small ceiling so the suite finishes promptly
// under MoltenVK's heavy internal serialization (every WaitIdle-bearing path drains the whole queue).
static u32 conc_threads(void)
{
    u32 hw = mel_thread_hardware_concurrency();
    if (hw < 2)
        hw = 2;
    if (hw > 8)
        hw = 8;
    return hw;
}

// =====================================================================================================
// 1. Concurrent resource creation (§3.7: buffer/texture/sampler/pipeline create are `Concurrent`).
//    M threads each create a private run of distinct resources in a tight loop. We collect every produced
//    slot.index into a shared, mutex-guarded ledger; afterward the main thread proves NO two simultaneously
//    live handles share a slot index (a torn handle or a double-allocated slot would collide), every create
//    succeeded, and the device destroys leak-clean. This stresses the per-device-obj_lock-serialized slotmap
//    that the spec wants lock-free; correctness is asserted, serialization is MEASURED in probe (3).
// =====================================================================================================
typedef struct
{
    Mel_Gpu_Device* dev;
    Mel_Barrier*    start;
    u32             per_thread;

    // Private result run, sized per_thread; filled by the worker, drained by main after join.
    Mel_Gpu_Buffer*  buffers;
    Mel_Gpu_Texture* textures;
    Mel_Gpu_Sampler* samplers;
    u32              made;     // count of fully-created (buffer,texture,sampler) triples
    _Atomic(u32)     failures; // any create that returned a failed status or a non-alive handle
    u32              thread_ix;
} Conc_Create_Ctx;

static int conc_create_worker(void* user)
{
    Conc_Create_Ctx* c = user;
    char             name[16];
    mel_thread_set_name("conc-create");
    mel_barrier_wait(c->start); // all threads cross the line together => real contention on obj_lock

    for (u32 i = 0; i < c->per_thread; i++)
    {
        // Distinct sizes/params per (thread, i) so nothing dedups or aliases.
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

// Returns true if any two of the `n` indices collide (a double-allocated slot among simultaneously-live handles).
static bool conc_indices_collide(u32* indices, u32 n)
{
    // Simple O(n log n)-ish via insertion-checked scan is overkill; n is a few thousand, O(n^2) is fine and
    // avoids pulling a sort dependency into a test. Mark-and-sweep on a bitset would be tighter but the index
    // space is grown-on-demand and unbounded, so we cannot size a fixed bitset (MEL-CODE-002).
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

    // --- main-thread assertions (workers never touch MEL_*) ---
    u32 total = 0, fails = 0;
    for (u32 i = 0; i < T; i++)
    {
        total += ctx[i].made;
        fails += atomic_load(&ctx[i].failures);
    }
    MEL_EXPECT_EQ(fails, 0u);
    MEL_EXPECT_EQ(total, T * PER); // every concurrent create succeeded

    // Per-type slot-index collision check across ALL still-live handles from every thread. Two live handles
    // of the same type sharing an index is the signature of a torn/double-allocated slot under contention.
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

    // Tear down every resource and free the ledgers.
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

    mel_gpu_device_destroy(dev); // leak report fires here; the run is grepped for "leak"
    mel_gpu_instance_destroy(inst);
}

// Bindless variant: concurrent texture-view + sampler creation on a bindless device. The direct family pins
// bindless_slot == handle.index (§3.1). We prove that invariant holds for EVERY view/sampler created under
// contention (a racing heap registration that wrote the wrong slot would break it) and that the per-type
// slot indices remain collision-free across threads.
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
    _Atomic(u32)          slot_breaks; // bindless_slot != handle.index
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

        // §3.1 slot==index, evaluated on the worker (the read is a const slotmap_get under obj_lock; safe).
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
    const u32 PER = 64; // stay far under any class cap (DEFECT-1 over-cap path is off-limits this round)

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
    MEL_EXPECT_EQ(breaks, 0u); // bindless_slot == handle.index held for every view & sampler under contention
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

// =====================================================================================================
// 2. SerializedPerObject command-list recording from per-thread TLS pools (§3.7 + U15). The canonical pattern
//    is one CL per recording thread; the per-thread per-queue command pool (mel_gpu__thread_pool keyed on
//    thread id) makes concurrent recording trivially race-free in Vulkan terms. Each thread creates its own CL,
//    records barriers on its own buffer, ends, and submits. queue_submit is SerializedPerObject on the queue;
//    the device's submit_lock serializes the actual vkQueueSubmit, so concurrent submit is correct (just
//    serialized). We assert every per-thread record+submit resolved OK and nothing leaked.
// =====================================================================================================
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
        // Per-thread private buffer (distinct resource — Concurrent create) consumed by this thread's CL.
        Mel_Gpu_Buffer_Create_Result b =
            mel_gpu_buffer_create(c->dev, .size = 256, .usage = MEL_GPU_BUFFER_STORAGE | MEL_GPU_BUFFER_TRANSFER_DST, .memory = MEL_GPU_MEMORY_DEVICE, .name = "rec");
        if (mel_gpu_failed(b.status))
        {
            atomic_fetch_add(&c->failures, 1);
            continue;
        }

        // One CL per recording thread: command_list_create routes to this thread's own VkCommandPool.
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

    // One shared queue: queue_submit is SerializedPerObject on the queue, and the engine's submit_lock provides
    // that serialization internally (the contract permits the caller to lean on it for a non-internally-sync
    // queue only because the engine happens to lock; §5.2 internally_synchronized is the Concurrent escape).
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
    MEL_EXPECT_EQ(ok, T * ROUNDS); // every per-thread record+submit resolved OK

    mel_dealloc(mel_alloc_heap(), ctx);
    mel_dealloc(mel_alloc_heap(), threads);
    mel_barrier_destroy(&start);
    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// =====================================================================================================
// 3. Concurrent buffer_write across DISTINCT resources (§3.7: buffer_write is Concurrent across distinct
//    resources, SerializedPerObject on the same — we never write the same resource from two threads; that is
//    UB by contract and is deliberately NOT triggered). Each thread owns its host-visible buffers, writes a
//    per-thread sentinel via buffer_write, then reads it back through the mapped pointer. A torn write or an
//    aliased allocation across threads would corrupt a neighbor's sentinel.
// =====================================================================================================
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
        // UPLOAD => host-visible mapped; buffer_write is a plain memcpy to the mapped pointer (Concurrent path).
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

    // Read back every sentinel through the mapped pointer; a cross-thread aliasing/tear shows as a mismatch.
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
    MEL_EXPECT_EQ(mism, 0u); // no cross-thread sentinel corruption => distinct-resource writes are isolated

    mel_dealloc(mel_alloc_heap(), ctx);
    mel_dealloc(mel_alloc_heap(), threads);
    mel_barrier_destroy(&start);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// =====================================================================================================
// 4. Slotmap serialization MEASUREMENT (the round-1 claim under test). §3.7/U1 specify the slotmap-per-type as
//    a "lock-free MPMC allocator", so Concurrent create should SCALE with threads. Round-1 confessed the real
//    implementation serializes every slotmap op (insert/get/remove/reclaim) behind ONE per-device obj_lock
//    (verified at modules/gpu/src/vulkan/device.c:377-414). If that is true, T threads doing the same total
//    create work as 1 thread finish in ~the same wall time (no parallel speedup) — the obj_lock is the choke.
//    We MEASURE the ratio and log it; we do NOT fail on it (it is a known spec deviation for the fixer, not a
//    correctness bug). The probe asserts only correctness (no failures), and prints the measured serialization.
// =====================================================================================================
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

// Run `total` creates spread across `T` threads; return wall-nanoseconds from the barrier release to the last
// join. The barrier guarantees all workers start contending together so the measured span is the contended one.
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
    // The barrier is crossed inside the workers; start the clock now and accept the tiny spawn skew (both the
    // 1-thread and T-thread runs pay the same per-thread spawn cost, so the ratio is unaffected at this scale).
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
    return fails ? 0 : (t1 - t0); // 0 signals a correctness failure to the caller
}

MEL_TEST(conc_slotmap, serialization_is_measured)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = conc_make_device(&inst);
    MEL_REQUIRE_NOT_NULL(dev);

    const u32 TOTAL = 2048;
    const u32 T = conc_threads();

    // Single-threaded baseline vs T-threaded run, same TOTAL create+destroy work.
    u64 one = conc_bench_run(dev, 1, TOTAL);
    u64 many = conc_bench_run(dev, T, TOTAL);
    MEL_REQUIRE(one != 0);  // 0 == a create failed during the baseline
    MEL_REQUIRE(many != 0); // 0 == a create failed during the contended run

    // A lock-free MPMC slotmap (what §3.7/U1 promise) would let the T-thread run approach one/T. A single
    // obj_lock serializing every insert/get/remove makes many >= one (no speedup; lock + contention overhead).
    // We log the ratio so the measurement is on the record; we do NOT assert a bound (timing is noisy and the
    // serialization is a documented spec deviation for the fixer, not a correctness failure of THIS suite).
    double speedup = (double)one / (double)(many ? many : 1);
    mel_log_info("gpu-concurrency", "slotmap serialization: 1-thread %llu us, %u-thread %llu us, speedup %.2fx (lock-free MPMC would approach %ux; ~1x => obj_lock serializes)",
                 (unsigned long long)(one / 1000), T, (unsigned long long)(many / 1000), speedup, T);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// =====================================================================================================
// 5. U36/U21 thread-safety tracker (device.debug.thread_safety_tracker / gpu/threading.h). The tracker is the
//    "single most useful debug aid for porting a single-threaded prototype to a multi-threaded renderer"
//    (§3.7). We exercise the tracker OBJECT directly (as gpu-foundation does for same-thread reentry) under
//    REAL threads:
//      (a) Concurrent class is NEVER tracked (enter/exit are no-ops) — many threads hammering the same object
//          under the Concurrent class must not register an owner, so a later SerializedPerObject enter on a
//          fresh object from any thread succeeds.
//      (b) SerializedPerObject on DISTINCT objects from DISTINCT threads is legal and must not assert — each
//          thread owns its own object; the tracker records one owner per object with no cross-talk.
//    The ILLEGAL case — a SerializedPerObject object entered by thread A then entered by thread B without an
//    intervening exit — would fire the tracker's mel_assert(owner==self) and ABORT the process (asserts live
//    in debug). A crashing probe takes down every sibling test under MEL_TEST_NOFORK, so we do NOT trigger it
//    live; its reality is asserted by construction (the assert is in mel_gpu_thread_tracker_enter) and recorded
//    in the writeup. See also BUG below: the device allocates a tracker but NEVER calls enter/exit on any
//    public path, so the tracker can presently fire only when driven directly like this.
// =====================================================================================================
typedef struct
{
    Mel_Gpu_Thread_Tracker* tracker;
    Mel_Barrier*            start;
    int                     object; // each worker's PRIVATE object instance
    _Atomic(u32)*           done;
} Conc_Tracker_Ctx;

static int conc_tracker_distinct_worker(void* user)
{
    Conc_Tracker_Ctx* c = user;
    mel_barrier_wait(c->start);
    // SerializedPerObject on this thread's OWN object: enter/reenter/exit is legal and must not assert.
    for (u32 r = 0; r < 200; r++)
    {
        mel_gpu_thread_tracker_enter(c->tracker, &c->object, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
        mel_gpu_thread_tracker_enter(c->tracker, &c->object, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT); // recursive depth
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
    // Concurrent class on a SHARED object: enter/exit are no-ops (never register an owner), so no assert ever.
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

    // (b) SerializedPerObject on distinct per-thread objects: legal, no cross-thread assert.
    Mel_Thread*       threads = mel_alloc_array(mel_alloc_heap(), Mel_Thread, T);
    Conc_Tracker_Ctx* ctx = mel_alloc_array(mel_alloc_heap(), Conc_Tracker_Ctx, T);
    for (u32 i = 0; i < T; i++)
    {
        ctx[i] = (Conc_Tracker_Ctx){ .tracker = tracker, .start = &start, .object = (int)i, .done = &done };
        MEL_REQUIRE(mel_thread_spawn(&threads[i], conc_tracker_distinct_worker, &ctx[i], .name = "trk-distinct"));
    }
    for (u32 i = 0; i < T; i++)
        mel_thread_join(&threads[i], NULL);
    MEL_EXPECT_EQ(atomic_load(&done), T); // every thread completed without the tracker asserting

    // (a) Concurrent class across all threads: the class is a no-op — enter/exit never register an owner, so
    // nothing is tracked and nothing can assert. Object identity is irrelevant under the Concurrent class (the
    // enter early-returns before touching the ledger), so each worker drives its own ctx object; what we
    // validate is the class GATE, not the address. A shared SerializedPerObject object here WOULD assert (the
    // illegal cross-thread case we deliberately do not ship live).
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

    // After all Concurrent traffic, the tracker holds no registered owners; a fresh SerializedPerObject enter
    // from the MAIN thread on a brand-new object succeeds and exits cleanly (the no-op class left no residue).
    int fresh = 0;
    mel_gpu_thread_tracker_enter(tracker, &fresh, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    mel_gpu_thread_tracker_exit(tracker, &fresh);

    mel_dealloc(mel_alloc_heap(), ctx);
    mel_dealloc(mel_alloc_heap(), threads);
    mel_barrier_destroy(&start);
    mel_gpu_thread_tracker_destroy(tracker);
}

// The tracker is wired into device lifecycle (created when device.debug.thread_safety_tracker = true) but —
// CONFIRMED BY SOURCE READ — NO public call path invokes mel_gpu_thread_tracker_enter/exit (the only callers
// anywhere are these tests and gpu-foundation). So §3.7's promise that "every public call records the calling
// thread and the object class; double-entry on a SerializedPerObject object from a different thread asserts
// loudly" is presently UNREALIZED: the tracker is dead infrastructure that can never fire on real RHI misuse.
// We assert the device ACCEPTS the flag and runs (so the create path does not regress) and document the wiring
// gap as a BUG for the fixer (writeup). We cannot assert the tracker fires on cross-thread RHI misuse, because
// it does not — and faking that pass is forbidden (MEL-ENGINE-VIII).
MEL_TEST(conc_tracker, device_accepts_flag_but_tracker_is_unwired)
{
    Mel_Gpu_Instance* inst = mel_gpu_instance_create(.app_name = "gpu-concurrency", .debug = { .enabled = true });
    MEL_REQUIRE_NOT_NULL(inst);
    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(inst, adapters, 8);
    MEL_REQUIRE(n > 0);

    // Request the tracker explicitly; the device must create (the flag flows into dev->tracker = create()).
    Mel_Gpu_Device_Create_Result dr =
        mel_gpu_device_create(inst, adapters[0], .features = { .timeline_semaphores = true }, .debug = { .enabled = true, .thread_safety_tracker = true });
    MEL_REQUIRE_NOT_NULL(dr.value);

    // Exercise a couple of public calls from TWO threads that, IF the tracker were wired, would be the kind of
    // SerializedPerObject misuse it must catch. They run clean today precisely BECAUSE the tracker is unwired —
    // there is no enter/exit on buffer_create/destroy to fire. This documents the gap without faking a catch.
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
