#include <core/platform.h>

#if !MEL_PLATFORM_WINDOWS
#error "win32-only translation unit"
#endif

#include <app/provider.h>
#include <reactor/reactor.h>
#include <log/log.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct
{
    Mel_Reactor* reactor;
    bool         installed;
} Win32_Lifecycle;

static Win32_Lifecycle g_w32;

static void post_will_terminate(void* user)
{
    (void)user;
    mel_app__emit(MEL_APP_PHASE_WILL_TERMINATE);
}

static BOOL WINAPI console_handler(DWORD type)
{
    switch (type)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (g_w32.reactor != NULL)
            mel_reactor_post(g_w32.reactor, post_will_terminate, NULL);
        return TRUE;
    default:
        return FALSE;
    }
}

static void plat_start(void* user)
{
    (void)user;
    if (g_w32.installed)
        return;
    g_w32.reactor = mel_app__reactor();
    if (g_w32.reactor == NULL)
    {
        mel_log_warn("app", "win32 lifecycle: no reactor; console close not wired");
        return;
    }
    if (!SetConsoleCtrlHandler(console_handler, TRUE))
    {
        mel_log_error("app", "win32 lifecycle: SetConsoleCtrlHandler failed");
        return;
    }
    g_w32.installed = true;
}

static void plat_stop(void* user)
{
    (void)user;
    if (!g_w32.installed)
        return;
    SetConsoleCtrlHandler(console_handler, FALSE);
    g_w32.reactor = NULL;
    g_w32.installed = false;
}

void mel_app__register_platform_provider(void)
{
    Mel_App_Provider_Desc desc = { .name = "win32-console", .start = plat_start, .stop = plat_stop };
    mel_app_provider_register(&desc);
}
