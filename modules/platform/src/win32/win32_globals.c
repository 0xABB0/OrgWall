#include <platform/win32/win32_globals.h>

#include <dbghelp.h>

MEL_CONSTRUCTOR
static void win32_globals_init(void) {
    current_process = GetCurrentProcess();
    current_hinst = GetModuleHandleW(NULL);

    
    SymInitialize(current_process, NULL, TRUE);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
}


MEL_DESTRUCTOR
static void win32_globals_shutdown(void) {
    SymCleanup(current_process);
}