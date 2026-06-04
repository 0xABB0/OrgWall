#include <time/sleep.h>
#include <time/nano.h>
#include <test/test.h>

MEL_TEST(sleep, zero_returns_immediately)
{
    MEL_EXPECT_EQ(mel_sleep(0), 0);
    MEL_EXPECT_EQ(mel_sleep(mel_dur_ms(-5)), 0);
    MEL_EXPECT_EQ(mel_busy_wait(0), 0);
    MEL_EXPECT_EQ(mel_busy_wait(mel_dur_ms(-5)), 0);
}

MEL_TEST(sleep, blocks_at_least_requested)
{
    mel_nanosec  before = mel_nanos_since_unspecified_epoch();
    Mel_Duration slept = mel_sleep(mel_dur_ms(10));
    mel_nanosec  after = mel_nanos_since_unspecified_epoch();
    MEL_EXPECT_GE(slept, mel_dur_ms(10));
    MEL_EXPECT_GE((Mel_Duration)(after - before), mel_dur_ms(10));
}

MEL_TEST(sleep, ms_helper_matches)
{
    Mel_Duration slept = mel_sleep_ms(5);
    MEL_EXPECT_GE(slept, mel_dur_ms(5));
}

MEL_TEST(sleep, until_past_deadline_returns_zero)
{
    mel_nanosec now = mel_nanos_since_unspecified_epoch();
    MEL_EXPECT_EQ(mel_sleep_until(now > 1000 ? now - 1000 : 0), 0);
    MEL_EXPECT_EQ(mel_busy_wait_until(now > 1000 ? now - 1000 : 0), 0);
}

MEL_TEST(sleep, busy_wait_spans_requested)
{
    mel_nanosec  before = mel_nanos_since_unspecified_epoch();
    Mel_Duration spun = mel_busy_wait(mel_dur_us(200));
    mel_nanosec  after = mel_nanos_since_unspecified_epoch();
    MEL_EXPECT_GE(spun, mel_dur_us(200));
    MEL_EXPECT_GE((Mel_Duration)(after - before), mel_dur_us(200));
}

MEL_TEST(sleep, until_future_blocks)
{
    mel_nanosec  now = mel_nanos_since_unspecified_epoch();
    mel_nanosec  deadline = now + (mel_nanosec)mel_dur_ms(8);
    Mel_Duration slept = mel_sleep_until(deadline);
    MEL_EXPECT_GE(mel_nanos_since_unspecified_epoch(), deadline);
    MEL_EXPECT_GE(slept, mel_dur_ms(8) - mel_dur_ms(1));
}
