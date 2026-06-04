#include <shell/backend.h>
#include <allocator/allocator.h>
#include <log/log.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

bool mel_shell__plat_available(void) { return true; }

static wchar_t* widen(const Mel_Alloc* a, str8 s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, NULL, 0);
    if (n < 0)
        n = 0;
    wchar_t* w = (wchar_t*)mel_alloc(a, (usize)(n + 1) * sizeof(wchar_t));
    if (!w)
        return NULL;
    if (n)
        MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, w, n);
    w[n] = 0;
    return w;
}

static Mel_Shell_Status status_from_hinstance(HINSTANCE r)
{
    if ((INT_PTR)r > 32)
        return MEL_SHELL_OK;
    switch ((INT_PTR)r)
    {
    case SE_ERR_FNF:
    case SE_ERR_PNF:
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_NOT_FOUND;
    case SE_ERR_ACCESSDENIED:
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_DENIED;
    case SE_ERR_NOASSOC:
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_HANDLER;
    default:
        return MEL_SHELL_ERROR | MEL_SHELL_RESULT_SPAWN_FAIL;
    }
}

void mel_shell__plat_open_url(Mel_Shell_Job* job)
{
    const Mel_Alloc* a = mel_shell_job_alloc(job);
    wchar_t*         w = widen(a, mel_shell_job_target(job));
    if (!w)
    {
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_BAD_TARGET);
        return;
    }
    HINSTANCE r = ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
    mel_dealloc(a, w);
    mel_shell_job_resolve(job, status_from_hinstance(r));
}

void mel_shell__plat_reveal_path(Mel_Shell_Job* job)
{
    const Mel_Alloc* a = mel_shell_job_alloc(job);
    str8             target = mel_shell_job_target(job);
    wchar_t*         wpath = widen(a, target);
    if (!wpath)
    {
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_BAD_TARGET);
        return;
    }
    DWORD attr = GetFileAttributesW(wpath);
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        mel_dealloc(a, wpath);
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_NOT_FOUND);
        return;
    }
    usize    plen = wcslen(wpath);
    wchar_t* args = (wchar_t*)mel_alloc(a, (plen + 16) * sizeof(wchar_t));
    if (!args)
    {
        mel_dealloc(a, wpath);
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_SPAWN_FAIL);
        return;
    }
    swprintf(args, plen + 16, L"/select,\"%ls\"", wpath);
    HINSTANCE r = ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
    mel_dealloc(a, args);
    mel_dealloc(a, wpath);
    mel_shell_job_resolve(job, status_from_hinstance(r));
}

void* mel_shell__plat_native(void) { return NULL; }
