#define VK_NO_PROTOTYPES
#include "render.graph.h"
#include "render.list.h"
#include "render.target.h"
#include "render.camera.fwd.h"
#include "swapchain.h"
#include "gpu.tracy.h"
#include "gpu.image.h"
#include "string.str8.h"
#include "allocator.heap.h"
#include <string.h>
#include <tracy/TracyC.h>

static u32 null_terminated_count(void** arr)
{
    if (!arr) return 0;
    u32 count = 0;
    while (arr[count]) count++;
    return count;
}

static u32 write_target_count(Mel_Pass_Write_Target* arr)
{
    if (!arr) return 0;
    u32 count = 0;
    while (arr[count].target) count++;
    return count;
}

static Mel_Render_List** copy_list_array(const Mel_Alloc* alloc, Mel_Render_List** src)
{
    if (!src) return nullptr;
    u32 count = null_terminated_count((void**)src);
    Mel_Render_List** dst = mel_alloc(alloc, sizeof(Mel_Render_List*) * (count + 1));
    memcpy(dst, src, sizeof(Mel_Render_List*) * (count + 1));
    return dst;
}

static u32 source_handle_count(Mel_Source_Handle* arr)
{
    if (!arr) return 0;
    u32 count = 0;
    while (mel_source_handle_valid(arr[count])) count++;
    return count;
}

static Mel_Source_Handle* copy_source_array(const Mel_Alloc* alloc, Mel_Source_Handle* src)
{
    if (!src) return nullptr;
    u32 count = source_handle_count(src);
    Mel_Source_Handle* dst = mel_alloc(alloc, sizeof(Mel_Source_Handle) * (count + 1));
    memcpy(dst, src, sizeof(Mel_Source_Handle) * (count + 1));
    return dst;
}

static Mel_Render_Target** copy_target_array(const Mel_Alloc* alloc, Mel_Render_Target** src)
{
    if (!src) return nullptr;
    u32 count = null_terminated_count((void**)src);
    Mel_Render_Target** dst = mel_alloc(alloc, sizeof(Mel_Render_Target*) * (count + 1));
    memcpy(dst, src, sizeof(Mel_Render_Target*) * (count + 1));
    return dst;
}

static Mel_Pass_Write_Target* copy_write_target_array(const Mel_Alloc* alloc, Mel_Pass_Write_Target* src)
{
    if (!src) return nullptr;
    u32 count = write_target_count(src);
    Mel_Pass_Write_Target* dst = mel_alloc(alloc, sizeof(Mel_Pass_Write_Target) * (count + 1));
    memcpy(dst, src, sizeof(Mel_Pass_Write_Target) * (count + 1));
    return dst;
}

static void free_pass_resources(const Mel_Alloc* alloc, Mel_Render_Graph_Pass* pass)
{
    if (pass->name.data) mel_dealloc(alloc, pass->name.data);
    if (pass->read_lists) mel_dealloc(alloc, pass->read_lists);
    if (pass->read_sources) mel_dealloc(alloc, pass->read_sources);
    if (pass->write_lists) mel_dealloc(alloc, pass->write_lists);
    if (pass->read_targets) mel_dealloc(alloc, pass->read_targets);
    if (pass->write_targets) mel_dealloc(alloc, pass->write_targets);
}

static void compute_pass_viewport(const Mel_Render_Graph_Pass* pass, u32 target_w, u32 target_h,
    f32* out_x, f32* out_y, f32* out_w, f32* out_h, i32* out_sx, i32* out_sy, u32* out_sw, u32* out_sh)
{
    *out_x = 0.0f;
    *out_y = 0.0f;
    *out_w = (f32)target_w;
    *out_h = (f32)target_h;
    *out_sx = 0;
    *out_sy = 0;
    *out_sw = target_w;
    *out_sh = target_h;

    if (pass->viewport_mode != MEL_PASS_VIEWPORT_FIT ||
        pass->viewport_design_width == 0 ||
        pass->viewport_design_height == 0 ||
        target_w == 0 ||
        target_h == 0)
        return;

    f32 scale_x = (f32)target_w / (f32)pass->viewport_design_width;
    f32 scale_y = (f32)target_h / (f32)pass->viewport_design_height;
    f32 scale = scale_x < scale_y ? scale_x : scale_y;

    u32 fit_w = (u32)((f32)pass->viewport_design_width * scale);
    u32 fit_h = (u32)((f32)pass->viewport_design_height * scale);
    if (fit_w == 0) fit_w = 1;
    if (fit_h == 0) fit_h = 1;

    i32 fit_x = ((i32)target_w - (i32)fit_w) / 2;
    i32 fit_y = ((i32)target_h - (i32)fit_h) / 2;

    *out_x = (f32)fit_x;
    *out_y = (f32)fit_y;
    *out_w = (f32)fit_w;
    *out_h = (f32)fit_h;
    *out_sx = fit_x;
    *out_sy = fit_y;
    *out_sw = fit_w;
    *out_sh = fit_h;
}

static bool has_write_target(Mel_Pass_Write_Target* arr, Mel_Render_Target* t)
{
    if (!arr) return false;
    while (arr->target)
    {
        if (arr->target == t) return true;
        arr++;
    }
    return false;
}

static bool has_list(Mel_Render_List** arr, Mel_Render_List* l)
{
    if (!arr) return false;
    while (*arr)
    {
        if (*arr == l) return true;
        arr++;
    }
    return false;
}

typedef struct {
    Mel_Render_Target* target;
    VkImageLayout layout;
} Target_State;

typedef struct {
    Mel_Render_List* list;
    bool has_writer;
    u32 writer_pass_type;
} List_State;

static VkImageLayout find_target_layout(Target_State* states, usize count, Mel_Render_Target* t)
{
    for (usize i = 0; i < count; i++)
        if (states[i].target == t)
            return states[i].layout;
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

static List_State* find_list_state(List_State* states, usize count, Mel_Render_List* l)
{
    for (usize i = 0; i < count; i++)
        if (states[i].list == l)
            return &states[i];
    return nullptr;
}

static void set_target_layout(Target_State* states, usize* count, usize capacity,
                               Mel_Render_Target* t, VkImageLayout layout)
{
    for (usize i = 0; i < *count; i++)
    {
        if (states[i].target == t)
        {
            states[i].layout = layout;
            return;
        }
    }
    assert(*count < capacity);
    states[*count] = (Target_State){t, layout};
    (*count)++;
}

static List_State* ensure_list_state(List_State* states, usize* count, usize capacity, Mel_Render_List* l)
{
    List_State* existing = find_list_state(states, *count, l);
    if (existing) return existing;
    assert(*count < capacity);
    states[*count] = (List_State){ .list = l };
    return &states[(*count)++];
}

static VkPipelineStageFlags2 list_stage_for_pass(u32 pass_type)
{
    return pass_type == MEL_PASS_COMPUTE
        ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
        : VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
}

static void compute_barriers(Mel_Render_Graph* g)
{
    mel_array_clear(&g->barriers);

    usize max_targets = 0;
    usize max_lists = 0;
    for (usize i = 0; i < g->passes.count; i++)
    {
        Mel_Render_Graph_Pass* p = &g->passes.items[i];
        if (p->read_targets)
            for (Mel_Render_Target** t = p->read_targets; *t; t++) max_targets++;
        if (p->write_targets)
            for (Mel_Pass_Write_Target* wt = p->write_targets; wt->target; wt++) max_targets++;
        if (p->read_lists)
            for (Mel_Render_List** l = p->read_lists; *l; l++) max_lists++;
        if (p->write_lists)
            for (Mel_Render_List** l = p->write_lists; *l; l++) max_lists++;
    }

    if (max_targets == 0 && max_lists == 0) return;

    Target_State* target_states = max_targets > 0
        ? mel_alloc(g->alloc, sizeof(Target_State) * max_targets)
        : nullptr;
    usize target_state_count = 0;
    List_State* list_states = max_lists > 0
        ? mel_alloc(g->alloc, sizeof(List_State) * max_lists)
        : nullptr;
    usize list_state_count = 0;

    for (usize si = 0; si < g->sorted_order.count; si++)
    {
        u32 pi = g->sorted_order.items[si];
        Mel_Render_Graph_Pass* pass = &g->passes.items[pi];

        if (pass->read_targets)
        {
            for (Mel_Render_Target** t = pass->read_targets; *t; t++)
            {
                VkImageLayout cur = find_target_layout(target_states, target_state_count, *t);
                if (cur != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                {
                    bool depth = ((*t)->kind == MEL_RENDER_TARGET_DEPTH);
                    VkPipelineStageFlags2 dst_stage = pass->type == MEL_PASS_COMPUTE
                        ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                        : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    Mel_Render_Graph_Barrier b = {
                        .pass_index = pi,
                        .target = *t,
                        .list = nullptr,
                        .src_stage = mel_gpu_image_layout_stage(cur),
                        .src_access = mel_gpu_image_layout_access(cur),
                        .dst_stage = dst_stage,
                        .dst_access = VK_ACCESS_2_SHADER_READ_BIT,
                        .old_layout = cur,
                        .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        .aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                    };
                    mel_array_push(&g->barriers, b);
                    set_target_layout(target_states, &target_state_count, max_targets, *t,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                }
            }
        }

        if (pass->write_targets)
        {
            for (Mel_Pass_Write_Target* wt = pass->write_targets; wt->target; wt++)
            {
                Mel_Render_Target* t = wt->target;
                VkImageLayout cur = find_target_layout(target_states, target_state_count, t);
                bool depth = (t->kind == MEL_RENDER_TARGET_DEPTH);
                VkImageLayout target_layout = depth
                    ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                    : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                if (cur != target_layout)
                {
                    Mel_Render_Graph_Barrier b = {
                        .pass_index = pi,
                        .target = t,
                        .list = nullptr,
                        .src_stage = mel_gpu_image_layout_stage(cur),
                        .src_access = mel_gpu_image_layout_access(cur),
                        .dst_stage = mel_gpu_image_layout_stage(target_layout),
                        .dst_access = mel_gpu_image_layout_access(target_layout),
                        .old_layout = cur,
                        .new_layout = target_layout,
                        .aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                    };
                    mel_array_push(&g->barriers, b);
                    set_target_layout(target_states, &target_state_count, max_targets, t, target_layout);
                }
            }
        }

        if (pass->read_lists && list_states)
        {
            for (Mel_Render_List** l = pass->read_lists; *l; l++)
            {
                List_State* st = find_list_state(list_states, list_state_count, *l);
                if (st && st->has_writer)
                {
                    Mel_Render_Graph_Barrier b = {
                        .pass_index = pi,
                        .target = nullptr,
                        .list = *l,
                        .src_stage = list_stage_for_pass(st->writer_pass_type),
                        .src_access = VK_ACCESS_2_SHADER_WRITE_BIT,
                        .dst_stage = list_stage_for_pass(pass->type),
                        .dst_access = VK_ACCESS_2_SHADER_READ_BIT,
                    };
                    mel_array_push(&g->barriers, b);
                }
            }
        }

        if (pass->write_lists && list_states)
        {
            for (Mel_Render_List** l = pass->write_lists; *l; l++)
            {
                List_State* st = ensure_list_state(list_states, &list_state_count, max_lists, *l);

                if (st->has_writer)
                {
                    Mel_Render_Graph_Barrier b = {
                        .pass_index = pi,
                        .target = nullptr,
                        .list = *l,
                        .src_stage = list_stage_for_pass(st->writer_pass_type),
                        .src_access = VK_ACCESS_2_SHADER_WRITE_BIT,
                        .dst_stage = list_stage_for_pass(pass->type),
                        .dst_access = VK_ACCESS_2_SHADER_WRITE_BIT,
                    };
                    mel_array_push(&g->barriers, b);
                }

                st->has_writer = true;
                st->writer_pass_type = pass->type;
            }
        }
    }

    if (target_states)
        mel_dealloc(g->alloc, target_states);
    if (list_states)
        mel_dealloc(g->alloc, list_states);
}

typedef struct {
    Mel_Swapchain* items[MEL_MAX_SWAPCHAINS];
    bool acquired[MEL_MAX_SWAPCHAINS];
    u32 count;
} Mel_Swapchain_Set;

static void collect_swapchain_targets(Mel_Render_Graph* g, Mel_Swapchain_Set* set)
{
    set->count = 0;
    for (usize i = 0; i < g->passes.count; i++)
    {
        Mel_Render_Graph_Pass* p = &g->passes.items[i];
        if (!p->write_targets) continue;
        for (Mel_Pass_Write_Target* wt = p->write_targets; wt->target; wt++)
        {
            if (wt->target->kind != MEL_RENDER_TARGET_SWAPCHAIN)
                continue;
            Mel_Swapchain* sc = wt->target->swapchain;
            bool found = false;
            for (u32 j = 0; j < set->count; j++)
            {
                if (set->items[j] == sc) { found = true; break; }
            }
            if (!found)
            {
                assert(set->count < MEL_MAX_SWAPCHAINS);
                set->items[set->count] = sc;
                set->acquired[set->count] = false;
                set->count++;
            }
        }
    }
}

static void execute_passes(Mel_Render_Graph* g, Mel_Gpu_Cmd* cmd)
{
    usize barrier_cursor = 0;

    for (usize si = 0; si < g->sorted_order.count; si++)
    {
        u32 pi = g->sorted_order.items[si];
        Mel_Render_Graph_Pass* pass = &g->passes.items[pi];
        Mel_Gpu_Tracy_Zone gpu_zone = mel_gpu_tracy_zone_begin(g->tracy_ctx, cmd->cmd, pass->name);

        while (barrier_cursor < g->barriers.count &&
               g->barriers.items[barrier_cursor].pass_index == pi)
        {
            Mel_Render_Graph_Barrier* b = &g->barriers.items[barrier_cursor++];
            if (b->target)
            {
                VkImageLayout old_layout = b->old_layout;
                VkPipelineStageFlags2 src_stage = b->src_stage;
                VkAccessFlags2 src_access = b->src_access;

                if (b->target->kind == MEL_RENDER_TARGET_SWAPCHAIN)
                {
                    old_layout = mel_swapchain_current_image_layout(b->target->swapchain);
                    src_stage = mel_gpu_image_layout_stage(old_layout);
                    src_access = mel_gpu_image_layout_access(old_layout);
                }

                mel_gpu_cmd_image_barrier(cmd,
                    mel_render_target_image(b->target),
                    src_stage, src_access,
                    b->dst_stage, b->dst_access,
                    old_layout, b->new_layout,
                    b->aspect);
            }
            else if (b->list)
            {
                VkBuffer buffer = b->list->gpu_buffer.buffer;
                if (buffer != VK_NULL_HANDLE)
                    mel_gpu_cmd_buffer_barrier(cmd, buffer,
                        b->src_stage, b->src_access,
                        b->dst_stage, b->dst_access);
            }
        }

        bool has_wt = pass->write_targets && pass->write_targets[0].target;

        if (pass->type == MEL_PASS_GRAPHICS && has_wt)
        {
            u32 color_count = 0;
            Mel_Gpu_Color_Attachment color_attachments[8];
            Mel_Gpu_Depth_Attachment depth_att = {0};
            bool has_depth = false;

            for (Mel_Pass_Write_Target* wt = pass->write_targets; wt->target; wt++)
            {
                if (wt->target->kind == MEL_RENDER_TARGET_DEPTH)
                {
                    depth_att = (Mel_Gpu_Depth_Attachment){
                        .image_view = mel_render_target_view(wt->target),
                        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        .load_op = wt->load_op,
                        .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                        .clear_depth = wt->clear.depth.depth,
                        .clear_stencil = wt->clear.depth.stencil,
                    };
                    has_depth = true;
                    continue;
                }

                assert(color_count < 8);
                color_attachments[color_count++] = (Mel_Gpu_Color_Attachment){
                    .image_view = mel_render_target_view(wt->target),
                    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .load_op = wt->load_op,
                    .store_op = VK_ATTACHMENT_STORE_OP_STORE,
                    .clear_r = wt->clear.color.r,
                    .clear_g = wt->clear.color.g,
                    .clear_b = wt->clear.color.b,
                    .clear_a = wt->clear.color.a,
                };
            }

            u32 rw = mel_render_target_width(pass->write_targets[0].target);
            u32 rh = mel_render_target_height(pass->write_targets[0].target);
            f32 viewport_x = 0.0f, viewport_y = 0.0f, viewport_w = (f32)rw, viewport_h = (f32)rh;
            i32 scissor_x = 0, scissor_y = 0;
            u32 scissor_w = rw, scissor_h = rh;

            compute_pass_viewport(pass, rw, rh,
                &viewport_x, &viewport_y, &viewport_w, &viewport_h,
                &scissor_x, &scissor_y, &scissor_w, &scissor_h);

            mel_gpu_cmd_begin_rendering(cmd,
                .color_attachments = color_count > 0 ? color_attachments : nullptr,
                .color_count = color_count,
                .depth_attachment = has_depth ? &depth_att : nullptr,
                .render_width = rw,
                .render_height = rh,
            );

            mel_gpu_cmd_set_viewport(cmd, viewport_x, viewport_y, viewport_w, viewport_h, 0.0f, 1.0f);
            mel_gpu_cmd_set_scissor(cmd, scissor_x, scissor_y, scissor_w, scissor_h);
        }

        if (pass->fn)
        {
            u32 rw = 0, rh = 0;
            Mel_Render_Target* write_target_ptrs[9] = {0};

            if (has_wt)
            {
                rw = mel_render_target_width(pass->write_targets[0].target);
                rh = mel_render_target_height(pass->write_targets[0].target);

                u32 wt_count = 0;
                for (Mel_Pass_Write_Target* wt = pass->write_targets; wt->target; wt++)
                    write_target_ptrs[wt_count++] = wt->target;
            }

            Mel_Render_Pass_Ctx ctx = {
                .cmd = *cmd,
                .read_lists = pass->read_lists,
                .read_sources = pass->read_sources,
                .write_lists = pass->write_lists,
                .read_targets = pass->read_targets,
                .write_targets = has_wt ? write_target_ptrs : nullptr,
                .camera = pass->camera,
                .user = pass->user,
                .render_width = rw,
                .render_height = rh,
                .gpu_frame_index = g->current_frame,
            };
            pass->fn(&ctx);
        }

        if (pass->type == MEL_PASS_GRAPHICS && has_wt)
            mel_gpu_cmd_end_rendering(cmd);

        mel_gpu_tracy_zone_end(gpu_zone);
    }
}

void mel_render_graph_init_opt(Mel_Render_Graph* g, Mel_Render_Graph_Opt opt)
{
    assert(g != nullptr);

    *g = (Mel_Render_Graph){0};
    g->dev = opt.dev;
    g->alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    mel_array_init(&g->passes, g->alloc);
    mel_array_init(&g->sorted_order, g->alloc);
    mel_array_init(&g->barriers, g->alloc);
    mel_array_init(&g->explicit_deps, g->alloc);

    if (g->dev)
    {
        g->frame_count = opt.frame_count > 0 ? opt.frame_count : 2;
        assert(g->frame_count <= MEL_MAX_FRAMES_IN_FLIGHT);

        for (u32 i = 0; i < g->frame_count; i++)
        {
            VkCommandPoolCreateInfo pool_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                .queueFamilyIndex = g->dev->graphics_family,
            };
            vkCreateCommandPool(g->dev->device, &pool_info, nullptr, &g->frames[i].pool);

            VkCommandBufferAllocateInfo alloc_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = g->frames[i].pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            vkAllocateCommandBuffers(g->dev->device, &alloc_info, &g->frames[i].cmd);

            VkFenceCreateInfo fence_info = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT,
            };
            vkCreateFence(g->dev->device, &fence_info, nullptr, &g->frames[i].fence);
        }
    }
}

void mel_render_graph_shutdown(Mel_Render_Graph* g)
{
    assert(g != nullptr);

    if (g->dev)
    {
        vkDeviceWaitIdle(g->dev->device);

        for (u32 i = 0; i < g->frame_count; i++)
        {
            if (g->frames[i].fence)
                vkDestroyFence(g->dev->device, g->frames[i].fence, nullptr);
            if (g->frames[i].pool)
                vkDestroyCommandPool(g->dev->device, g->frames[i].pool, nullptr);
        }
    }

    mel_gpu_tracy_shutdown(g->tracy_ctx);
    g->tracy_ctx = nullptr;

    for (usize i = 0; i < g->passes.count; i++)
        free_pass_resources(g->alloc, &g->passes.items[i]);

    mel_array_free(&g->passes);
    mel_array_free(&g->sorted_order);
    mel_array_free(&g->barriers);
    mel_array_free(&g->explicit_deps);

    *g = (Mel_Render_Graph){0};
}

u32 mel_render_graph_add_pass_opt(Mel_Render_Graph* g, str8 name, Mel_Pass_Desc desc)
{
    assert(g != nullptr);

    Mel_Render_Graph_Pass pass = {
        .name = str8_dup(name, g->alloc),
        .fn = desc.fn,
        .user = desc.user,
        .type = desc.type,
        .viewport_mode = desc.viewport_mode,
        .viewport_design_width = desc.viewport_design_width,
        .viewport_design_height = desc.viewport_design_height,
        .read_lists = copy_list_array(g->alloc, desc.read_lists),
        .read_sources = copy_source_array(g->alloc, desc.read_sources),
        .write_lists = copy_list_array(g->alloc, desc.write_lists),
        .read_targets = copy_target_array(g->alloc, desc.read_targets),
        .write_targets = copy_write_target_array(g->alloc, desc.write_targets),
        .camera = desc.camera,
    };

    u32 id = (u32)g->passes.count;
    mel_array_push(&g->passes, pass);
    g->dirty = true;

    return id;
}

void mel_render_graph_remove_pass(Mel_Render_Graph* g, str8 name)
{
    assert(g != nullptr);

    for (usize i = 0; i < g->passes.count; i++)
    {
        if (str8_equals(g->passes.items[i].name, name))
        {
            free_pass_resources(g->alloc, &g->passes.items[i]);
            mel_array_remove_ordered(&g->passes, i);

            u32 removed = (u32)i;
            for (usize d = g->explicit_deps.count; d > 0; d--)
            {
                Mel_Render_Graph_Dep* dep = &g->explicit_deps.items[d - 1];
                if (dep->from == removed || dep->to == removed)
                {
                    mel_array_remove_ordered(&g->explicit_deps, d - 1);
                    continue;
                }
                if (dep->from > removed) dep->from--;
                if (dep->to > removed) dep->to--;
            }

            g->dirty = true;
            return;
        }
    }

    assert(false);
}

void mel_render_graph_pass_depends_on(Mel_Render_Graph* g, u32 pass_id, u32 dependency_id)
{
    assert(g != nullptr);
    assert(pass_id < (u32)g->passes.count);
    assert(dependency_id < (u32)g->passes.count);
    assert(pass_id != dependency_id);

    mel_array_push(&g->explicit_deps, ((Mel_Render_Graph_Dep){.from = dependency_id, .to = pass_id}));
    g->dirty = true;
}

bool mel_render_graph_compile(Mel_Render_Graph* g)
{
    assert(g != nullptr);

    usize n = g->passes.count;
    mel_array_clear(&g->sorted_order);
    mel_array_clear(&g->barriers);

    if (n == 0)
    {
        g->dirty = false;
        return true;
    }

    bool* adj = mel_alloc(g->alloc, n * n * sizeof(bool));
    memset(adj, 0, n * n * sizeof(bool));

    for (usize d = 0; d < g->explicit_deps.count; d++)
    {
        u32 from = g->explicit_deps.items[d].from;
        u32 to = g->explicit_deps.items[d].to;
        adj[to * n + from] = true;
    }

    for (usize i = 0; i < n; i++)
    {
        Mel_Render_Graph_Pass* pi = &g->passes.items[i];

        if (pi->read_targets)
        {
            for (Mel_Render_Target** t = pi->read_targets; *t; t++)
            {
                for (usize j = 0; j < n; j++)
                {
                    if (i == j) continue;
                    if (has_write_target(g->passes.items[j].write_targets, *t))
                        adj[i * n + j] = true;
                }
            }
        }

        if (pi->write_targets)
        {
            for (Mel_Pass_Write_Target* wt = pi->write_targets; wt->target; wt++)
            {
                for (usize j = 0; j < i; j++)
                {
                    if (has_write_target(g->passes.items[j].write_targets, wt->target))
                        adj[i * n + j] = true;
                }
            }
        }

        if (pi->read_lists)
        {
            for (Mel_Render_List** l = pi->read_lists; *l; l++)
            {
                for (usize j = 0; j < n; j++)
                {
                    if (i == j) continue;
                    if (has_list(g->passes.items[j].write_lists, *l))
                        adj[i * n + j] = true;
                }
            }
        }

        if (pi->write_lists)
        {
            for (Mel_Render_List** l = pi->write_lists; *l; l++)
            {
                for (usize j = 0; j < i; j++)
                {
                    if (has_list(g->passes.items[j].write_lists, *l))
                        adj[i * n + j] = true;
                }
            }
        }
    }

    u32* in_degree = mel_alloc(g->alloc, n * sizeof(u32));
    memset(in_degree, 0, n * sizeof(u32));

    for (usize i = 0; i < n; i++)
        for (usize j = 0; j < n; j++)
            if (adj[i * n + j])
                in_degree[i]++;

    u32* queue = mel_alloc(g->alloc, n * sizeof(u32));
    u32 qfront = 0, qback = 0;

    for (usize i = 0; i < n; i++)
        if (in_degree[i] == 0)
            queue[qback++] = (u32)i;

    while (qfront < qback)
    {
        u32 cur = queue[qfront++];
        mel_array_push(&g->sorted_order, cur);

        for (usize i = 0; i < n; i++)
        {
            if (adj[i * n + cur])
            {
                in_degree[i]--;
                if (in_degree[i] == 0)
                    queue[qback++] = (u32)i;
            }
        }
    }

    bool success = (g->sorted_order.count == n);

    mel_dealloc(g->alloc, adj);
    mel_dealloc(g->alloc, in_degree);
    mel_dealloc(g->alloc, queue);

    if (success)
    {
        compute_barriers(g);
    }
    else
    {
        mel_array_clear(&g->sorted_order);
        mel_array_clear(&g->barriers);
    }

    g->dirty = !success;
    return success;
}

static void prepare_lists(Mel_Render_Graph* g)
{
    u64 frame_id = g->execute_count;

    for (usize i = 0; i < g->passes.count; i++)
    {
        Mel_Render_Graph_Pass* pass = &g->passes.items[i];

        if (pass->read_lists)
            for (Mel_Render_List** l = pass->read_lists; *l; l++)
                mel_render_list_begin_frame(*l, frame_id);

        if (pass->write_lists)
            for (Mel_Render_List** l = pass->write_lists; *l; l++)
                mel_render_list_begin_frame(*l, frame_id);
    }
}

static void produce_lists(Mel_Render_Graph* g)
{
    for (usize i = 0; i < g->passes.count; i++)
    {
        Mel_Render_Graph_Pass* pass = &g->passes.items[i];

        if (pass->read_lists)
            for (Mel_Render_List** l = pass->read_lists; *l; l++)
                mel_render_list_produce(*l);

        if (pass->write_lists)
            for (Mel_Render_List** l = pass->write_lists; *l; l++)
                mel_render_list_produce(*l);
    }
}

bool mel_render_graph_execute(Mel_Render_Graph* g)
{
    assert(g != nullptr);

    if (g->dirty && !mel_render_graph_compile(g))
    {
        assert(false);
        return false;
    }

    if (g->sorted_order.count == 0)
        return true;

    prepare_lists(g);
    produce_lists(g);

    bool has_gpu = (g->dev != nullptr && g->frame_count > 0);
    Mel_Swapchain_Set swapchains = {0};
    Mel_Gpu_Cmd cmd = {0};

    if (has_gpu)
    {
        collect_swapchain_targets(g, &swapchains);

        Mel_Render_Graph_Frame* fd = &g->frames[g->current_frame];

        TracyCZoneN(ctx_fence_wait, "render_graph_fence_wait", true);
        vkWaitForFences(g->dev->device, 1, &fd->fence, VK_TRUE, UINT64_MAX);
        TracyCZoneEnd(ctx_fence_wait);

        for (u32 i = 0; i < swapchains.count; i++)
            swapchains.acquired[i] = mel_swapchain_acquire(swapchains.items[i], g->dev);

        vkResetFences(g->dev->device, 1, &fd->fence);
        vkResetCommandPool(g->dev->device, fd->pool, 0);

        if (!g->tracy_ctx)
        {
            bool tracy_ok = mel_gpu_tracy_init(&g->tracy_ctx, g->dev, g->dev->graphics_queue, fd->cmd, S8("render_graph"));
            assert(tracy_ok);
            MEL_UNUSED(tracy_ok);
            vkResetCommandPool(g->dev->device, fd->pool, 0);
        }

        VkCommandBufferBeginInfo begin = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        TracyCZoneN(ctx_cmd_begin, "render_graph_begin_cmd", true);
        vkBeginCommandBuffer(fd->cmd, &begin);
        TracyCZoneEnd(ctx_cmd_begin);

        cmd = (Mel_Gpu_Cmd){ .cmd = fd->cmd, .dev = g->dev };
    }

    TracyCZoneN(ctx_execute_passes, "render_graph_execute_passes", true);
    execute_passes(g, &cmd);
    TracyCZoneEnd(ctx_execute_passes);

    if (has_gpu)
    {
        Mel_Render_Graph_Frame* fd = &g->frames[g->current_frame];
        mel_gpu_tracy_collect(g->tracy_ctx, fd->cmd);

        for (u32 i = 0; i < swapchains.count; i++)
        {
            if (swapchains.acquired[i])
                mel_swapchain_prepare_present(swapchains.items[i], fd->cmd);
        }

        VkResult end_r = vkEndCommandBuffer(fd->cmd);
        assert(end_r == VK_SUCCESS);

        Mel_Gpu_Submit_Gather gather = {0};
        for (u32 i = 0; i < swapchains.count; i++)
        {
            if (swapchains.acquired[i])
                mel_swapchain_collect_sync(swapchains.items[i], &gather);
        }

        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = gather.wait_count,
            .pWaitSemaphores = gather.wait_count > 0 ? gather.wait_semaphores : nullptr,
            .pWaitDstStageMask = gather.wait_count > 0 ? gather.wait_stages : nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &fd->cmd,
            .signalSemaphoreCount = gather.signal_count,
            .pSignalSemaphores = gather.signal_count > 0 ? gather.signal_semaphores : nullptr,
        };

        VkResult r = vkQueueSubmit(g->dev->graphics_queue, 1, &submit_info, fd->fence);
        assert(r == VK_SUCCESS);
        MEL_UNUSED(r);

        for (u32 i = 0; i < swapchains.count; i++)
        {
            if (swapchains.acquired[i])
                mel_swapchain_present(swapchains.items[i], g->dev);
        }

        g->current_frame = (g->current_frame + 1) % g->frame_count;
    }

    g->execute_count++;
    return true;
}
