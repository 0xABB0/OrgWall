#pragma once

#include "core.types.h"

typedef struct Mel_Event_Channel Mel_Event_Channel;
typedef void (*Mel_Event_Fn)(void* ctx, const void* event);
typedef struct { u32 id; } Mel_Event_Sub;
#define MEL_EVENT_SUB_NULL ((Mel_Event_Sub){0})
