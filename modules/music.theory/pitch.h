#pragma once

#include <stdint.h>

#include "pitch_class.h"

typedef struct Mel_Pitch Mel_Pitch;


struct Mel_Pitch
{
  Mel_Pitch_Class class;
  uint8_t reg;
};

