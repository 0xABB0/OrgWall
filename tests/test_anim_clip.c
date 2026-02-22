#include "anim.clip.h"
#include "anim.track.h"
#include "allocator.heap.h"
#include "test.harness.h"

MEL_TEST(init_with_tracks, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Clip clip;
    mel_anim_clip_init(&clip, alloc, 0xABCD, 3, 0, 1.0f, false);

    MEL_ASSERT_EQ(clip.track_count, 3u);
    MEL_ASSERT_EQ(clip.name_hash, 0xABCDull);
    MEL_ASSERT_NOT_NULL(clip.tracks);

    mel_anim_clip_destroy(&clip, alloc);
}

MEL_TEST(find_track_hit, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Clip clip;
    mel_anim_clip_init(&clip, alloc, 0, 2, 0, 1.0f, false);

    mel_anim_track_init(&clip.tracks[0], alloc, 100, 1, 1);
    mel_anim_track_init(&clip.tracks[1], alloc, 200, 1, 1);

    Mel_Anim_Track* found = mel_anim_clip_find_track(&clip, 200);
    MEL_ASSERT_NOT_NULL(found);
    MEL_ASSERT_EQ(found->property_id, 200ull);

    mel_anim_clip_destroy(&clip, alloc);
}

MEL_TEST(find_track_miss, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Clip clip;
    mel_anim_clip_init(&clip, alloc, 0, 1, 0, 1.0f, false);

    mel_anim_track_init(&clip.tracks[0], alloc, 100, 1, 1);

    Mel_Anim_Track* found = mel_anim_clip_find_track(&clip, 999);
    MEL_ASSERT_NULL(found);

    mel_anim_clip_destroy(&clip, alloc);
}

MEL_TEST(has_property, .tags = "anim")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Anim_Clip clip;
    mel_anim_clip_init(&clip, alloc, 0, 2, 0, 1.0f, false);

    mel_anim_track_init(&clip.tracks[0], alloc, 10, 1, 1);
    mel_anim_track_init(&clip.tracks[1], alloc, 20, 1, 1);

    MEL_ASSERT(mel_anim_clip_has_property(&clip, 10));
    MEL_ASSERT(mel_anim_clip_has_property(&clip, 20));
    MEL_ASSERT(!mel_anim_clip_has_property(&clip, 30));

    mel_anim_clip_destroy(&clip, alloc);
}