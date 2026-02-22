#include "../melody/test.harness.h"
#include "../melody/swapchain.h"

typedef struct {
    u32 acquire_count;
    u32 present_count;
    u32 resize_count;
    u32 shutdown_count;

    u32 last_resize_w;
    u32 last_resize_h;

    VkCommandBuffer last_cmd;
    VkFence last_fence;

    bool acquire_result;
    bool present_result;
} Mock_Swapchain;

static bool mock_acquire(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)dev;
    Mock_Swapchain* mock = sc->data;
    mock->acquire_count++;
    sc->current_image = mock->acquire_count % sc->image_count;
    return mock->acquire_result;
}

static bool mock_present(Mel_Swapchain* sc, Mel_Gpu_Device* dev, VkCommandBuffer cmd, VkFence fence)
{
    (void)dev;
    Mock_Swapchain* mock = sc->data;
    mock->present_count++;
    mock->last_cmd = cmd;
    mock->last_fence = fence;
    return mock->present_result;
}

static void mock_resize(Mel_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height)
{
    (void)dev;
    Mock_Swapchain* mock = sc->data;
    mock->resize_count++;
    mock->last_resize_w = width;
    mock->last_resize_h = height;
    sc->extent = (VkExtent2D){ width, height };
}

static void mock_shutdown(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)dev;
    Mock_Swapchain* mock = sc->data;
    mock->shutdown_count++;
    sc->data = nullptr;
}

static const Mel_Swapchain_Vtable mock_vtable = {
    .acquire  = mock_acquire,
    .present  = mock_present,
    .resize   = mock_resize,
    .shutdown = mock_shutdown,
};

static void init_mock(Mel_Swapchain* sc, Mock_Swapchain* mock)
{
    *mock = (Mock_Swapchain){
        .acquire_result = true,
        .present_result = true,
    };

    *sc = (Mel_Swapchain){
        .vtable = &mock_vtable,
        .data = mock,
        .format = VK_FORMAT_B8G8R8A8_SRGB,
        .extent = { 800, 600 },
        .image_count = 3,
        .current_image = 0,
    };
}

MEL_TEST(vtable_acquire_dispatches, .tags = "gpu")
{
    Mel_Swapchain sc;
    Mock_Swapchain mock;
    init_mock(&sc, &mock);

    bool r = mel_swapchain_acquire(&sc, nullptr);
    MEL_ASSERT(r);
    MEL_ASSERT_EQ(mock.acquire_count, 1u);

    mel_swapchain_acquire(&sc, nullptr);
    MEL_ASSERT_EQ(mock.acquire_count, 2u);

}

MEL_TEST(vtable_present_dispatches, .tags = "gpu")
{
    Mel_Swapchain sc;
    Mock_Swapchain mock;
    init_mock(&sc, &mock);

    VkCommandBuffer fake_cmd = (VkCommandBuffer)(uintptr_t)0xDEAD;
    VkFence fake_fence = (VkFence)(uintptr_t)0xBEEF;

    bool r = mel_swapchain_present(&sc, nullptr, fake_cmd, fake_fence);
    MEL_ASSERT(r);
    MEL_ASSERT_EQ(mock.present_count, 1u);
    MEL_ASSERT_EQ(mock.last_cmd, fake_cmd);
    MEL_ASSERT_EQ(mock.last_fence, fake_fence);

}

MEL_TEST(vtable_resize_dispatches, .tags = "gpu")
{
    Mel_Swapchain sc;
    Mock_Swapchain mock;
    init_mock(&sc, &mock);

    mel_swapchain_resize(&sc, nullptr, 1920, 1080);
    MEL_ASSERT_EQ(mock.resize_count, 1u);
    MEL_ASSERT_EQ(mock.last_resize_w, 1920u);
    MEL_ASSERT_EQ(mock.last_resize_h, 1080u);
    MEL_ASSERT_EQ(sc.extent.width, 1920u);
    MEL_ASSERT_EQ(sc.extent.height, 1080u);

}

MEL_TEST(vtable_shutdown_dispatches, .tags = "gpu")
{
    Mel_Swapchain sc;
    Mock_Swapchain mock;
    init_mock(&sc, &mock);

    mel_swapchain_shutdown(&sc, nullptr);
    MEL_ASSERT_EQ(mock.shutdown_count, 1u);
    MEL_ASSERT_NULL(sc.data);

}

MEL_TEST(acquire_updates_current_image, .tags = "gpu")
{
    Mel_Swapchain sc;
    Mock_Swapchain mock;
    init_mock(&sc, &mock);

    mel_swapchain_acquire(&sc, nullptr);
    MEL_ASSERT_EQ(sc.current_image, 1u);

    mel_swapchain_acquire(&sc, nullptr);
    MEL_ASSERT_EQ(sc.current_image, 2u);

    mel_swapchain_acquire(&sc, nullptr);
    MEL_ASSERT_EQ(sc.current_image, 0u);

}

MEL_TEST(acquire_failure_propagates, .tags = "gpu")
{
    Mel_Swapchain sc;
    Mock_Swapchain mock;
    init_mock(&sc, &mock);
    mock.acquire_result = false;

    bool r = mel_swapchain_acquire(&sc, nullptr);
    MEL_ASSERT(!r);
    MEL_ASSERT_EQ(mock.acquire_count, 1u);

}

MEL_TEST(present_failure_propagates, .tags = "gpu")
{
    Mel_Swapchain sc;
    Mock_Swapchain mock;
    init_mock(&sc, &mock);
    mock.present_result = false;

    bool r = mel_swapchain_present(&sc, nullptr, nullptr, nullptr);
    MEL_ASSERT(!r);
    MEL_ASSERT_EQ(mock.present_count, 1u);

}

MEL_TEST(fields_accessible, .tags = "gpu")
{
    Mel_Swapchain sc;
    Mock_Swapchain mock;
    init_mock(&sc, &mock);

    MEL_ASSERT_EQ(sc.format, VK_FORMAT_B8G8R8A8_SRGB);
    MEL_ASSERT_EQ(sc.extent.width, 800u);
    MEL_ASSERT_EQ(sc.extent.height, 600u);
    MEL_ASSERT_EQ(sc.image_count, 3u);
    MEL_ASSERT_EQ(sc.current_image, 0u);
    MEL_ASSERT_NOT_NULL(sc.vtable);
    MEL_ASSERT_NOT_NULL(sc.data);

}

MEL_TEST(multiple_swapchains_independent, .tags = "gpu")
{
    Mel_Swapchain sc_a, sc_b;
    Mock_Swapchain mock_a, mock_b;
    init_mock(&sc_a, &mock_a);
    init_mock(&sc_b, &mock_b);

    sc_b.extent = (VkExtent2D){ 1280, 720 };
    sc_b.image_count = 2;

    mel_swapchain_acquire(&sc_a, nullptr);
    MEL_ASSERT_EQ(mock_a.acquire_count, 1u);
    MEL_ASSERT_EQ(mock_b.acquire_count, 0u);

    mel_swapchain_resize(&sc_b, nullptr, 640, 480);
    MEL_ASSERT_EQ(mock_a.resize_count, 0u);
    MEL_ASSERT_EQ(mock_b.resize_count, 1u);
    MEL_ASSERT_EQ(sc_a.extent.width, 800u);
    MEL_ASSERT_EQ(sc_b.extent.width, 640u);

}

static bool alt_acquire(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)dev;
    sc->current_image = 42;
    return true;
}

static bool alt_present(Mel_Swapchain* sc, Mel_Gpu_Device* dev, VkCommandBuffer cmd, VkFence fence)
{
    (void)sc; (void)dev; (void)cmd; (void)fence;
    return true;
}

static void alt_resize(Mel_Swapchain* sc, Mel_Gpu_Device* dev, u32 w, u32 h)
{
    (void)dev;
    sc->extent = (VkExtent2D){ w * 2, h * 2 };
}

static void alt_shutdown(Mel_Swapchain* sc, Mel_Gpu_Device* dev)
{
    (void)dev; (void)sc;
}

static const Mel_Swapchain_Vtable alt_vtable = {
    .acquire  = alt_acquire,
    .present  = alt_present,
    .resize   = alt_resize,
    .shutdown = alt_shutdown,
};

MEL_TEST(different_vtables_dispatch_independently, .tags = "gpu")
{
    Mel_Swapchain sc_mock, sc_alt;
    Mock_Swapchain mock;
    init_mock(&sc_mock, &mock);

    sc_alt = (Mel_Swapchain){
        .vtable = &alt_vtable,
        .data = nullptr,
        .image_count = 1,
    };

    mel_swapchain_acquire(&sc_alt, nullptr);
    MEL_ASSERT_EQ(sc_alt.current_image, 42u);

    mel_swapchain_resize(&sc_alt, nullptr, 100, 200);
    MEL_ASSERT_EQ(sc_alt.extent.width, 200u);
    MEL_ASSERT_EQ(sc_alt.extent.height, 400u);

    mel_swapchain_acquire(&sc_mock, nullptr);
    MEL_ASSERT_EQ(sc_mock.current_image, 1u);

}

MEL_TEST(full_lifecycle, .tags = "gpu")
{
    Mel_Swapchain sc;
    Mock_Swapchain mock;
    init_mock(&sc, &mock);

    bool r = mel_swapchain_acquire(&sc, nullptr);
    MEL_ASSERT(r);

    r = mel_swapchain_present(&sc, nullptr, nullptr, nullptr);
    MEL_ASSERT(r);

    mel_swapchain_resize(&sc, nullptr, 1920, 1080);
    MEL_ASSERT_EQ(sc.extent.width, 1920u);

    r = mel_swapchain_acquire(&sc, nullptr);
    MEL_ASSERT(r);

    r = mel_swapchain_present(&sc, nullptr, nullptr, nullptr);
    MEL_ASSERT(r);

    mel_swapchain_shutdown(&sc, nullptr);
    MEL_ASSERT_NULL(sc.data);

    MEL_ASSERT_EQ(mock.acquire_count, 2u);
    MEL_ASSERT_EQ(mock.present_count, 2u);
    MEL_ASSERT_EQ(mock.resize_count, 1u);
    MEL_ASSERT_EQ(mock.shutdown_count, 1u);

}
