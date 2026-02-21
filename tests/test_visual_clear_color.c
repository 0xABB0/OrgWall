#include "../melody/test.harness.h"
#include "../melody/test.visual.h"

static void render_clear_red(Mel_Gpu_Cmd* cmd, Mel_Swapchain* sc, void* user_data)
{
    (void)user_data;

    mel_gpu_cmd_image_barrier(cmd,
        sc->images[sc->current_image],
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    Mel_Gpu_Color_Attachment color_att = {
        .image_view = sc->image_views[sc->current_image],
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .clear_r = 1.0f,
        .clear_g = 0.0f,
        .clear_b = 0.0f,
        .clear_a = 1.0f,
    };

    mel_gpu_cmd_begin_rendering(cmd,
        .color_attachments = &color_att,
        .color_count = 1,
        .render_width = sc->extent.width,
        .render_height = sc->extent.height);

    mel_gpu_cmd_end_rendering(cmd);
}

MEL_TEST(clear_red)
{
    Mel_Visual_Test_Ctx ctx = {0};
    MEL_ASSERT(mel_visual_test_init(&ctx, .width = 64, .height = 64));

    Mel_Visual_Test_Result result = mel_visual_test_check(&ctx, "clear_red", render_clear_red, nullptr);
    MEL_ASSERT(result.passed);

    mel_visual_test_shutdown(&ctx);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Visual Clear Color Tests");

    MEL_RUN_TEST(clear_red);

    MEL_TEST_END();

    return MEL_TEST_EXIT_CODE();
}
