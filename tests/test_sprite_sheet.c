#include "../melody/test.harness.h"
#include "../melody/sprite.sheet.h"
#include "../melody/allocator.heap.h"

MEL_TEST(sprite_sheet_push_frame, .tags = "render")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    Mel_Sprite_Sheet sheet;
    mel_sprite_sheet_init(&sheet, heap);

    mel_sprite_sheet_push_frame(&sheet, mel_rect(0.0f, 0.0f, 0.5f, 1.0f));
    mel_sprite_sheet_push_frame(&sheet, mel_rect(0.5f, 0.0f, 0.5f, 1.0f));

    MEL_ASSERT_EQ(sheet.frame_count, 2);

    Mel_Rect f0 = mel_sprite_sheet_frame(&sheet, 0);
    MEL_ASSERT_FLOAT_EQ(f0.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(f0.w, 0.5f, 0.001f);

    Mel_Rect f1 = mel_sprite_sheet_frame(&sheet, 1);
    MEL_ASSERT_FLOAT_EQ(f1.x, 0.5f, 0.001f);

    mel_sprite_sheet_destroy(&sheet);
}

MEL_TEST(sprite_sheet_from_grid_uniform, .tags = "render")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    Mel_Sprite_Sheet sheet;
    mel_sprite_sheet_init(&sheet, heap);
    mel_sprite_sheet_from_grid(&sheet, .cols = 4, .rows = 1);

    MEL_ASSERT_EQ(sheet.frame_count, 4);

    Mel_Rect f0 = mel_sprite_sheet_frame(&sheet, 0);
    MEL_ASSERT_FLOAT_EQ(f0.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(f0.w, 0.25f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(f0.h, 1.0f, 0.001f);

    Mel_Rect f3 = mel_sprite_sheet_frame(&sheet, 3);
    MEL_ASSERT_FLOAT_EQ(f3.x, 0.75f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(f3.w, 0.25f, 0.001f);

    mel_sprite_sheet_destroy(&sheet);
}

MEL_TEST(sprite_sheet_from_grid_multi_row, .tags = "render")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    Mel_Sprite_Sheet sheet;
    mel_sprite_sheet_init(&sheet, heap);
    mel_sprite_sheet_from_grid(&sheet, .cols = 2, .rows = 2);

    MEL_ASSERT_EQ(sheet.frame_count, 4);

    Mel_Rect f0 = mel_sprite_sheet_frame(&sheet, 0);
    MEL_ASSERT_FLOAT_EQ(f0.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(f0.y, 0.0f, 0.001f);

    Mel_Rect f1 = mel_sprite_sheet_frame(&sheet, 1);
    MEL_ASSERT_FLOAT_EQ(f1.x, 0.5f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(f1.y, 0.0f, 0.001f);

    Mel_Rect f2 = mel_sprite_sheet_frame(&sheet, 2);
    MEL_ASSERT_FLOAT_EQ(f2.x, 0.0f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(f2.y, 0.5f, 0.001f);

    Mel_Rect f3 = mel_sprite_sheet_frame(&sheet, 3);
    MEL_ASSERT_FLOAT_EQ(f3.x, 0.5f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(f3.y, 0.5f, 0.001f);

    mel_sprite_sheet_destroy(&sheet);
}

MEL_TEST(sprite_sheet_from_grid_partial, .tags = "render")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    Mel_Sprite_Sheet sheet;
    mel_sprite_sheet_init(&sheet, heap);
    mel_sprite_sheet_from_grid(&sheet, .cols = 4, .rows = 2, .frame_count = 6);

    MEL_ASSERT_EQ(sheet.frame_count, 6);

    Mel_Rect last = mel_sprite_sheet_frame(&sheet, 5);
    MEL_ASSERT_FLOAT_EQ(last.x, 0.25f, 0.001f);
    MEL_ASSERT_FLOAT_EQ(last.y, 0.5f, 0.001f);

    mel_sprite_sheet_destroy(&sheet);
}

MEL_TEST(sprite_sheet_destroy_cleans_up, .tags = "render")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    Mel_Sprite_Sheet sheet;
    mel_sprite_sheet_init(&sheet, heap);
    mel_sprite_sheet_from_grid(&sheet, .cols = 8, .rows = 1);
    MEL_ASSERT_EQ(sheet.frame_count, 8);

    mel_sprite_sheet_destroy(&sheet);
    MEL_ASSERT_EQ(sheet.frame_count, 0);
    MEL_ASSERT_NULL(sheet.frames);
}
