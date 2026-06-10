#pragma once

#include <core/types.h>
#include <string/str8.h>
#include <allocator/allocator.h>

#include "tuning.h"

MEL_NODISCARD bool mel_scala_parse(Mel_Tuning* out, const Mel_Alloc* alloc, str8 data, Mel_Hz ref_frequency);

MEL_NODISCARD str8 mel_scala_export(const Mel_Tuning* t, str8 description, const Mel_Alloc* alloc);
