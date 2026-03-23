#include "render.response.h"
#include "render.viewport.h"
#include "render.target.h"
#include "render.texture_table.h"
#include "texture.pool.h"
#include "gpu.device.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.pipeline_cache.h"
#include "gpu.cmd.h"
#include "gpu.texture.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "collection.array.h"
#include "boot.registry.h"
#include "event.channel.h"
#include "core.engine.h"
#include "async.job.h"

#include <assert.h>
#include <math.h>
#include <string.h>

typedef struct {
    u32 id;
    i32 order;
    u64 sequence;
    const Mel_Render_Response_Op_Type* type;
    void* params;
    void* user_data;
    bool live;
} Mel_Render_Response_Op_Slot;

typedef struct {
    Mel_Render_View_Handle view;
    Mel_Array(Mel_Render_Response_Op_Slot) ops;
} Mel_Render_View_Response_Store;

typedef struct {
    f32 multiplier;
    f32 _pad0;
    f32 _pad1;
    f32 _pad2;
} Mel_Render_Response_Exposure_Push;

_Static_assert(sizeof(Mel_Render_Response_Exposure_Push) == 16,
               "Mel_Render_Response_Exposure_Push must be 16 bytes");

static Mel_Array(Mel_Render_View_Response_Store) s_response_stores;
static u32 s_next_response_op_id = 1;
static u64 s_next_response_sequence = 1;

static Mel_Gpu_Device* s_dev;
static Mel_Gpu_Shader s_exposure_shader;
static void* s_response_sampler;
static bool s_response_ready;

static void mel__render_response_storage_init(void)
{
    if (s_response_stores.allocator == nullptr)
        mel_array_init(&s_response_stores, mel_alloc_heap());
}

static void mel__render_response_slot_clear(Mel_Render_Response_Op_Slot* slot)
{
    assert(slot != nullptr);

    if (slot->params != nullptr)
    {
        assert(slot->type != nullptr);
        mel_aligned_dealloc(mel_alloc_heap(), slot->params, slot->type->params_align);
    }

    *slot = (Mel_Render_Response_Op_Slot){0};
}

static Mel_Render_View_Response_Store* mel__render_response_find_store(Mel_Render_View_Handle view)
{
    if (!mel_render_view_handle_valid(view))
        return nullptr;

    mel__render_response_storage_init();

    for (usize i = 0; i < s_response_stores.count; i++)
    {
        Mel_Render_View_Response_Store* store = &s_response_stores.items[i];
        if (store->view.handle.index == view.handle.index &&
            store->view.handle.generation == view.handle.generation)
            return store;
    }

    return nullptr;
}

static Mel_Render_View_Response_Store* mel__render_response_get_or_create_store(Mel_Render_View_Handle view)
{
    Mel_Render_View_Response_Store* store = mel__render_response_find_store(view);
    if (store != nullptr)
        return store;

    Mel_Render_View_Response_Store new_store = {0};
    new_store.view = view;
    mel_array_init(&new_store.ops, mel_alloc_heap());
    mel_array_push(&s_response_stores, new_store);
    return &mel_array_last(&s_response_stores);
}

static void mel__render_response_destroy_store(Mel_Render_View_Response_Store* store)
{
    assert(store != nullptr);

    for (usize i = 0; i < store->ops.count; i++)
        mel__render_response_slot_clear(&store->ops.items[i]);

    mel_array_free(&store->ops);
}

static usize mel__render_response_live_count(const Mel_Render_View_Response_Store* store)
{
    usize count = 0;
    for (usize i = 0; i < store->ops.count; i++)
    {
        if (store->ops.items[i].live)
            count++;
    }
    return count;
}

static Mel_Render_Response_Op_Slot* mel__render_response_pick_next(const Mel_Render_View_Response_Store* store,
                                                                   bool have_prev,
                                                                   i32 prev_order,
                                                                   u64 prev_sequence)
{
    Mel_Render_Response_Op_Slot* best = nullptr;

    for (usize i = 0; i < store->ops.count; i++)
    {
        Mel_Render_Response_Op_Slot* slot = &store->ops.items[i];
        if (!slot->live)
            continue;

        if (have_prev)
        {
            if (slot->order < prev_order)
                continue;
            if (slot->order == prev_order && slot->sequence <= prev_sequence)
                continue;
        }

        if (best == nullptr ||
            slot->order < best->order ||
            (slot->order == best->order && slot->sequence < best->sequence))
        {
            best = slot;
        }
    }

    return best;
}

static Mel_Gpu_Pipeline* mel__render_response_exposure_pipeline(Mel_Gpu_Format color_format)
{
    Mel_Gpu_Descriptor_Binding binding = {
        .binding = 0,
        .type = MEL_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,
        .count = 1,
        .stages = MEL_GPU_SHADER_STAGE_FRAGMENT,
    };

    return mel_gpu_pipeline_cache_get(s_dev->pipeline_cache, s_dev,
        .shader = &s_exposure_shader,
        .color_format = color_format,
        .blend_mode = MEL_GPU_BLEND_NONE,
        .cull_mode = MEL_GPU_CULL_NONE,
        .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
        .depth_test = false,
        .depth_write = false,
        .push_constant_size = sizeof(Mel_Render_Response_Exposure_Push),
        .push_constant_stages = MEL_GPU_SHADER_STAGE_FRAGMENT,
        .descriptor_bindings = &binding,
        .descriptor_binding_count = 1,
        .max_descriptor_sets = 16);
}

static void mel__render_response_begin_target(Mel_Render_Response_Ctx* ctx)
{
    if (ctx->dst != nullptr)
        mel_gpu_cmd_transition_image(ctx->draw_ctx->cmd, ctx->dst, MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT);

    Mel_Gpu_Color_Attachment color_att = {
        ._image_view = mel_render_target_image_view(ctx->dst_target),
        .layout = MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .load_op = MEL_GPU_LOAD_OP_CLEAR,
        .store_op = MEL_GPU_STORE_OP_STORE,
        .clear_r = 0.0f,
        .clear_g = 0.0f,
        .clear_b = 0.0f,
        .clear_a = 1.0f,
    };

    u32 dst_width = mel_render_target_width(ctx->dst_target);
    u32 dst_height = mel_render_target_height(ctx->dst_target);

    mel_gpu_cmd_begin_rendering(ctx->draw_ctx->cmd,
        .color_attachments = &color_att,
        .color_count = 1,
        .render_width = dst_width,
        .render_height = dst_height);

    mel_gpu_cmd_set_viewport(ctx->draw_ctx->cmd,
        0.0f, 0.0f, (f32)dst_width, (f32)dst_height, 0.0f, 1.0f);
    mel_gpu_cmd_set_scissor(ctx->draw_ctx->cmd, 0, 0, dst_width, dst_height);
}

static void mel__render_response_exposure_run(Mel_Render_Response_Ctx* ctx, const void* params)
{
    assert(ctx != nullptr);
    assert(params != nullptr);
    assert(ctx->draw_ctx != nullptr);
    assert(ctx->src != nullptr);
    assert(ctx->dst_target != nullptr);
    assert(s_response_ready);

    const Mel_Render_Response_Exposure_Manual_Params* exposure = params;
    Mel_Gpu_Pipeline* pipeline =
        mel__render_response_exposure_pipeline(mel_render_target_format(ctx->dst_target));
    assert(pipeline != nullptr);

    mel_gpu_cmd_transition_image(ctx->draw_ctx->cmd, ctx->src, MEL_GPU_IMAGE_LAYOUT_SHADER_READ_ONLY);

    mel__render_response_begin_target(ctx);
    mel_gpu_cmd_bind_pipeline(ctx->draw_ctx->cmd, pipeline);

    void* desc = mel_gpu_pipeline_alloc_descriptor(pipeline, s_dev);
    assert(desc != nullptr);

    mel_gpu_pipeline_write_texture(pipeline, s_dev, desc,
        ctx->src->_view, s_response_sampler);
    mel_gpu_cmd_bind_descriptor_set(ctx->draw_ctx->cmd, pipeline, desc);

    Mel_Render_Response_Exposure_Push push = {
        .multiplier = exp2f(exposure->value),
    };
    mel_gpu_cmd_push_constants(ctx->draw_ctx->cmd, pipeline,
        MEL_GPU_SHADER_STAGE_FRAGMENT, 0, sizeof(push), &push);
    mel_gpu_cmd_draw(ctx->draw_ctx->cmd, 6, 1, 0, 0);
    mel_gpu_cmd_end_rendering(ctx->draw_ctx->cmd);
}

const Mel_Render_Response_Op_Type mel_render_response_exposure_manual = {
    .name = { .data = (u8*)"exposure.manual", .len = 15 },
    .params_size = sizeof(Mel_Render_Response_Exposure_Manual_Params),
    .params_align = _Alignof(Mel_Render_Response_Exposure_Manual_Params),
    .run = mel__render_response_exposure_run,
};

Mel_Render_View_Response_Op_Handle
mel_render_view_response_op_add_impl(Mel_Render_View_Response_Op_Desc desc)
{
    assert(mel_render_view_alive(desc.view));
    assert(desc.type != nullptr);
    assert(desc.params_size == desc.type->params_size);
    assert(desc.type->params_align > 0);
    if (desc.type->params_size > 0)
        assert(desc.params != nullptr);

    Mel_Render_View_Response_Store* store = mel__render_response_get_or_create_store(desc.view);
    assert(store != nullptr);

    usize slot_index = store->ops.count;
    for (usize i = 0; i < store->ops.count; i++)
    {
        if (!store->ops.items[i].live)
        {
            slot_index = i;
            break;
        }
    }

    Mel_Render_Response_Op_Slot slot = {
        .id = s_next_response_op_id++,
        .order = desc.order,
        .sequence = s_next_response_sequence++,
        .type = desc.type,
        .user_data = desc.user_data,
        .live = true,
    };
    if (slot.id == 0)
        slot.id = s_next_response_op_id++;

    if (desc.type->params_size > 0)
    {
        slot.params = mel_aligned_alloc(mel_alloc_heap(), desc.type->params_size, desc.type->params_align);
        memcpy(slot.params, desc.params, desc.type->params_size);
    }

    if (slot_index == store->ops.count)
        mel_array_push(&store->ops, slot);
    else
        store->ops.items[slot_index] = slot;

    return (Mel_Render_View_Response_Op_Handle){ .id = slot.id };
}

void mel_render_view_response_op_clear(Mel_Render_View_Handle view,
                                       const Mel_Render_Response_Op_Type* type)
{
    assert(type != nullptr);

    Mel_Render_View_Response_Store* store = mel__render_response_find_store(view);
    if (store == nullptr)
        return;

    for (usize i = 0; i < store->ops.count; i++)
    {
        Mel_Render_Response_Op_Slot* slot = &store->ops.items[i];
        if (!slot->live || slot->type != type)
            continue;
        mel__render_response_slot_clear(slot);
    }
}

void mel_render_view_response_ops_clear_all(Mel_Render_View_Handle view)
{
    Mel_Render_View_Response_Store* store = mel__render_response_find_store(view);
    if (store == nullptr)
        return;

    for (usize i = 0; i < store->ops.count; i++)
        mel__render_response_slot_clear(&store->ops.items[i]);
}

void mel_render_view_response_op_remove(Mel_Render_View_Response_Op_Handle handle)
{
    if (handle.id == 0)
        return;

    for (usize i = 0; i < s_response_stores.count; i++)
    {
        Mel_Render_View_Response_Store* store = &s_response_stores.items[i];
        for (usize j = 0; j < store->ops.count; j++)
        {
            Mel_Render_Response_Op_Slot* slot = &store->ops.items[j];
            if (!slot->live || slot->id != handle.id)
                continue;
            mel__render_response_slot_clear(slot);
            return;
        }
    }
}

bool mel_render_view_response_op_alive(Mel_Render_View_Response_Op_Handle handle)
{
    if (handle.id == 0)
        return false;

    for (usize i = 0; i < s_response_stores.count; i++)
    {
        Mel_Render_View_Response_Store* store = &s_response_stores.items[i];
        for (usize j = 0; j < store->ops.count; j++)
        {
            Mel_Render_Response_Op_Slot* slot = &store->ops.items[j];
            if (slot->live && slot->id == handle.id)
                return true;
        }
    }

    return false;
}

u32 mel_render_view_response_op_count(Mel_Render_View_Handle view)
{
    Mel_Render_View_Response_Store* store = mel__render_response_find_store(view);
    if (store == nullptr)
        return 0;
    return (u32)mel__render_response_live_count(store);
}

void mel_render_view_response_view_destroy(Mel_Render_View_Handle view)
{
    Mel_Render_View_Response_Store* store = mel__render_response_find_store(view);
    if (store == nullptr)
        return;

    usize index = (usize)(store - s_response_stores.items);
    mel__render_response_destroy_store(store);
    mel_array_remove_unordered(&s_response_stores, index);
}

void mel_render_view_response_execute(Mel_Render_View_Handle view,
                                      Mel_Render_Response_Ctx* base_ctx,
                                      Mel_Render_Target* src_target,
                                      Mel_Render_Target* ping_a,
                                      Mel_Render_Target* ping_b,
                                      Mel_Render_Target* final_target)
{
    assert(base_ctx != nullptr);
    assert(base_ctx->draw_ctx != nullptr);
    assert(src_target != nullptr);
    assert(final_target != nullptr);
    assert(src_target->type == MEL_TARGET_OFFSCREEN);
    assert(s_response_sampler != nullptr);

    Mel_Render_View_Response_Store* store = mel__render_response_find_store(view);
    if (store == nullptr)
        return;

    usize live_count = mel__render_response_live_count(store);
    if (live_count == 0)
        return;

    Mel_Texture_Table* texture_table = mel_texture_pool_get_table();
    assert(texture_table != nullptr);

    Mel_Render_Target* current_src_target = src_target;
    bool have_prev = false;
    i32 prev_order = 0;
    u64 prev_sequence = 0;

    for (usize step = 0; step < live_count; step++)
    {
        Mel_Render_Response_Op_Slot* slot =
            mel__render_response_pick_next(store, have_prev, prev_order, prev_sequence);
        assert(slot != nullptr);

        bool is_last = (step + 1) == live_count;
        Mel_Render_Target* dst_target = final_target;
        if (!is_last)
        {
            dst_target = (step & 1u) == 0u ? ping_a : ping_b;
            assert(dst_target != nullptr);
            assert(dst_target->type == MEL_TARGET_OFFSCREEN);
        }

        u32 src_texture_idx = mel_texture_table_add(texture_table,
            current_src_target->offscreen_image._view, s_response_sampler);
        assert(src_texture_idx != MEL_TEXTURE_TABLE_INVALID_INDEX);

        Mel_Render_Response_Ctx ctx = *base_ctx;
        ctx.src = &current_src_target->offscreen_image;
        ctx.src_texture_idx = src_texture_idx;
        ctx.dst_target = dst_target;
        ctx.dst = dst_target->type == MEL_TARGET_OFFSCREEN ? &dst_target->offscreen_image : nullptr;
        ctx.user_data = slot->user_data;

        slot->type->run(&ctx, slot->params);
        mel_texture_table_remove(texture_table, src_texture_idx);

        current_src_target = dst_target;
        have_prev = true;
        prev_order = slot->order;
        prev_sequence = slot->sequence;
    }
}

void mel__render_view_response_visit(Mel_Render_View_Handle view,
                                     Mel_Render_Response_Visit_Cb visit,
                                     void* ctx)
{
    assert(visit != nullptr);

    Mel_Render_View_Response_Store* store = mel__render_response_find_store(view);
    if (store == nullptr)
        return;

    usize live_count = mel__render_response_live_count(store);
    bool have_prev = false;
    i32 prev_order = 0;
    u64 prev_sequence = 0;

    for (usize step = 0; step < live_count; step++)
    {
        Mel_Render_Response_Op_Slot* slot =
            mel__render_response_pick_next(store, have_prev, prev_order, prev_sequence);
        assert(slot != nullptr);

        visit(slot->type, slot->params, slot->user_data, ctx);

        have_prev = true;
        prev_order = slot->order;
        prev_sequence = slot->sequence;
    }
}

static void mel__render_response_compile(void* data)
{
    (void)data;

    mel_gpu_shader_load(&s_exposure_shader, .path = S8("shaders/response_exposure.slang"), .dev = s_dev);
    s_response_sampler = mel_gpu_sampler_create(s_dev,
        .nearest_filter = false,
        .address_mode_u = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE,
        .address_mode_v = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE,
        .address_mode_w = MEL_GPU_SAMPLER_ADDRESS_CLAMP_TO_EDGE,
        .compare_enable = false,
        .compare_op = MEL_GPU_COMPARE_ALWAYS,
        .min_lod = 0.0f,
        .max_lod = 0.0f);
    s_response_ready = true;
}

static void mel__render_response_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    s_dev = e->dev;
    mel_job_run(e->phase_counter, mel__render_response_compile, e->phase_counter);
}

static void mel__render_response_on_shutdown(void* ctx, const void* event)
{
    (void)ctx;
    (void)event;

    for (usize i = 0; i < s_response_stores.count; i++)
        mel__render_response_destroy_store(&s_response_stores.items[i]);
    mel_array_free(&s_response_stores);

    if (s_response_ready)
    {
        if (s_response_sampler != nullptr)
            mel_gpu_sampler_destroy(s_dev, s_response_sampler);
        mel_gpu_shader_shutdown(&s_exposure_shader, s_dev);
    }

    s_response_sampler = nullptr;
    s_response_ready = false;
    s_dev = nullptr;
}

static void mel__render_response_wire(void)
{
    mel_event_channel_on(&mel_gpu_device_ready, mel__render_response_on_gpu_ready, nullptr);
    mel_event_channel_on(&mel_shutdown_begin, mel__render_response_on_shutdown, nullptr);
}

__attribute__((constructor))
static void mel__render_response_register(void)
{
    mel__boot_register_wire(mel__render_response_wire);
}
