#pragma once

#include <stdint.h> // TODO: use core/types.h

typedef uint64_t mel_nanosec;

static constexpr mel_nanosec MEL_NANOS_PER_SEC = (1000*1000*1000);

// The maximum time span representable is 584 years.
uint64_t mel_nanos_since_unspecified_epoch(void);

