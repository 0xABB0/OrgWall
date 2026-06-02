#pragma once

#include <core/types.h>
#include <gpu/handle.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/sampler.h>

typedef struct Mel_Gpu_Device       Mel_Gpu_Device;
typedef struct Mel_Gpu_Command_List Mel_Gpu_Command_List;

// U14 resource binding (gpu-rhi.md §6.7). The engine maintains one device-global bindless heap: a
// persistent integer-indexed array per resource class. Engine-created direct resources auto-register at
// their handle index when the heap is enabled, so a resource keeps its slot from creation to destroy with
// no per-frame rebinding. The shader receives a per-draw root record carrying these indices; on the Vulkan
// floor the carrier is a push-constant block the user fills via mel_gpu_cmd_push_constants. The simple path
// is the powerful path further along (MEL-ENGINE-II): bind a bindless pipeline and the heap follows.

// True when the device created its bindless heap (descriptor-indexing floor granted and requested at
// device-create via Mel_Gpu_Feature_Request.descriptor_indexing).
bool mel_gpu_bindless_available(Mel_Gpu_Device* dev);

// Shader-visible heap slots for the direct families. Each equals the handle index (§3.1) but is queried so
// call sites stay correct if a handle migrates to the indirect family. Assert the resource is registered.
u32 mel_gpu_texture_view_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view);
u32 mel_gpu_buffer_bindless_slot(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf);

// U14 ceiling (gpu-rhi.md §6.7): the buffer's GPU device address, for a pointer-bearing root record where
// the shader dereferences buffer data directly rather than through a descriptor index. The buffer must
// carry MEL_GPU_BUFFER_DEVICE_ADDRESS usage and the device must have been created with
// buffer_device_address. Returns 0 when unavailable (caps.memory.bindless.root_record_payload reports it).
u64 mel_gpu_buffer_device_address(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf);

// Bind the device-global heap descriptor set at set 0 for subsequent draws/dispatches on this command
// list. cmd_bind_pipeline does this automatically for bindless pipelines; the explicit call is the P2 peer
// for users driving their own bind cadence.
void mel_gpu_cmd_bind_bindless(Mel_Gpu_Command_List* cmd);
