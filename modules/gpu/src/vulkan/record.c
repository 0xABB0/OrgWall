#include "vk_backend.h"

#include <log/log.h>

// ---- U15: standalone command lists ----

Mel_Gpu_Command_List* mel_gpu_command_list_create(Mel_Gpu_Queue* q)
{
    if (!q)
        return NULL;
    Mel_Gpu_Device* dev = q->dev;
    VkCommandPool   pool = mel_gpu__thread_pool(dev, q->family);

    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(dev->vk, &cai, &cb) != VK_SUCCESS)
    {
        mel_log_error("gpu", "command_list_create: vkAllocateCommandBuffers failed");
        return NULL;
    }

    Mel_Gpu_Command_List* cmd = mel_alloc_type(dev->alloc, Mel_Gpu_Command_List);
    *cmd = (Mel_Gpu_Command_List){ .dev = dev, .cb = cb, .owner_pool = pool, .standalone = true };
    return cmd;
}

void mel_gpu_command_list_begin(Mel_Gpu_Command_List* cmd)
{
    mel_assert(cmd && cmd->standalone);
    // §3.7 / BUG-2: every cmd_* is SerializedPerObject on the command list itself; the canonical pattern is one
    // CL per recording thread (U15). Bracket the recording window by registering the owning thread on the CL
    // pointer at begin and releasing at end — a second thread that begins/records the same CL is reported.
    mel_gpu__track_enter(cmd->dev, cmd, MEL_GPU_CONCURRENCY_SERIALIZED_PER_OBJECT);
    vkResetCommandBuffer(cmd->cb, 0);
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    vkBeginCommandBuffer(cmd->cb, &bi);
    cmd->cur_layout = VK_NULL_HANDLE;
    cmd->state_count = 0; // U17 state tracking is per-recording (gpu-rhi.md §7.3)
    cmd->recording = true;
}

void mel_gpu_command_list_end(Mel_Gpu_Command_List* cmd)
{
    mel_assert(cmd && cmd->recording);
    vkEndCommandBuffer(cmd->cb);
    cmd->recording = false;
    mel_gpu__track_exit(cmd->dev, cmd); // §3.7: release the per-CL recording ownership registered at begin
}

void mel_gpu_command_list_destroy(Mel_Gpu_Command_List* cmd)
{
    if (!cmd)
        return;
    mel_assert(cmd->standalone);
    if (cmd->cb)
        vkFreeCommandBuffers(cmd->dev->vk, cmd->owner_pool, 1, &cmd->cb);
    if (cmd->states)
        mel_dealloc(cmd->dev->alloc, cmd->states);
    mel_dealloc(cmd->dev->alloc, cmd);
}

// U17 state tracking: validate the declared source state against what this command list last recorded for
// each subresource, then record the destination. First touch accepts the declared source (the resource's
// external/initial state). A mismatch is a missing or wrong barrier — the most common porting bug (§7.3).
static void mel_gpu__track_state(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, u32 mip, u32 layer, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    for (u32 i = 0; i < cmd->state_count; i++)
    {
        Mel_Gpu_Cmd_State_Entry* e = &cmd->states[i];
        if (e->tex_index == tex.slot.index && e->tex_generation == tex.slot.generation && e->mip == mip && e->layer == layer)
        {
            if (e->state != src)
            {
                mel_log_error("gpu", "cmd_barrier: state mismatch on subresource (mip=%u, layer=%u): tracked=%d, declared src=%d", mip, layer, (int)e->state, (int)src);
                mel_assert(!"cmd_barrier: declared source state does not match the command list's tracked state");
            }
            e->state = dst;
            return;
        }
    }
    if (cmd->state_count == cmd->state_cap)
    {
        u32 cap = cmd->state_cap ? cmd->state_cap * 2 : 16;
        cmd->states = cmd->states ? mel_realloc(cmd->dev->alloc, cmd->states, sizeof(Mel_Gpu_Cmd_State_Entry) * cap) : mel_alloc(cmd->dev->alloc, sizeof(Mel_Gpu_Cmd_State_Entry) * cap);
        cmd->state_cap = cap;
    }
    cmd->states[cmd->state_count++] = (Mel_Gpu_Cmd_State_Entry){ .tex_index = tex.slot.index, .tex_generation = tex.slot.generation, .mip = mip, .layer = layer, .state = dst };
}

// ---- U17: state -> (stage, access, layout) ----

void mel_gpu__state_to_barrier(Mel_Gpu_Resource_State state, bool is_depth, VkPipelineStageFlags* stage, VkAccessFlags* access, VkImageLayout* layout)
{
    (void)is_depth;
    switch (state)
    {
    case MEL_GPU_STATE_COMMON:
        *stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        *access = 0;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    case MEL_GPU_STATE_VERTEX_BUFFER:
        *stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        *access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    case MEL_GPU_STATE_INDEX_BUFFER:
        *stage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        *access = VK_ACCESS_INDEX_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    case MEL_GPU_STATE_CONSTANT_BUFFER:
        *stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        *access = VK_ACCESS_UNIFORM_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    case MEL_GPU_STATE_INDIRECT_ARGUMENT:
        *stage = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
        *access = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    case MEL_GPU_STATE_SHADER_RESOURCE:
        *stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        *access = VK_ACCESS_SHADER_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return;
    case MEL_GPU_STATE_UNORDERED_ACCESS:
        *stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        *access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    case MEL_GPU_STATE_RENDER_TARGET:
        *stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        *access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        *layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        return;
    case MEL_GPU_STATE_DEPTH_WRITE:
        *stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        *access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        *layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        return;
    case MEL_GPU_STATE_DEPTH_READ:
        *stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        *access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        return;
    case MEL_GPU_STATE_COPY_SOURCE:
    case MEL_GPU_STATE_RESOLVE_SOURCE:
        *stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        *access = VK_ACCESS_TRANSFER_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        return;
    case MEL_GPU_STATE_COPY_DEST:
    case MEL_GPU_STATE_RESOLVE_DEST:
        *stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        *access = VK_ACCESS_TRANSFER_WRITE_BIT;
        *layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        return;
    case MEL_GPU_STATE_PRESENT:
        *stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        *access = 0;
        *layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        return;
    case MEL_GPU_STATE_SHADING_RATE_SOURCE:
        *stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        *access = VK_ACCESS_MEMORY_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    default:
        // Conservative fallback for states whose Vulkan lowering is a later M2/M3 slice (gpu-rhi.md §7.3).
        mel_log_warn("gpu", "cmd_barrier: state %d not yet lowered; using GENERAL/all-access", (int)state);
        *stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        *access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    }
}

// U17 synchronization2 peer (gpu-rhi.md §7.3). Same state→layout mapping as the legacy lowering, but the
// stage/access masks use the 64-bit pipeline_stage_2 / access_2 enums. Where granted these are the engine's
// primary lowering; the legacy path is the floor. The layouts are identical, so cross-path behaviour matches.
void mel_gpu__state_to_barrier2(Mel_Gpu_Resource_State state, bool is_depth, VkPipelineStageFlags2* stage, VkAccessFlags2* access, VkImageLayout* layout)
{
    (void)is_depth;
    switch (state)
    {
    case MEL_GPU_STATE_COMMON:
        *stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        *access = 0;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    case MEL_GPU_STATE_VERTEX_BUFFER:
        *stage = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        *access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    case MEL_GPU_STATE_INDEX_BUFFER:
        *stage = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        *access = VK_ACCESS_2_INDEX_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    case MEL_GPU_STATE_CONSTANT_BUFFER:
        *stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        *access = VK_ACCESS_2_UNIFORM_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    case MEL_GPU_STATE_INDIRECT_ARGUMENT:
        *stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        *access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    case MEL_GPU_STATE_SHADER_RESOURCE:
        *stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        *access = VK_ACCESS_2_SHADER_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return;
    case MEL_GPU_STATE_UNORDERED_ACCESS:
        *stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        *access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    case MEL_GPU_STATE_RENDER_TARGET:
        *stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        *access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        *layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        return;
    case MEL_GPU_STATE_DEPTH_WRITE:
        *stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        *access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        *layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        return;
    case MEL_GPU_STATE_DEPTH_READ:
        *stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        *access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        return;
    case MEL_GPU_STATE_COPY_SOURCE:
    case MEL_GPU_STATE_RESOLVE_SOURCE:
        *stage = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
        *access = VK_ACCESS_2_TRANSFER_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        return;
    case MEL_GPU_STATE_COPY_DEST:
    case MEL_GPU_STATE_RESOLVE_DEST:
        *stage = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT;
        *access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        *layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        return;
    case MEL_GPU_STATE_PRESENT:
        *stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        *access = 0;
        *layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        return;
    case MEL_GPU_STATE_SHADING_RATE_SOURCE:
        *stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        *access = VK_ACCESS_2_MEMORY_READ_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    default:
        mel_log_warn("gpu", "cmd_barrier: state %d not yet lowered (sync2); using GENERAL/all-access", (int)state);
        *stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        *access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        *layout = VK_IMAGE_LAYOUT_GENERAL;
        return;
    }
}

void mel_gpu_cmd_texture_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Subresource_Range range, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    Mel_Gpu_Texture_Obj  o_obj; // BUG-1: snapshot the immutable texture record under obj_lock
    Mel_Gpu_Texture_Obj* o = &o_obj;
    if (!cmd || !mel_gpu__texture_get(cmd->dev, tex, o))
    {
        mel_assert(!"cmd_texture_barrier: invalid texture handle");
        return;
    }

    bool is_depth = (o->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;

    // State tracking is path-independent (gpu-rhi.md §7.3): validate the declared source and record the
    // destination per subresource, then lower onto sync2 where granted, legacy otherwise.
    u32 mip_n = range.mip_count ? range.mip_count : (o->mip_levels - range.base_mip);
    u32 layer_n = range.layer_count ? range.layer_count : (o->array_layers - range.base_layer);
    for (u32 m = 0; m < mip_n; m++)
        for (u32 l = 0; l < layer_n; l++)
            mel_gpu__track_state(cmd, tex, range.base_mip + m, range.base_layer + l, src, dst);

    VkImageAspectFlags        aspect = mel_gpu__aspect_flags(range.aspect, o->format);
    VkImageSubresourceRange   sub = {
        .aspectMask = aspect,
        .baseMipLevel = range.base_mip,
        .levelCount = range.mip_count ? range.mip_count : VK_REMAINING_MIP_LEVELS,
        .baseArrayLayer = range.base_layer,
        .layerCount = range.layer_count ? range.layer_count : VK_REMAINING_ARRAY_LAYERS,
    };

    if (cmd->dev->sync2)
    {
        VkPipelineStageFlags2 src_stage, dst_stage;
        VkAccessFlags2        src_access, dst_access;
        VkImageLayout         old_layout, new_layout;
        mel_gpu__state_to_barrier2(src, is_depth, &src_stage, &src_access, &old_layout);
        mel_gpu__state_to_barrier2(dst, is_depth, &dst_stage, &dst_access, &new_layout);
        if (src == MEL_GPU_STATE_COMMON)
            old_layout = VK_IMAGE_LAYOUT_UNDEFINED; // COMMON-as-source: discard (UNDEFINED), not GENERAL
        VkImageMemoryBarrier2 b = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = o->image,
            .subresourceRange = sub,
        };
        VkDependencyInfo di = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b };
        cmd->dev->cmd_pipeline_barrier2(cmd->cb, &di);
        return;
    }

    VkPipelineStageFlags src_stage, dst_stage;
    VkAccessFlags        src_access, dst_access;
    VkImageLayout        old_layout, new_layout;
    mel_gpu__state_to_barrier(src, is_depth, &src_stage, &src_access, &old_layout);
    mel_gpu__state_to_barrier(dst, is_depth, &dst_stage, &dst_access, &new_layout);
    if (src == MEL_GPU_STATE_COMMON)
        old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = o->image,
        .subresourceRange = sub,
    };
    vkCmdPipelineBarrier(cmd->cb, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}

void mel_gpu_cmd_buffer_barrier(Mel_Gpu_Command_List* cmd, Mel_Gpu_Buffer buf, Mel_Gpu_Resource_State src, Mel_Gpu_Resource_State dst)
{
    VkBuffer vk = VK_NULL_HANDLE;
    if (!cmd || !mel_gpu__buffer_get(cmd->dev, buf, &vk))
    {
        mel_assert(!"cmd_buffer_barrier: invalid buffer handle");
        return;
    }

    if (cmd->dev->sync2)
    {
        VkPipelineStageFlags2 src_stage, dst_stage;
        VkAccessFlags2        src_access, dst_access;
        VkImageLayout         ignore;
        mel_gpu__state_to_barrier2(src, false, &src_stage, &src_access, &ignore);
        mel_gpu__state_to_barrier2(dst, false, &dst_stage, &dst_access, &ignore);
        VkBufferMemoryBarrier2 b = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vk,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        VkDependencyInfo di = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &b };
        cmd->dev->cmd_pipeline_barrier2(cmd->cb, &di);
        return;
    }

    VkPipelineStageFlags src_stage, dst_stage;
    VkAccessFlags        src_access, dst_access;
    VkImageLayout        ignore;
    mel_gpu__state_to_barrier(src, false, &src_stage, &src_access, &ignore);
    mel_gpu__state_to_barrier(dst, false, &dst_stage, &dst_access, &ignore);
    VkBufferMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = vk,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(cmd->cb, src_stage, dst_stage, 0, 0, NULL, 1, &b, 0, NULL);
}

void mel_gpu_cmd_copy_texture_to_buffer(Mel_Gpu_Command_List* cmd, Mel_Gpu_Texture tex, Mel_Gpu_Subresource_Range subresource, Mel_Gpu_Buffer dst)
{
    Mel_Gpu_Texture_Obj  o_obj; // BUG-1: snapshot the immutable texture record under obj_lock
    Mel_Gpu_Texture_Obj* o = &o_obj;
    VkBuffer             vkdst = VK_NULL_HANDLE;
    if (!cmd || !mel_gpu__texture_get(cmd->dev, tex, o) || !mel_gpu__buffer_get(cmd->dev, dst, &vkdst))
    {
        mel_assert(!"cmd_copy_texture_to_buffer: invalid handle");
        return;
    }

    VkImageAspectFlags aspect = mel_gpu__aspect_flags(subresource.aspect, o->format);
    u32                mip = subresource.base_mip;
    u32                w = o->width >> mip ? o->width >> mip : 1;
    u32                h = o->height >> mip ? o->height >> mip : 1;
    VkBufferImageCopy  copy = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = { aspect, mip, subresource.base_layer, subresource.layer_count ? subresource.layer_count : 1 },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { w, h, 1 },
    };
    vkCmdCopyImageToBuffer(cmd->cb, o->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vkdst, 1, &copy);
}

// ---- U16: dynamic rendering ----

static VkAttachmentLoadOp mel_gpu__load_op(Mel_Gpu_Load_Op op)
{
    switch (op)
    {
    case MEL_GPU_LOAD_LOAD:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case MEL_GPU_LOAD_DONT_CARE:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    case MEL_GPU_LOAD_CLEAR:
    default:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    }
}

static VkAttachmentStoreOp mel_gpu__store_op(Mel_Gpu_Store_Op op)
{
    return op == MEL_GPU_STORE_DONT_CARE ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
}

void mel_gpu_cmd_begin_rendering_opt(Mel_Gpu_Command_List* cmd, Mel_Gpu_Rendering_Opt opt)
{
    mel_assert(cmd);
    Mel_Gpu_Device* dev = cmd->dev;
    if (!dev->dynamic_rendering)
    {
        mel_assert(!"cmd_begin_rendering requires VK_KHR_dynamic_rendering (render-pass floor lowering uses cmd_begin_pass)");
        return;
    }

    // CRITICAL-2: the color array is sized to opt.color_count from the device allocator (MEL-CODE-002), not a
    // fixed [8] stack array that silently truncates the rest (MEL-CODE-007 / MEL-ENGINE-VIII) — matching the
    // pipeline path's dynamic pColorAttachmentFormats. No hardware cap is encoded as a silent floor.
    u32                           n = opt.color_count;
    VkRenderingAttachmentInfoKHR* color = n ? mel_alloc_array(dev->alloc, VkRenderingAttachmentInfoKHR, n) : NULL;
    for (u32 i = 0; i < n; i++)
    {
        Mel_Gpu_Texture_View_Obj v; // BUG-1: snapshot under obj_lock
        VkImageView               iv = VK_NULL_HANDLE;
        if (mel_gpu__texture_view_get(dev, opt.colors[i].view, &v))
            iv = v.view;
        color[i] = (VkRenderingAttachmentInfoKHR){
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
            .imageView = iv,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = mel_gpu__load_op(opt.colors[i].load),
            .storeOp = mel_gpu__store_op(opt.colors[i].store),
            .clearValue = { .color = { .float32 = { opt.colors[i].clear.r, opt.colors[i].clear.g, opt.colors[i].clear.b, opt.colors[i].clear.a } } },
        };
        // U16 on-tile MSAA resolve (gpu-rhi.md §7.2): a set resolve_view resolves the multisample attachment to
        // single-sample with VK_RESOLVE_MODE_AVERAGE (the screen-space color default). generation != 0 = set.
        if (opt.colors[i].resolve_view.slot.generation != 0)
        {
            Mel_Gpu_Texture_View_Obj rv; // BUG-1: snapshot under obj_lock
            if (mel_gpu__texture_view_get(dev, opt.colors[i].resolve_view, &rv))
            {
                color[i].resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                color[i].resolveImageView = rv.view;
                color[i].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            else
                mel_log_warn("gpu", "cmd_begin_rendering: color attachment %u resolve_view is not a live view; resolve skipped", i);
        }
    }

    VkRenderingAttachmentInfoKHR depth = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
    bool                         has_depth = false;
    if (opt.depth)
    {
        Mel_Gpu_Texture_View_Obj v; // BUG-1: snapshot under obj_lock
        if (mel_gpu__texture_view_get(dev, opt.depth->view, &v))
        {
            has_depth = true;
            depth.imageView = v.view;
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth.loadOp = mel_gpu__load_op(opt.depth->load);
            depth.storeOp = mel_gpu__store_op(opt.depth->store);
            depth.clearValue.depthStencil = (VkClearDepthStencilValue){ opt.depth->clear_depth, opt.depth->clear_stencil };
        }
    }

    VkRenderingInfoKHR ri = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .renderArea = { { 0, 0 }, { opt.width, opt.height } },
        .layerCount = 1,
        .colorAttachmentCount = n,
        .pColorAttachments = n ? color : NULL,
        .pDepthAttachment = has_depth ? &depth : NULL,
    };
    dev->cmd_begin_rendering(cmd->cb, &ri);
    if (color)
        mel_dealloc(dev->alloc, color); // vkCmdBeginRendering copies the attachment array at record time

    VkViewport vp = { 0.0f, (f32)opt.height, (f32)opt.width, -(f32)opt.height, 0.0f, 1.0f };
    VkRect2D   scissor = { { 0, 0 }, { opt.width, opt.height } };
    vkCmdSetViewport(cmd->cb, 0, 1, &vp);
    vkCmdSetScissor(cmd->cb, 0, 1, &scissor);
}

void mel_gpu_cmd_end_rendering(Mel_Gpu_Command_List* cmd)
{
    mel_assert(cmd && cmd->dev->dynamic_rendering);
    cmd->dev->cmd_end_rendering(cmd->cb);
}
