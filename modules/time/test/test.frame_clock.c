#include <time/frame_clock.h>
#include <time/stopwatch.h>
#include <test/test.h>

MEL_TEST(stopwatch, elapsed_and_restart)
{
    Mel_Stopwatch sw = { .origin = 1000 };
    MEL_EXPECT_EQ(mel_stopwatch_elapsed_at(&sw, 1000 + mel_dur_ms(5)), mel_dur_ms(5));
    Mel_Duration e = mel_stopwatch_restart_at(&sw, 1000 + mel_dur_ms(5));
    MEL_EXPECT_EQ(e, mel_dur_ms(5));
    MEL_EXPECT_EQ(sw.origin, (mel_nanosec)(1000 + mel_dur_ms(5)));
    MEL_EXPECT_EQ(mel_stopwatch_elapsed_at(&sw, 1000 + mel_dur_ms(5)), 0);
}

MEL_TEST(frame_clock, first_tick_is_zero)
{
    Mel_Frame_Clock fc;
    mel_frame_clock_init_at(&fc, 0, 0, 0.0);
    MEL_EXPECT_EQ(mel_frame_clock_tick_at(&fc, 0), 0);
    MEL_EXPECT_EQ(fc.frame, 1u);
}

MEL_TEST(frame_clock, raw_dt_tracks_delta)
{
    Mel_Frame_Clock fc;
    mel_frame_clock_init_at(&fc, 0, 0, 0.0);
    MEL_EXPECT_EQ(mel_frame_clock_tick_at(&fc, mel_dur_ms(16)), mel_dur_ms(16));
    MEL_EXPECT_EQ(fc.raw_dt, mel_dur_ms(16));
    MEL_EXPECT_EQ(mel_frame_clock_tick_at(&fc, mel_dur_ms(33)), mel_dur_ms(17));
}

MEL_TEST(frame_clock, clamps_to_max_dt)
{
    Mel_Frame_Clock fc;
    mel_frame_clock_init_at(&fc, 0, mel_dur_ms(100), 0.0);
    Mel_Duration dt = mel_frame_clock_tick_at(&fc, mel_dur_secs(5));
    MEL_EXPECT_EQ(dt, mel_dur_ms(100));
}

MEL_TEST(frame_clock, backwards_clock_clamps_to_zero)
{
    Mel_Frame_Clock fc;
    mel_frame_clock_init_at(&fc, mel_dur_secs(10), 0, 0.0);
    MEL_EXPECT_EQ(mel_frame_clock_tick_at(&fc, mel_dur_secs(9)), 0);
}

MEL_TEST(frame_clock, smoothing_pass_through_when_zero)
{
    Mel_Frame_Clock fc;
    mel_frame_clock_init_at(&fc, 0, 0, 0.0);
    mel_frame_clock_tick_at(&fc, mel_dur_ms(16));
    mel_frame_clock_tick_at(&fc, mel_dur_ms(48));
    MEL_EXPECT_EQ(fc.smooth_dt, fc.raw_dt);
}

MEL_TEST(frame_clock, smoothing_blends_history)
{
    Mel_Frame_Clock fc;
    mel_frame_clock_init_at(&fc, 0, 0, 0.5);
    mel_frame_clock_tick_at(&fc, mel_dur_ms(10));
    mel_frame_clock_tick_at(&fc, mel_dur_ms(30));
    MEL_EXPECT_EQ(fc.smooth_dt, mel_dur_ms(15));
}
