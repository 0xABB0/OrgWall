#pragma once

#include <string/str8.h>
#include "graphical_app.h"

typedef struct Mel_Vat Mel_Vat;

void gpu_host_init(Mel_Vat* vat);

void gpu_host_open(const Graphical_App* app);

void gpu_host_set_status(str8 text);
