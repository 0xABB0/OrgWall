#include "../melody/test.harness.h"
#include "../melody/test.visual.h"

static void render_clear_red(Mel_Render_Pass_Ctx* ctx)
{
    (void)ctx;
}

MEL_TEST(clear_red, .tags = "visual")
{
    Mel_Visual_Test_Ctx ctx = {0};
    MEL_ASSERT(mel_visual_test_init(&ctx, .width = 64, .height = 64));

    Mel_Visual_Test_Result result = mel_visual_test_check(&ctx, "clear_red", render_clear_red,
        .clear_r = 1.0f, .clear_g = 0.0f, .clear_b = 0.0f, .clear_a = 1.0f);
    MEL_ASSERT(result.passed);

    mel_visual_test_shutdown(&ctx);
}
