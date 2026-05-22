#include <core/compiler.h>

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dbghelp.h>

#include <platform/win32/win32_globals.h>

HANDLE current_process;
HINSTANCE current_hinst;

// TODO: this stacktrace init seems like it's going to be a pain in the ass if it's always enabled like this. maybe we should have a way to not get initialized?

MEL_CONSTRUCTOR
static void win32_globals_init(void) {
    current_process = GetCurrentProcess();
    current_hinst = GetModuleHandleW(NULL);

    
    //SymInitialize(current_process, NULL, TRUE);
    //SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
}


MEL_DESTRUCTOR
static void win32_globals_shutdown(void) {
    //SymCleanup(current_process);
}