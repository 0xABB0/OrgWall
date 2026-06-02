#include "d3d_backend.h"

#include <gpu/swapchain.h>
#include <gpu/command.h>
#include <gpu/rendering.h>

#include <log/log.h>

// U18 DXGI flip-model swapchain (gpu-rhi.md §7.4; design/gpu-d3d12.md Phase 4). The Vulkan reference shape is
// src/vulkan/swapchain.c + src/vulkan/command.c (the frame loop). Here the swapchain *is* the present surface
// (DXGI has no separate VkSurfaceKHR object): CreateSwapChainForHwnd over the window's HWND with
// DXGI_SWAP_EFFECT_FLIP_DISCARD — the only modern, low-latency, tearing-capable model the in-box runtime
// offers (the legacy BitBlt models are MEL-ENGINE-VII "broken shadow" we refuse). Back buffers carry an RTV
// each in a dedicated CPU RTV heap; the per-frame command allocator/list pairs are fenced on the device
// timeline serial (frames_in_flight deep). Present is sync-interval 1 (vsync) or 0 (+ALLOW_TEARING when the
// surface supports it). No depth: the swapchain owns only its color targets; a depth target is the app's
// (cf. the Vulkan swapchain, which is also color-only).

// ---- device-lost (gpu-rhi.md §3.4). DXGI present/submit can surface a removed/hung/reset device; report it
// once, fire the user callback, and latch `lost` so teardown skips the drain. Mirrors the Vulkan
// mel_gpu__device_is_lost. ----
bool mel_gpu__device_is_lost(Mel_Gpu_Device* dev, HRESULT hr, const char* where)
{
    if (hr != DXGI_ERROR_DEVICE_REMOVED && hr != DXGI_ERROR_DEVICE_RESET && hr != DXGI_ERROR_DEVICE_HUNG)
        return false;
    if (!dev->lost)
    {
        dev->lost = true;
        HRESULT reason = dev->d3d ? ID3D12Device_GetDeviceRemovedReason(dev->d3d) : hr;
        mel_log_error("gpu", "device lost at %s: 0x%08lx (removed-reason 0x%08lx)", where, (unsigned long)hr, (unsigned long)reason);
        if (dev->on_device_lost)
            dev->on_device_lost(dev, where, dev->device_lost_user);
    }
    return true;
}

static bool mel_gpu__tearing_supported(Mel_Gpu_Instance* inst)
{
    BOOL allow = FALSE;
    if (FAILED(IDXGIFactory6_CheckFeatureSupport(inst->factory, DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof allow)))
        return false;
    return allow == TRUE;
}

// §6.5 format selection: the requested format if it is a presentable flip-model color format, else the
// flip-model default B8G8R8A8_UNORM. FLIP_DISCARD forbids _SRGB back-buffer formats and typeless/depth, so an
// unrequestable or unsupported format is rejected to the UNORM default rather than silently honored
// (MEL-ENGINE-VIII). Only the two 8-bit UNORM color formats the backend's DXGI map produces are presentable;
// a wide-gamut HDR back-buffer (R10G10B10A2 / RGBA16F) rides the HDR-output tier once those mel formats exist.
static DXGI_FORMAT mel_gpu__present_format(Mel_Gpu_Format requested, Mel_Gpu_Format* out_mel)
{
    DXGI_FORMAT want = requested == MEL_GPU_FORMAT_UNDEFINED ? DXGI_FORMAT_UNKNOWN : mel_gpu__dxgi_format(requested);
    if (want == DXGI_FORMAT_R8G8B8A8_UNORM || want == DXGI_FORMAT_B8G8R8A8_UNORM)
    {
        *out_mel = requested;
        return want;
    }
    *out_mel = MEL_GPU_FORMAT_BGRA8_UNORM;
    return DXGI_FORMAT_B8G8R8A8_UNORM;
}

// Acquire the back-buffer resources + their RTVs after create/resize. The swapchain owns a ref on each back
// buffer (GetBuffer AddRefs); released in mel_gpu__buffers_teardown.
static bool mel_gpu__buffers_acquire(Mel_Gpu_Swapchain* sc)
{
    Mel_Gpu_Device* dev = sc->dev;
    for (u32 i = 0; i < sc->buffer_count; i++)
    {
        if (FAILED(IDXGISwapChain3_GetBuffer(sc->swap, i, &IID_ID3D12Resource, (void**)&sc->buffers[i])) || !sc->buffers[i])
        {
            mel_log_error("gpu", "swapchain: GetBuffer(%u) failed", i);
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv;
        ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sc->rtv_heap, &rtv);
        rtv.ptr += (SIZE_T)i * sc->rtv_inc;
        D3D12_RENDER_TARGET_VIEW_DESC rtvd = { .Format = sc->format, .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D };
        ID3D12Device_CreateRenderTargetView(dev->d3d, sc->buffers[i], &rtvd, rtv);
    }
    return true;
}

static void mel_gpu__buffers_teardown(Mel_Gpu_Swapchain* sc)
{
    for (u32 i = 0; i < sc->buffer_count; i++)
        if (sc->buffers && sc->buffers[i])
        {
            ID3D12Resource_Release(sc->buffers[i]);
            sc->buffers[i] = NULL;
        }
}

static D3D12_CPU_DESCRIPTOR_HANDLE mel_gpu__back_rtv(Mel_Gpu_Swapchain* sc, u32 index)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(sc->rtv_heap, &rtv);
    rtv.ptr += (SIZE_T)index * sc->rtv_inc;
    return rtv;
}

// Common setup once a DXGI flip-model swapchain object exists (from the HWND production path or the
// composition headless-test path): adopt the IDXGISwapChain3, build the RTV heap + back-buffer RTVs, and
// allocate the per-frame command pairs. Consumes `sc1` (releases it). Returns the swapchain or NULL (after
// teardown). `sc` is already populated with width/height/format/vsync/allow_tearing/recorder.
static Mel_Gpu_Swapchain* mel_gpu__swapchain_finish(Mel_Gpu_Swapchain* sc, IDXGISwapChain1* sc1)
{
    Mel_Gpu_Device* dev = sc->dev;
    HRESULT         hr = IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3, (void**)&sc->swap);
    IDXGISwapChain1_Release(sc1);
    if (FAILED(hr) || !sc->swap)
    {
        mel_log_error("gpu", "swapchain: IDXGISwapChain3 query failed: 0x%08lx", (unsigned long)hr);
        mel_dealloc(dev->alloc, sc);
        return NULL;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtvd = { .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, .NumDescriptors = sc->buffer_count, .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE };
    ID3D12Device_CreateDescriptorHeap(dev->d3d, &rtvd, &IID_ID3D12DescriptorHeap, (void**)&sc->rtv_heap);
    sc->rtv_inc = ID3D12Device_GetDescriptorHandleIncrementSize(dev->d3d, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    sc->buffers = mel_alloc_array(dev->alloc, ID3D12Resource*, sc->buffer_count);
    for (u32 i = 0; i < sc->buffer_count; i++)
        sc->buffers[i] = NULL;
    if (!sc->rtv_heap || !mel_gpu__buffers_acquire(sc))
    {
        mel_gpu_swapchain_destroy(sc);
        return NULL;
    }

    // Per-frame command allocator/list pairs (frames_in_flight deep). Each is created closed; frame_begin
    // resets it. The single-use contract is the same as the U15 standalone path; the fence serial gates reuse.
    sc->allocators = mel_alloc_array(dev->alloc, ID3D12CommandAllocator*, sc->frames_in_flight);
    sc->lists = mel_alloc_array(dev->alloc, ID3D12GraphicsCommandList*, sc->frames_in_flight);
    sc->frame_serial = mel_alloc_array(dev->alloc, u64, sc->frames_in_flight);
    for (u32 i = 0; i < sc->frames_in_flight; i++)
    {
        sc->allocators[i] = NULL;
        sc->lists[i] = NULL;
        sc->frame_serial[i] = 0;
    }
    for (u32 i = 0; i < sc->frames_in_flight; i++)
    {
        if (FAILED(ID3D12Device_CreateCommandAllocator(dev->d3d, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&sc->allocators[i])) ||
            FAILED(ID3D12Device_CreateCommandList(dev->d3d, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, sc->allocators[i], NULL, &IID_ID3D12GraphicsCommandList, (void**)&sc->lists[i])))
        {
            mel_log_error("gpu", "swapchain: per-frame allocator/list creation failed");
            mel_gpu_swapchain_destroy(sc);
            return NULL;
        }
        ID3D12GraphicsCommandList_Close(sc->lists[i]);
    }

    sc->back_index = IDXGISwapChain3_GetCurrentBackBufferIndex(sc->swap);
    mel_log_info("gpu", "swapchain ready: %u buffers %ux%u%s", sc->buffer_count, sc->width, sc->height, sc->allow_tearing ? " (tearing)" : "");
    return sc;
}

static void mel_gpu__swapchain_common_init(Mel_Gpu_Swapchain* sc, Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    *sc = (Mel_Gpu_Swapchain){ 0 };
    sc->dev = dev;
    sc->surface = opt.surface;
    sc->vsync = opt.vsync;
    sc->width = opt.width > 0 ? (u32)opt.width : 1;
    sc->height = opt.height > 0 ? (u32)opt.height : 1;
    sc->buffer_count = 2; // FLIP_DISCARD requires >= 2; double-buffered floor (triple is an additive tier)
    sc->frames_in_flight = 2;
    sc->format = mel_gpu__present_format(opt.format, &sc->mel_format);
    sc->allow_tearing = !opt.vsync && mel_gpu__tearing_supported(dev->instance);
    sc->recorder.dev = dev;
    sc->recorder.sc = sc;
}

static DXGI_SWAP_CHAIN_DESC1 mel_gpu__swapchain_desc(const Mel_Gpu_Swapchain* sc)
{
    return (DXGI_SWAP_CHAIN_DESC1){
        .Width = sc->width,
        .Height = sc->height,
        .Format = sc->format,
        .SampleDesc = { .Count = 1, .Quality = 0 },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = sc->buffer_count,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = sc->allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0,
    };
}

Mel_Gpu_Swapchain* mel_gpu_swapchain_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    if (!dev || !opt.surface || !opt.surface->hwnd)
    {
        mel_log_error("gpu", "swapchain_create: null device, surface, or window handle");
        return NULL;
    }

    Mel_Gpu_Swapchain* sc = mel_alloc_type(dev->alloc, Mel_Gpu_Swapchain);
    mel_gpu__swapchain_common_init(sc, dev, opt);

    // The swapchain presents from the DIRECT command queue (flip-model swapchains create against the queue,
    // not the device). Fullscreen-transitions are disabled — Alt+Enter handling is the app's (the window
    // module owns the window); MakeWindowAssociation(NO_ALT_ENTER) keeps DXGI out of the message loop.
    // Tearing is best-effort: some windows/sessions report the feature but reject the ALLOW_TEARING swapchain
    // flag with DXGI_ERROR_INVALID_CALL, so a tearing-flag failure retries vsync-capable (MEL-ENGINE-VII).
    DXGI_SWAP_CHAIN_DESC1 scd = mel_gpu__swapchain_desc(sc);
    IDXGISwapChain1*      sc1 = NULL;
    HRESULT               hr = IDXGIFactory6_CreateSwapChainForHwnd(dev->instance->factory, (IUnknown*)dev->direct_queue, (HWND)opt.surface->hwnd, &scd, NULL, NULL, &sc1);
    if (FAILED(hr) && sc->allow_tearing)
    {
        mel_log_warn("gpu", "CreateSwapChainForHwnd(tearing) failed 0x%08lx; retrying without tearing", (unsigned long)hr);
        sc->allow_tearing = false;
        scd.Flags = 0;
        hr = IDXGIFactory6_CreateSwapChainForHwnd(dev->instance->factory, (IUnknown*)dev->direct_queue, (HWND)opt.surface->hwnd, &scd, NULL, NULL, &sc1);
    }
    if (FAILED(hr) || !sc1)
    {
        mel_log_error("gpu", "CreateSwapChainForHwnd failed: 0x%08lx", (unsigned long)hr);
        mel_dealloc(dev->alloc, sc);
        return NULL;
    }
    IDXGIFactory6_MakeWindowAssociation(dev->instance->factory, (HWND)opt.surface->hwnd, DXGI_MWA_NO_ALT_ENTER);
    return mel_gpu__swapchain_finish(sc, sc1);
}

// Test-only headless swapchain (gpu-d3d12 swapchain test): a non-interactive (SSH service) window station has
// no DWM, so CreateSwapChainForHwnd fails with DXGI_ERROR_INVALID_CALL — an environment limit, not a backend
// bug. CreateSwapChainForComposition needs no HWND/desktop, so it drives the identical back-buffer / present /
// resize machinery headlessly. Not on the public surface (production presents to a window); declared extern in
// the test. The composition swapchain forbids ALLOW_TEARING, so tearing is forced off here.
Mel_Gpu_Swapchain* mel_gpu__swapchain_create_headless(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    if (!dev)
        return NULL;
    Mel_Gpu_Swapchain* sc = mel_alloc_type(dev->alloc, Mel_Gpu_Swapchain);
    mel_gpu__swapchain_common_init(sc, dev, opt);
    sc->allow_tearing = false;

    // Composition swapchains require an explicit alpha mode (PREMULTIPLIED), forbid ALLOW_TEARING, and use the
    // flip-model present effect. NB: DirectComposition is still backed by the DWM, so this too returns
    // DXGI_ERROR_INVALID_CALL in a non-interactive (SSH service) window station — no DXGI swapchain of any kind
    // exists there. The caller treats a NULL return as "skip" in that environment.
    DXGI_SWAP_CHAIN_DESC1 scd = mel_gpu__swapchain_desc(sc);
    scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    scd.Flags = 0;
    IDXGISwapChain1* sc1 = NULL;
    HRESULT          hr = IDXGIFactory6_CreateSwapChainForComposition(dev->instance->factory, (IUnknown*)dev->direct_queue, &scd, NULL, &sc1);
    if (FAILED(hr) || !sc1)
    {
        mel_log_warn("gpu", "CreateSwapChainForComposition failed: 0x%08lx (expected in a non-interactive session)", (unsigned long)hr);
        mel_dealloc(dev->alloc, sc);
        return NULL;
    }
    return mel_gpu__swapchain_finish(sc, sc1);
}

void mel_gpu_swapchain_resize(Mel_Gpu_Swapchain* sc, i32 width, i32 height)
{
    if (!sc || width <= 0 || height <= 0)
        return;
    Mel_Gpu_Device* dev = sc->dev;

    // Every in-flight frame must retire before the back buffers are released (DXGI requires it; outstanding
    // references make ResizeBuffers fail). Drain on the device timeline.
    if (!dev->lost)
    {
        u64 s = mel_gpu__submit_serial_next(dev);
        if (SUCCEEDED(ID3D12CommandQueue_Signal(dev->direct_queue, dev->timeline, s)))
            mel_gpu__wait_serial(dev, s);
        mel_gpu__submit_complete(dev, s);
    }

    mel_gpu__buffers_teardown(sc);
    UINT    flags = sc->allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    HRESULT hr = IDXGISwapChain3_ResizeBuffers(sc->swap, sc->buffer_count, (UINT)width, (UINT)height, sc->format, flags);
    if (FAILED(hr))
    {
        if (mel_gpu__device_is_lost(dev, hr, "ResizeBuffers"))
            return;
        mel_log_error("gpu", "swapchain: ResizeBuffers failed: 0x%08lx", (unsigned long)hr);
        return;
    }
    sc->width = (u32)width;
    sc->height = (u32)height;
    mel_gpu__buffers_acquire(sc);
    sc->back_index = IDXGISwapChain3_GetCurrentBackBufferIndex(sc->swap);
}

void mel_gpu_swapchain_destroy(Mel_Gpu_Swapchain* sc)
{
    if (!sc)
        return;
    Mel_Gpu_Device* dev = sc->dev;

    if (dev->direct_queue && dev->timeline && !dev->lost)
    {
        u64 s = mel_gpu__submit_serial_next(dev);
        if (SUCCEEDED(ID3D12CommandQueue_Signal(dev->direct_queue, dev->timeline, s)))
            mel_gpu__wait_serial(dev, s);
        mel_gpu__submit_complete(dev, s);
    }

    if (sc->lists)
        for (u32 i = 0; i < sc->frames_in_flight; i++)
            if (sc->lists[i])
                ID3D12GraphicsCommandList_Release(sc->lists[i]);
    if (sc->allocators)
        for (u32 i = 0; i < sc->frames_in_flight; i++)
            if (sc->allocators[i])
                ID3D12CommandAllocator_Release(sc->allocators[i]);

    mel_gpu__buffers_teardown(sc);
    if (sc->rtv_heap)
        ID3D12DescriptorHeap_Release(sc->rtv_heap);
    if (sc->swap)
        IDXGISwapChain3_Release(sc->swap);
    if (sc->recorder.states)
        mel_dealloc(dev->alloc, sc->recorder.states);

    if (sc->buffers)
        mel_dealloc(dev->alloc, sc->buffers);
    if (sc->lists)
        mel_dealloc(dev->alloc, sc->lists);
    if (sc->allocators)
        mel_dealloc(dev->alloc, sc->allocators);
    if (sc->frame_serial)
        mel_dealloc(dev->alloc, sc->frame_serial);
    mel_dealloc(dev->alloc, sc);
}

Mel_Gpu_Format mel_gpu_swapchain_format(const Mel_Gpu_Swapchain* sc) { return sc ? sc->mel_format : MEL_GPU_FORMAT_UNDEFINED; }

// ---- U18 frame loop (gpu-rhi.md §7.4) — the present-path analog of src/vulkan/command.c. ----

void mel_gpu_frame_begin(Mel_Gpu_Swapchain* sc)
{
    Mel_Gpu_Device* dev = sc->dev;
    u32             frame = sc->frame_index;
    sc->frame_ok = false;
    if (dev->lost)
        return;

    // Wait the device timeline past this frame slot's prior submission so its allocator is safe to reset, then
    // advance the retirement watermark past it (the slot's resources have now retired). DXGI flip-model has no
    // explicit acquire — GetCurrentBackBufferIndex names the writable buffer for this frame.
    if (sc->frame_serial[frame])
    {
        mel_gpu__wait_serial(dev, sc->frame_serial[frame]);
        mel_gpu__submit_complete(dev, sc->frame_serial[frame]);
    }
    sc->back_index = IDXGISwapChain3_GetCurrentBackBufferIndex(sc->swap);

    ID3D12CommandAllocator_Reset(sc->allocators[frame]);
    ID3D12GraphicsCommandList_Reset(sc->lists[frame], sc->allocators[frame], NULL);

    sc->recorder.allocator = sc->allocators[frame];
    sc->recorder.list = sc->lists[frame];
    sc->recorder.recording = true;
    sc->recorder.state_count = 0;
    sc->frame_ok = true;
}

Mel_Gpu_Command_List* mel_gpu_frame_commands(Mel_Gpu_Swapchain* sc) { return &sc->recorder; }

void mel_gpu_frame_end(Mel_Gpu_Swapchain* sc)
{
    if (!sc->frame_ok)
        return;
    Mel_Gpu_Device*            dev = sc->dev;
    u32                        frame = sc->frame_index;
    ID3D12GraphicsCommandList* list = sc->lists[frame];

    ID3D12GraphicsCommandList_Close(list);
    sc->recorder.recording = false;

    u64                serial = mel_gpu__submit_serial_next(dev);
    ID3D12CommandList* cl = (ID3D12CommandList*)list;
    mel_mutex_lock(&dev->submit_lock);
    ID3D12CommandQueue_ExecuteCommandLists(dev->direct_queue, 1, &cl);
    HRESULT hr = ID3D12CommandQueue_Signal(dev->direct_queue, dev->timeline, serial);
    mel_mutex_unlock(&dev->submit_lock);
    if (FAILED(hr) && mel_gpu__device_is_lost(dev, hr, "frame_end Signal"))
        return;
    sc->frame_serial[frame] = serial;

    UINT    sync = sc->vsync ? 1 : 0;
    UINT    flags = (!sc->vsync && sc->allow_tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    HRESULT pr = IDXGISwapChain3_Present(sc->swap, sync, flags);
    if (FAILED(pr))
    {
        if (mel_gpu__device_is_lost(dev, pr, "Present"))
            return;
        mel_log_error("gpu", "swapchain: Present failed: 0x%08lx", (unsigned long)pr);
    }

    sc->frame_index = (sc->frame_index + 1) % sc->frames_in_flight;
}

// U16 simple-path pass over the acquired back buffer (cf. the Vulkan mel_gpu_cmd_begin_pass). Bring the back
// buffer COMMON -> RENDER_TARGET, bind its RTV, clear, and set a full-surface viewport/scissor. The matching
// end_pass returns it to PRESENT. The general begin_rendering path (record.c) renders into app textures; this
// pair is the swapchain-target convenience the frame loop uses.
void mel_gpu_cmd_begin_pass(Mel_Gpu_Command_List* cmd, Mel_Gpu_Color clear)
{
    Mel_Gpu_Swapchain* sc = cmd->sc;
    mel_assert(sc && "cmd_begin_pass requires a swapchain frame recorder");
    ID3D12Resource* back = sc->buffers[sc->back_index];

    D3D12_RESOURCE_BARRIER to_rt = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = { .pResource = back, .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, .StateBefore = D3D12_RESOURCE_STATE_COMMON, .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET },
    };
    ID3D12GraphicsCommandList_ResourceBarrier(cmd->list, 1, &to_rt);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = mel_gpu__back_rtv(sc, sc->back_index);
    ID3D12GraphicsCommandList_OMSetRenderTargets(cmd->list, 1, &rtv, FALSE, NULL);
    FLOAT c[4] = { clear.r, clear.g, clear.b, clear.a };
    ID3D12GraphicsCommandList_ClearRenderTargetView(cmd->list, rtv, c, 0, NULL);

    D3D12_VIEWPORT vp = { .TopLeftX = 0.0f, .TopLeftY = 0.0f, .Width = (FLOAT)sc->width, .Height = (FLOAT)sc->height, .MinDepth = 0.0f, .MaxDepth = 1.0f };
    D3D12_RECT     scissor = { 0, 0, (LONG)sc->width, (LONG)sc->height };
    ID3D12GraphicsCommandList_RSSetViewports(cmd->list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(cmd->list, 1, &scissor);
}

void mel_gpu_cmd_end_pass(Mel_Gpu_Command_List* cmd)
{
    Mel_Gpu_Swapchain* sc = cmd->sc;
    mel_assert(sc && "cmd_end_pass requires a swapchain frame recorder");
    ID3D12Resource* back = sc->buffers[sc->back_index];

    D3D12_RESOURCE_BARRIER to_present = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = { .pResource = back, .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET, .StateAfter = D3D12_RESOURCE_STATE_PRESENT },
    };
    ID3D12GraphicsCommandList_ResourceBarrier(cmd->list, 1, &to_present);
}

// Test-only (gpu-d3d12 swapchain test): copy the last-presented back buffer into `dst` (a READBACK buffer) so
// the rendered clear is CPU-verifiable — a presented HWND front buffer is not directly mappable. Not part of
// the public surface (it would expose the swapchain's internal back buffers); declared extern in the test.
// Runs after frame_end (the back buffer sits in PRESENT == COMMON, so COPY_SOURCE auto-promotes), on a
// transient DIRECT list, fence-waited synchronously.
bool mel_gpu__swapchain_readback_back(Mel_Gpu_Swapchain* sc, Mel_Gpu_Buffer dst)
{
    Mel_Gpu_Device* dev = sc->dev;
    ID3D12Resource* dst_res = NULL;
    if (!sc->swap || !mel_gpu__buffer_resource(dev, dst, &dst_res))
        return false;
    // `back_index` is re-queried only at frame_begin, so right after frame_end it still names the buffer the
    // just-finished frame rendered into; its content persists until the next frame reuses it (FLIP_DISCARD
    // discards the present, not the resource memory). Copy that buffer.
    ID3D12Resource* back = sc->buffers[sc->back_index];

    ID3D12CommandAllocator*    allocr = NULL;
    ID3D12GraphicsCommandList* list = NULL;
    if (FAILED(ID3D12Device_CreateCommandAllocator(dev->d3d, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void**)&allocr)) ||
        FAILED(ID3D12Device_CreateCommandList(dev->d3d, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocr, NULL, &IID_ID3D12GraphicsCommandList, (void**)&list)))
    {
        if (allocr)
            ID3D12CommandAllocator_Release(allocr);
        return false;
    }

    D3D12_RESOURCE_DESC td;
    ID3D12Resource_GetDesc(back, &td);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = { 0 };
    ID3D12Device_GetCopyableFootprints(dev->d3d, &td, 0, 1, 0, &fp, NULL, NULL, NULL);

    D3D12_RESOURCE_BARRIER to_copy = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = { .pResource = back, .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, .StateBefore = D3D12_RESOURCE_STATE_PRESENT, .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE },
    };
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &to_copy);
    D3D12_TEXTURE_COPY_LOCATION src_loc = { .pResource = back, .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, .SubresourceIndex = 0 };
    D3D12_TEXTURE_COPY_LOCATION dst_loc = { .pResource = dst_res, .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, .PlacedFootprint = fp };
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst_loc, 0, 0, 0, &src_loc, NULL);
    D3D12_RESOURCE_BARRIER to_present = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition = { .pResource = back, .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, .StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE, .StateAfter = D3D12_RESOURCE_STATE_PRESENT },
    };
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &to_present);
    ID3D12GraphicsCommandList_Close(list);

    u64                serial = mel_gpu__submit_serial_next(dev);
    ID3D12CommandList* cl = (ID3D12CommandList*)list;
    mel_mutex_lock(&dev->submit_lock);
    ID3D12CommandQueue_ExecuteCommandLists(dev->direct_queue, 1, &cl);
    HRESULT hr = ID3D12CommandQueue_Signal(dev->direct_queue, dev->timeline, serial);
    mel_mutex_unlock(&dev->submit_lock);
    if (SUCCEEDED(hr))
        mel_gpu__wait_serial(dev, serial);
    mel_gpu__submit_complete(dev, serial);

    ID3D12GraphicsCommandList_Release(list);
    ID3D12CommandAllocator_Release(allocr);
    return SUCCEEDED(hr);
}
