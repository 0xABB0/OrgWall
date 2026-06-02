#include <test/test.h>

#if MEL_GPU_VULKAN

// Adversarial stress / churn suite over the Vulkan RHI (gpu-rhi.md §3/§6/§7). Each probe asserts an
// invariant and is intended to be validation- and leak-clean. Where a probe surfaces a real backend bug the
// auditor cannot work around (the builder owns src/** fixes), it is written as a clearly-documented bounded
// probe that stays within the contract that does NOT crash (mel_assert is live in debug, MEL_ASSERT_ENABLED),
// and the writeup carries the defect. We never fake a pass (MEL-ENGINE-VIII).

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

#include "bindless_spv.h"

#include <string.h>

// ---- shared device factories (mirror test_vulkan.c) ----

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

// Submit an empty command list and block on its completion future, advancing the retirement watermark so
// deferred-free entries gated at or below this submission retire. The canonical "drain one frame" primitive.
static void stress_drain(Mel_Gpu_Device* dev, Mel_Gpu_Queue* q)
{
    Mel_Gpu_Command_List* cmd = mel_gpu_command_list_create(q);
    mel_gpu_command_list_begin(cmd);
    mel_gpu_command_list_end(cmd);
    Mel_Gpu_Future* f = mel_gpu_queue_submit(q, (Mel_Gpu_Submit){ .command_lists = &cmd, .command_list_count = 1 });
    mel_gpu_future_destroy(f);
    mel_gpu_command_list_destroy(cmd);
}

// =====================================================================================================
// 1. Resource churn — create/destroy thousands of buffers across simulated frames; the deferred-free queue
//    must drain on each retirement edge (no unbounded growth), the leak count must be zero at device destroy,
//    and a stale handle must read NULL after destroy (generation roll, gpu-rhi.md §3.1).
// =====================================================================================================
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
        // Touch a submission so the destroyed buffers are deferred-gated on a real serial.
        stress_drain(dev, q);
        Mel_Gpu_Buffer stale = live[0];
        for (u32 i = 0; i < PER_FRAME; i++)
            mel_gpu_buffer_destroy(dev, live[i]);
        // Generation rolled immediately: the stale handle reads not-alive even before the GPU retires it.
        MEL_EXPECT(!mel_gpu_buffer_alive(dev, stale));
        // Retire this frame's destroys so the deferred queue drains rather than accreting.
        stress_drain(dev, q);
    }

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev); // leak report fires here; the run is grepped for "leak"
    mel_gpu_instance_destroy(inst);
}

// Textures + views + samplers + pipelines churned together: exercises the deferred-free path for every owned
// resource type and the future-gated bindless-slot reclamation for views/samplers (gpu-rhi.md §3.3 / §6.7).
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

        // The view and sampler are heap-registered at their handle index — assert the slot==index contract.
        MEL_EXPECT_EQ(mel_gpu_texture_view_bindless_slot(dev, v.value), v.value.slot.index);
        MEL_EXPECT_EQ(mel_gpu_sampler_bindless_slot(dev, s.value), s.value.slot.index);

        stress_drain(dev, q);
        mel_gpu_sampler_destroy(dev, s.value);
        mel_gpu_texture_view_destroy(dev, v.value);
        mel_gpu_texture_destroy(dev, t.value);
        stress_drain(dev, q); // retire the deferred frees + slot reclamation
    }

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// Slot reclamation invariant under churn: a destroyed view's slot index is reused by a later create only
// after its retirement edge passes, and the heap-registration contract still holds (slot==index) at reuse.
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
    stress_drain(dev, q); // reclaim the held slot

    Mel_Gpu_Texture_View_Create_Result b = mel_gpu_texture_default_view(dev, t.value);
    MEL_REQUIRE(!mel_gpu_failed(b.status));
    // The reclaimed index is reused; the new view re-registers the heap descriptor at the same index.
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

// =====================================================================================================
// 2. Bindless heap — fill the sampler class to a meaningful live count, asserting slot==index throughout,
//    then drain. We deliberately stay UNDER the per-class cap: registering past it trips a live mel_assert in
//    mel_gpu__bindless_check (a crash in debug; a silently-dropped descriptor in release). That over-cap path
//    is a DEFECT for the builder, documented in the writeup — not exercised here, since it would abort the run
//    (MEL-ENGINE-VIII: we do not fake a pass, and we do not crash the suite to prove a known bug).
// =====================================================================================================
MEL_TEST(stress_bindless, fill_sampler_class_under_cap)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device_bindless(&inst);
    MEL_REQUIRE_NOT_NULL(dev);
    MEL_REQUIRE(mel_gpu_bindless_available(dev));

    const Mel_Gpu_Caps* caps = mel_gpu_device_caps(dev);
    u32                 cap = caps->memory.bindless.max_sampler_slots;
    MEL_REQUIRE(cap >= 64);

    // Distinct descriptors so dedup does not collapse them (vary wrap by index). Keep well under the cap so
    // no handle index reaches the heap bound (see DEFECT-1: index can exceed cap under enough live resources).
    u32 n = cap / 4 < 256 ? cap / 4 : 256;
    Mel_Gpu_Sampler* samps = mel_alloc_array(mel_alloc_heap(), Mel_Gpu_Sampler, n);
    for (u32 i = 0; i < n; i++)
    {
        Mel_Gpu_Sampler_Create_Result s = mel_gpu_sampler_create(dev, .min_filter = MEL_GPU_FILTER_NEAREST, .mag_filter = MEL_GPU_FILTER_NEAREST,
                                                                 .wrap_u = (Mel_Gpu_Wrap)(i % 4), .wrap_v = (Mel_Gpu_Wrap)((i / 4) % 4),
                                                                 .lod_max = (f32)(i + 1), .name = "fill");
        MEL_REQUIRE(!mel_gpu_failed(s.status));
        samps[i] = s.value;
        // slot == handle.index for every direct heap-registered sampler (§3.1), and within the class cap.
        MEL_EXPECT_EQ(mel_gpu_sampler_bindless_slot(dev, s.value), s.value.slot.index);
        MEL_EXPECT(s.value.slot.index < cap);
    }
    for (u32 i = 0; i < n; i++)
        mel_gpu_sampler_destroy(dev, samps[i]);
    mel_dealloc(mel_alloc_heap(), samps);

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// Over-cap detection through the SANCTIONED path: a pipeline whose shader declares a SIZED set-0 array longer
// than the class cap fails create with MissingBindlessSlot (graceful status, no crash, no VUID). This is the
// only over-cap surface the writeup promises is graceful; resource over-registration is not (DEFECT-1).
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

// Sampler dedup under churn: many requests for a SMALL set of distinct descriptors must collapse to that set
// of shared handles (refcount), and the shared handle must survive until the last claim is dropped (§6.3).
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
        // Every claim is the same interned handle (slot index + generation).
        MEL_EXPECT(mel_gpu_handle_eq(claims[i].slot, claims[0].slot));
    }
    // Drop all but one claim; the shared sampler stays alive until the final destroy.
    for (u32 i = 1; i < CLAIMS; i++)
        mel_gpu_sampler_destroy(dev, claims[i]);
    MEL_EXPECT(mel_gpu_sampler_alive(dev, claims[0]));
    mel_gpu_sampler_destroy(dev, claims[0]);
    MEL_EXPECT(!mel_gpu_sampler_alive(dev, claims[0]));

    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// =====================================================================================================
// 3. Buddy suballocator (src/vulkan/memory.c) — many odd-sized, over-aligned, and dedicated-path allocations
//    interleaved with frees. Each buffer carries host-visible memory; we write a per-buffer sentinel pattern
//    and read it back, proving no two live suballocations overlap (corruption would mismatch the sentinel).
// =====================================================================================================
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

    // Pseudo-random but deterministic churn: allocate odd sizes, free a rolling subset, re-allocate.
    u32 rng = 0x1234567u;
    for (u32 step = 0; step < 1500; step++)
    {
        rng = rng * 1664525u + 1013904223u;
        u32 idx = rng % N;
        if (slots[idx].live)
        {
            // Verify the sentinel survived since the last write, then free.
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
            // Odd, prime-ish sizes spanning the min-block (256) boundary; UPLOAD => host-visible + mapped.
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
    // Final pass: every still-live sentinel must be intact (no overlap corrupted it), then free all.
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

// Dedicated path (size >= 32 MiB threshold) interleaved with suballocated buffers: the large allocation must
// not corrupt the small ones and must free cleanly. Also exercises the BDA-capable allocate flag path.
MEL_TEST(stress_alloc, dedicated_interleaved)
{
    Mel_Gpu_Instance* inst = NULL;
    Mel_Gpu_Device*   dev = stress_make_device_bindless(&inst); // bda_enabled => DEVICE_ADDRESS allocate flag
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

    // > MEL_GPU_DEDICATED_THRESHOLD (32 MiB) forces the dedicated vkAllocateMemory path.
    Mel_Gpu_Buffer_Create_Result big = mel_gpu_buffer_create(dev, .size = 40ull * 1024 * 1024, .usage = MEL_GPU_BUFFER_STORAGE, .memory = MEL_GPU_MEMORY_DEVICE, .name = "big");
    MEL_REQUIRE(!mel_gpu_failed(big.status));

    // The small sentinels survive the big allocation.
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

// =====================================================================================================
// 4. buffer_write / texture_write + readback fuzz — byte-exact roundtrips across sizes and the supported
//    formats. Validates the staging-upload path and the GPU copy-to-buffer readback (gpu-rhi.md §6.1/§6.2).
// =====================================================================================================
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

        // DEVICE buffer written via the staging upload path (buffer_write -> staging copy), read back via GPU copy.
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

// texture_write + copy-to-buffer readback across several dimensions for the RGBA8 path (the only blittable,
// host-readable color format the enum exposes without block compression). Byte-exact roundtrip per texel.
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

// =====================================================================================================
// 5. Command-list / TLS-pool churn — create/begin/end/destroy many command lists from the per-thread per-queue
//    pool registry (gpu-rhi.md §7.1), interleaved with destroy-immediately-after-submit so the deferred-free
//    path runs while pools recycle their command buffers (RESET_COMMAND_BUFFER_BIT, free back to the pool).
// =====================================================================================================
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
        // A buffer destroyed immediately after a submit that referenced it: its free is gated on this serial.
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
        // Destroy the buffer right after submit (before any explicit retire): the deferred queue gates it.
        mel_gpu_buffer_destroy(dev, b.value);
        mel_gpu_future_destroy(f);
        mel_gpu_command_list_destroy(cmd);
    }

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// =====================================================================================================
// 6. Compute storage-buffer bindless under churn — the add kernel (out[i]=in[i]+1) addressed purely by heap
//    slot, repeated across rounds with fresh buffers each round (slot churn + reclamation under a real GPU
//    consumer). Proves the heap descriptor at a reused slot is the new buffer, not a stale one (§6.7).
// =====================================================================================================
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
        stress_drain(dev, q); // retire so slots reclaim before the next round reuses them
    }

    mel_gpu_queue_release(q);
    mel_gpu_pipeline_destroy(dev, pipe.value);
    mel_gpu_shader_destroy(dev, sh.value);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// =====================================================================================================
// 7. Future / retirement — many in-flight queue_submit futures. The headless test device carries no reactor,
//    so queue_submit resolves synchronously by blocking on the submit fence (the no-pump path in queue.c);
//    each future must still resolve OK, the watermark must advance per submit, and nothing leaks. The
//    reactor-driven pump + backpressure high-water (U3) is exercised by gpu-foundation's pump tests; here we
//    stress the submit/retire cadence at volume. A standalone manual pump probe covers backpressure shape.
// =====================================================================================================
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
        // No reactor => synchronous resolution on the fence (queue.c no-pump branch).
        MEL_EXPECT(mel_gpu_future_resolved(f));
        MEL_EXPECT(mel_gpu_ok(mel_gpu_future_status(f)));
        mel_gpu_future_destroy(f);
        mel_gpu_command_list_destroy(cmd);
    }

    mel_gpu_queue_release(q);
    mel_gpu_device_destroy(dev);
    mel_gpu_instance_destroy(inst);
}

// Backpressure shape (U3, gpu-rhi.md §3.3) on a standalone manual-tick pump (NULL reactor): the high-water
// warning fires above the mark and the same-future re-resolve coalesces to a no-op rather than a queue
// duplicate. We stay below the 4x hard-assert ceiling. This is the pump's contract in isolation; the GPU
// device's submit poller multiplexes onto it identically.
static void stress_future_then_noop(Mel_Gpu_Future* f, void* user)
{
    (void)f;
    *(u32*)user += 1;
}

MEL_TEST(stress_future, pump_backpressure_coalesce)
{
    Mel_Gpu_Completion_Pump* pump = mel_gpu_pump_create_opt(NULL, (Mel_Gpu_Pump_Opt){ .high_water = 8 });
    MEL_REQUIRE_NOT_NULL(pump);

    const u32       N = 32; // 4x high-water is 32; stay at/under the hard ceiling (assert is < 4x)
    Mel_Gpu_Future* futs[N];
    u32             delivered = 0;
    for (u32 i = 0; i < N; i++)
    {
        futs[i] = mel_gpu_future_create(pump, NULL);
        mel_gpu_future_then(futs[i], stress_future_then_noop, &delivered);
    }
    // Enqueue all without ticking: depth climbs past high-water (warns once), never past the assert ceiling.
    for (u32 i = 0; i < N; i++)
    {
        mel_gpu_future_resolve(futs[i], NULL, MEL_GPU_STATUS(0, MEL_GPU_SEVERITY_OK));
        // Re-resolving the same future is idempotent and must NOT enqueue a duplicate (coalesce).
        mel_gpu_future_resolve(futs[i], NULL, MEL_GPU_STATUS(9, MEL_GPU_SEVERITY_ERROR));
    }
    mel_gpu_pump_tick(pump);
    // Every unique completion delivered exactly once — no drops, no duplicates.
    MEL_EXPECT_EQ(delivered, N);

    for (u32 i = 0; i < N; i++)
        mel_gpu_future_destroy(futs[i]);
    mel_gpu_pump_destroy(pump);
}

#else

#include <test/test.h>
MEL_TEST(stress, backend_absent) { MEL_SKIP("gpu-stress requires the Vulkan backend (--gpu=vulkan)"); }

#endif
