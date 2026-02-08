#include "engine.h"
#include "backtrace.h"

void mel__engine_init(void)
{
    mel_backtrace_init();
}

void mel__engine_shutdown(void)
{
}
