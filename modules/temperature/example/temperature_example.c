#include <temperature/temperature.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int near(double a, double b) { return fabs(a - b) < 1e-9; }

int main(void)
{
    Mel_Degrees freezing = mel_degrees_celsius(0.0);
    assert(near(mel_degrees_to_fahrenheit(freezing), 32.0));
    assert(near(mel_degrees_to_kelvin(freezing), 273.15));

    Mel_Degrees boiling = mel_degrees_fahrenheit(212.0);
    assert(near(mel_degrees_to_celsius(boiling), 100.0));
    assert(near(mel_degrees_to_kelvin(boiling), 373.15));

    Mel_Degrees minus_forty = mel_degrees_celsius(-40.0);
    assert(near(mel_degrees_to_fahrenheit(minus_forty), -40.0));

    Mel_Degrees absolute_zero = mel_degrees_kelvin(0.0);
    assert(mel_degrees_is_absolute_zero(absolute_zero));
    assert(!mel_degrees_is_absolute_zero(freezing));
    assert(near(mel_degrees_to_celsius(absolute_zero), -273.15));
    assert(near(mel_degrees_to_fahrenheit(absolute_zero), -459.67));

    Mel_Degrees room = mel_degrees_celsius(20.0);
    Mel_Degrees warm = mel_degrees_celsius(25.0);
    assert(near(mel_degrees_to_kelvin(mel_degrees_sub(warm, room)), 5.0));
    assert(near(mel_degrees_to_celsius(mel_degrees_min(room, warm)), 20.0));
    assert(near(mel_degrees_to_celsius(mel_degrees_max(room, warm)), 25.0));
    assert(near(mel_degrees_to_celsius(mel_degrees_midpoint(room, warm)), 22.5));

    printf("temperature units\n");
    printf("  0 C   = %6.2f F = %7.2f K\n", mel_degrees_to_fahrenheit(freezing), mel_degrees_to_kelvin(freezing));
    printf("  100 C = %6.2f F = %7.2f K\n", mel_degrees_to_fahrenheit(boiling), mel_degrees_to_kelvin(boiling));
    printf("  -40 C = %6.2f F (the shared point)\n", mel_degrees_to_fahrenheit(minus_forty));
    printf("  36.6 C body = %6.2f F\n", mel_degrees_to_fahrenheit(mel_degrees_celsius(36.6)));
    printf("temperature-example: all assertions passed\n");
    return 0;
}
