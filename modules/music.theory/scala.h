#pragma once

#include "tuning.h"
#include <stdint.h>

Mel_Tuning mel_scala_parse_to_tuning(const char* data, Mel_Hz ref_frequency);

char* mel_scala_export(const Mel_Tuning* t, const char* description);
