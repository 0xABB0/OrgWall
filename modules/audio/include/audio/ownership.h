#pragma once

#include <core/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    MEL_AUDIO_OWNERSHIP_OWNED = 0,
    MEL_AUDIO_OWNERSHIP_BORROWED = 1,
} Mel_Audio_Ownership;

#ifdef __cplusplus
}
#endif
