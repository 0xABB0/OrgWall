#pragma once

#include <core/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    MEL_MIXER_OWNERSHIP_OWNED = 0,
    MEL_MIXER_OWNERSHIP_BORROWED = 1,
} Mel_Mixer_Ownership;

#ifdef __cplusplus
}
#endif
