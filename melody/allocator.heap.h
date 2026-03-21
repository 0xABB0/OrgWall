#pragma once

#include "allocator.fwd.h"
#include "allocator.guard.cfg.h"
#include "core.types.h"

const Mel_Alloc* mel_alloc_heap(void);
u32              mel_alloc_heap_memory_debug_level(void);
