#include <test/test.h>

#include <image/gpu.h>

MEL_TEST(image_gpu, to_gpu_format_mapped)
{
    MEL_EXPECT_EQ(mel_image_to_gpu_format(&mel_image_rgba8), MEL_GPU_FORMAT_RGBA8_UNORM);
    MEL_EXPECT_EQ(mel_image_to_gpu_format(&mel_image_rgba8_srgb), MEL_GPU_FORMAT_RGBA8_SRGB);
    MEL_EXPECT_EQ(mel_image_to_gpu_format(&mel_image_bgra8), MEL_GPU_FORMAT_BGRA8_UNORM);
    MEL_EXPECT_EQ(mel_image_to_gpu_format(&mel_image_rgba32f), MEL_GPU_FORMAT_RGBA32_FLOAT);
}

MEL_TEST(image_gpu, from_gpu_format_mapped)
{
    MEL_EXPECT_EQ((const void*)mel_image_from_gpu_format(MEL_GPU_FORMAT_RGBA8_UNORM), (const void*)&mel_image_rgba8);
    MEL_EXPECT_EQ((const void*)mel_image_from_gpu_format(MEL_GPU_FORMAT_RGBA8_SRGB), (const void*)&mel_image_rgba8_srgb);
    MEL_EXPECT_EQ((const void*)mel_image_from_gpu_format(MEL_GPU_FORMAT_BGRA8_UNORM), (const void*)&mel_image_bgra8);
    MEL_EXPECT_EQ((const void*)mel_image_from_gpu_format(MEL_GPU_FORMAT_RGBA32_FLOAT), (const void*)&mel_image_rgba32f);
}

MEL_TEST(image_gpu, premul_maps_to_unorm_canonical_reverse)
{
    MEL_EXPECT_EQ(mel_image_to_gpu_format(&mel_image_rgba8_premul), MEL_GPU_FORMAT_RGBA8_UNORM);
    MEL_EXPECT_EQ((const void*)mel_image_from_gpu_format(MEL_GPU_FORMAT_RGBA8_UNORM), (const void*)&mel_image_rgba8);
}

MEL_TEST(image_gpu, roundtrip)
{
    const mel_image_format* mapped[] = { &mel_image_rgba8, &mel_image_rgba8_srgb, &mel_image_bgra8, &mel_image_rgba32f };
    for (usize i = 0; i < sizeof(mapped) / sizeof(mapped[0]); i++)
    {
        Mel_Gpu_Format          g = mel_image_to_gpu_format(mapped[i]);
        const mel_image_format* back = mel_image_from_gpu_format(g);
        MEL_EXPECT_EQ((const void*)back, (const void*)mapped[i]);
    }
}

MEL_TEST(image_gpu, unmapped_yields_undefined)
{
    MEL_EXPECT_EQ(mel_image_to_gpu_format(&mel_image_nv12), MEL_GPU_FORMAT_UNDEFINED);
    MEL_EXPECT_EQ(mel_image_to_gpu_format(&mel_image_gray8), MEL_GPU_FORMAT_UNDEFINED);
    MEL_EXPECT_EQ(mel_image_to_gpu_format(NULL), MEL_GPU_FORMAT_UNDEFINED);
}

MEL_TEST(image_gpu, unmapped_gpu_yields_null)
{
    MEL_EXPECT_NULL((const void*)mel_image_from_gpu_format(MEL_GPU_FORMAT_UNDEFINED));
    MEL_EXPECT_NULL((const void*)mel_image_from_gpu_format(MEL_GPU_FORMAT_D32_FLOAT));
    MEL_EXPECT_NULL((const void*)mel_image_from_gpu_format(MEL_GPU_FORMAT_RG32_FLOAT));
}
