#include <test/test.h>

#include <math.h>
#include <pitchdetect/pitchdetect.h>
#include <allocator/heap.h>

#define SAMPLE_RATE 48000u
#define WINDOW      2048u

static Mel_PitchDetector_Opt opt(void)
{
    return (Mel_PitchDetector_Opt){
        .sample_rate = SAMPLE_RATE,
        .window_size = WINDOW,
        .min_hz = 50.0,
        .max_hz = 1500.0,
        .threshold = 0.15f,
    };
}

static void fill_sine(f32* out, u32 count, f64 hz, f64 amplitude)
{
    for (u32 i = 0; i < count; i++)
        out[i] = (f32)(amplitude * sin(2.0 * M_PI * hz * (f64)i / (f64)SAMPLE_RATE));
}

static void fill_sawtooth(f32* out, u32 count, f64 hz)
{
    for (u32 i = 0; i < count; i++)
    {
        f64 t = (f64)i * hz / (f64)SAMPLE_RATE;
        out[i] = (f32)(2.0 * (t - floor(t + 0.5)));
    }
}

static f64 cents_off(f64 detected, f64 expected) { return fabs(1200.0 * log2(detected / expected)); }

MEL_TEST(pitchdetect, pure_sine)
{
    Mel_PitchDetector d = mel_pitch_detector_make(mel_alloc_heap(), opt());
    f32               buf[WINDOW];

    fill_sine(buf, WINDOW, 440.0, 0.8);
    Mel_Pitch_Estimate e = mel_pitch_detect(&d, buf, WINDOW);
    MEL_REQUIRE(e.voiced);
    MEL_EXPECT_LT(cents_off(e.frequency_hz, 440.0), 1.0);
    MEL_EXPECT_GT(e.clarity, 0.9f);

    fill_sine(buf, WINDOW, 261.6256, 0.8);
    e = mel_pitch_detect(&d, buf, WINDOW);
    MEL_REQUIRE(e.voiced);
    MEL_EXPECT_LT(cents_off(e.frequency_hz, 261.6256), 1.0);

    mel_pitch_detector_free(&d);
}

MEL_TEST(pitchdetect, sawtooth_harmonics)
{
    Mel_PitchDetector d = mel_pitch_detector_make(mel_alloc_heap(), opt());
    f32               buf[WINDOW];

    fill_sawtooth(buf, WINDOW, 220.0);
    Mel_Pitch_Estimate e = mel_pitch_detect(&d, buf, WINDOW);
    MEL_REQUIRE(e.voiced);
    MEL_EXPECT_LT(cents_off(e.frequency_hz, 220.0), 2.0);

    mel_pitch_detector_free(&d);
}

MEL_TEST(pitchdetect, low_amplitude)
{
    Mel_PitchDetector d = mel_pitch_detector_make(mel_alloc_heap(), opt());
    f32               buf[WINDOW];

    fill_sine(buf, WINDOW, 440.0, 0.01);
    Mel_Pitch_Estimate e = mel_pitch_detect(&d, buf, WINDOW);
    MEL_REQUIRE(e.voiced);
    MEL_EXPECT_LT(cents_off(e.frequency_hz, 440.0), 1.0);

    mel_pitch_detector_free(&d);
}

MEL_TEST(pitchdetect, silence_unvoiced)
{
    Mel_PitchDetector d = mel_pitch_detector_make(mel_alloc_heap(), opt());
    f32               buf[WINDOW] = { 0 };

    Mel_Pitch_Estimate e = mel_pitch_detect(&d, buf, WINDOW);
    MEL_EXPECT(!e.voiced);
    MEL_EXPECT_EQ(e.frequency_hz, 0.0);

    mel_pitch_detector_free(&d);
}

MEL_TEST(pitchdetect, noise_unvoiced)
{
    Mel_PitchDetector d = mel_pitch_detector_make(mel_alloc_heap(), opt());
    f32               buf[WINDOW];

    u32 state = 0x12345678u;
    for (u32 i = 0; i < WINDOW; i++)
    {
        state = state * 1664525u + 1013904223u;
        buf[i] = ((f32)(state >> 8) / (f32)(1u << 24)) * 2.0f - 1.0f;
    }

    Mel_Pitch_Estimate e = mel_pitch_detect(&d, buf, WINDOW);
    MEL_EXPECT(!e.voiced);

    mel_pitch_detector_free(&d);
}

MEL_TEST(pitchdetect, successive_windows)
{
    Mel_PitchDetector d = mel_pitch_detector_make(mel_alloc_heap(), opt());
    f32               buf[WINDOW];

    fill_sine(buf, WINDOW, 440.0, 0.8);
    Mel_Pitch_Estimate e1 = mel_pitch_detect(&d, buf, WINDOW);

    fill_sine(buf, WINDOW, 523.2511, 0.8);
    Mel_Pitch_Estimate e2 = mel_pitch_detect(&d, buf, WINDOW);

    MEL_REQUIRE(e1.voiced && e2.voiced);
    MEL_EXPECT_LT(cents_off(e1.frequency_hz, 440.0), 1.0);
    MEL_EXPECT_LT(cents_off(e2.frequency_hz, 523.2511), 1.0);

    mel_pitch_detector_free(&d);
}
