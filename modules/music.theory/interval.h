#pragma once

#include <stdint.h>


typedef uint8_t Mel_Interval;

/*
  An interval class is a signed count inside of a musical system.
  In 12-TET, this is equivalent to the number of semitones between two notes (excluding octaves. in 12-TET, C + 11 = B. B + 6 = F#)
*/

