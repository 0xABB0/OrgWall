#pragma once

#include "event.channel.fwd.h"
#include "render.manager.2d.fwd.h"

typedef struct Mel_Render_Pipeline Mel_Render_Pipeline;

extern Mel_Event_Channel mel_pipeline_2d_ready;

void mel_pipeline_2d_set_manager(Mel_Render_Pipeline* pipeline, Mel_Render_Manager_2D* mgr);
