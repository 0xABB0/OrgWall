#pragma once

#include <speech/common.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct mel_speech_auth
{
    const char* name;
    bool        granted;
};

#ifdef __cplusplus
}
#endif
