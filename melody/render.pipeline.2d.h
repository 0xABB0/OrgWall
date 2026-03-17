#pragma once

#include "event.channel.fwd.h"

typedef struct Mel_Texture_Table Mel_Texture_Table;

extern Mel_Event_Channel mel_pipeline_2d_ready;

void mel_pipeline_2d_set_texture_table(Mel_Texture_Table* tt);
