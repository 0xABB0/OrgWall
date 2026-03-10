#include "mugen_cns_parse.h"

static void noop_exec(Mugen_State_Controller* sc, Mugen_Char_State* state) { (void)sc; (void)state; }

__attribute__((constructor))
static void register_stubs(void)
{
    mugen_sc_register(MUGEN_SC_NULL, "forcefeedback", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "makedust", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "explod", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "palfx", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "remappal", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "screenbound", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "envshake", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "hitoverride", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "reversaldef", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "pause", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "projectile", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "gamemakeanim", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "displaytoclipboard", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_NULL, "appendtoclipboard", NULL, noop_exec);
}
