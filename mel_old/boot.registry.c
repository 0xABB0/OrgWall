#include "boot.registry.h"
#include "core.types.h"
#include <assert.h>

#define MEL_BOOT_WIRE_CAP 32

static Mel_Boot_Wire_Fn s_wires[MEL_BOOT_WIRE_CAP];
static u32 s_wire_count;

void mel__boot_register_wire(Mel_Boot_Wire_Fn fn)
{
    assert(fn != NULL);
    assert(s_wire_count < MEL_BOOT_WIRE_CAP);
    s_wires[s_wire_count++] = fn;
}

void mel__boot_run_wires(void)
{
    for (u32 i = 0; i < s_wire_count; i++)
        s_wires[i]();
}

u32 mel__boot_wire_count(void)
{
    return s_wire_count;
}
