#pragma once

#include "boot.registry.fwd.h"
#include "core.types.h"

void mel__boot_register_wire(Mel_Boot_Wire_Fn fn);
void mel__boot_run_wires(void);
u32  mel__boot_wire_count(void);
