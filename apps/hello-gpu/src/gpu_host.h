#pragma once

#include <reactor/reactor.h>
#include <string/str8.h>
#include "graphical_app.h"

void gpu_host_init(Mel_Reactor* reactor);

void gpu_host_open(const Graphical_App* app);

void gpu_host_set_status(str8 text);
