#pragma once

#include "pitch.h"
#include "cent.h"

typedef struct Mel_Imperfect_Pitch Mel_Imperfect_Pitch;

struct Mel_Imperfect_Pitch
{
  Mel_Pitch pitch;
  Mel_Cent adjustment;
};


