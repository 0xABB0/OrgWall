#include "../melody/test.harness.h"
#include "../melody/gpu.format.h"

MEL_TEST(format_size_r8, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_R8_UNORM), 1u);
}

MEL_TEST(format_size_r8g8, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_R8G8_UNORM), 2u);
}

MEL_TEST(format_size_rgba8, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_R8G8B8A8_UNORM), 4u);
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_R8G8B8A8_SRGB), 4u);
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_B8G8R8A8_UNORM), 4u);
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_B8G8R8A8_SRGB), 4u);
}

MEL_TEST(format_size_rgba16f, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_R16G16B16A16_SFLOAT), 8u);
}

MEL_TEST(format_size_rgba32f, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_R32G32B32A32_SFLOAT), 16u);
}

MEL_TEST(format_size_r32f, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_R32_SFLOAT), 4u);
}

MEL_TEST(format_size_rg32f, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_R32G32_SFLOAT), 8u);
}

MEL_TEST(format_size_rgb32f, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_R32G32B32_SFLOAT), 12u);
}

MEL_TEST(format_size_d32, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_D32_SFLOAT), 4u);
}

MEL_TEST(format_size_d24s8, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_D24_UNORM_S8_UINT), 4u);
}

MEL_TEST(format_size_d16, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_D16_UNORM), 2u);
}

MEL_TEST(format_size_d32s8, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_D32_SFLOAT_S8_UINT), 5u);
}

MEL_TEST(format_size_unknown, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_size(VK_FORMAT_UNDEFINED), 0u);
}

MEL_TEST(format_has_depth_true, .tags = "gpu")
{
    MEL_ASSERT(mel_gpu_format_has_depth(VK_FORMAT_D32_SFLOAT));
    MEL_ASSERT(mel_gpu_format_has_depth(VK_FORMAT_D16_UNORM));
    MEL_ASSERT(mel_gpu_format_has_depth(VK_FORMAT_D24_UNORM_S8_UINT));
    MEL_ASSERT(mel_gpu_format_has_depth(VK_FORMAT_D32_SFLOAT_S8_UINT));
    MEL_ASSERT(mel_gpu_format_has_depth(VK_FORMAT_X8_D24_UNORM_PACK32));
}

MEL_TEST(format_has_depth_false, .tags = "gpu")
{
    MEL_ASSERT(!mel_gpu_format_has_depth(VK_FORMAT_R8G8B8A8_UNORM));
    MEL_ASSERT(!mel_gpu_format_has_depth(VK_FORMAT_R16G16B16A16_SFLOAT));
    MEL_ASSERT(!mel_gpu_format_has_depth(VK_FORMAT_S8_UINT));
}

MEL_TEST(format_has_stencil_true, .tags = "gpu")
{
    MEL_ASSERT(mel_gpu_format_has_stencil(VK_FORMAT_S8_UINT));
    MEL_ASSERT(mel_gpu_format_has_stencil(VK_FORMAT_D24_UNORM_S8_UINT));
    MEL_ASSERT(mel_gpu_format_has_stencil(VK_FORMAT_D32_SFLOAT_S8_UINT));
}

MEL_TEST(format_has_stencil_false, .tags = "gpu")
{
    MEL_ASSERT(!mel_gpu_format_has_stencil(VK_FORMAT_D32_SFLOAT));
    MEL_ASSERT(!mel_gpu_format_has_stencil(VK_FORMAT_D16_UNORM));
    MEL_ASSERT(!mel_gpu_format_has_stencil(VK_FORMAT_R8G8B8A8_UNORM));
}

MEL_TEST(format_is_compressed_bc, .tags = "gpu")
{
    MEL_ASSERT(mel_gpu_format_is_compressed(VK_FORMAT_BC1_RGB_UNORM_BLOCK));
    MEL_ASSERT(mel_gpu_format_is_compressed(VK_FORMAT_BC3_UNORM_BLOCK));
    MEL_ASSERT(mel_gpu_format_is_compressed(VK_FORMAT_BC7_UNORM_BLOCK));
}

MEL_TEST(format_is_compressed_etc2, .tags = "gpu")
{
    MEL_ASSERT(mel_gpu_format_is_compressed(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK));
    MEL_ASSERT(mel_gpu_format_is_compressed(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK));
}

MEL_TEST(format_is_compressed_false, .tags = "gpu")
{
    MEL_ASSERT(!mel_gpu_format_is_compressed(VK_FORMAT_R8G8B8A8_UNORM));
    MEL_ASSERT(!mel_gpu_format_is_compressed(VK_FORMAT_D32_SFLOAT));
    MEL_ASSERT(!mel_gpu_format_is_compressed(VK_FORMAT_R32G32B32A32_SFLOAT));
}

MEL_TEST(format_aspect_color, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_aspect(VK_FORMAT_R8G8B8A8_UNORM), (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT);
    MEL_ASSERT_EQ(mel_gpu_format_aspect(VK_FORMAT_R16G16B16A16_SFLOAT), (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT);
}

MEL_TEST(format_aspect_depth, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_aspect(VK_FORMAT_D32_SFLOAT), (VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT);
    MEL_ASSERT_EQ(mel_gpu_format_aspect(VK_FORMAT_D16_UNORM), (VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT);
}

MEL_TEST(format_aspect_depth_stencil, .tags = "gpu")
{
    VkImageAspectFlags expected = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    MEL_ASSERT_EQ(mel_gpu_format_aspect(VK_FORMAT_D24_UNORM_S8_UINT), expected);
    MEL_ASSERT_EQ(mel_gpu_format_aspect(VK_FORMAT_D32_SFLOAT_S8_UINT), expected);
}

MEL_TEST(format_aspect_stencil_only, .tags = "gpu")
{
    MEL_ASSERT_EQ(mel_gpu_format_aspect(VK_FORMAT_S8_UINT), (VkImageAspectFlags)VK_IMAGE_ASPECT_STENCIL_BIT);
}
