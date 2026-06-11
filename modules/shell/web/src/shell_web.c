#include <shell/backend.h>
#include <string/str8.h>
#include <log/log.h>

#include <emscripten.h>

EM_JS(int, mel_shell_js_available, (void), { return (typeof window != "undefined" && typeof window.open == "function") ? 1 : 0; })

EM_JS(int, mel_shell_js_open, (const char* ptr, int len), {
    if (typeof window == "undefined" || typeof window.open != "function")
        return 0;
    var s = ptr ? UTF8ToString(ptr, len) : "";
    if (!s)
        return 2;
    try
    {
        var w = window.open(s, "_blank", "noopener");
        return w ? 1 : 3;
    }
    catch(e) { return 3; }
})

bool mel_shell__plat_available(void) { return mel_shell_js_available() != 0; }

void mel_shell__plat_open_url(Mel_Shell_Job* job)
{
    str8 t = mel_shell_job_target(job);
    int  r = mel_shell_js_open((const char*)t.data, (int)t.len);
    switch (r)
    {
    case 1:
        mel_shell_job_resolve(job, MEL_SHELL_OK);
        break;
    case 2:
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_BAD_TARGET);
        break;
    case 3:
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_DENIED);
        break;
    default:
        mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_BACKEND);
        break;
    }
}

void mel_shell__plat_reveal_path(Mel_Shell_Job* job)
{
    mel_log_warn("shell", "reveal_path: the browser sandbox has no file manager");
    mel_shell_job_resolve(job, MEL_SHELL_ERROR | MEL_SHELL_RESULT_NO_HANDLER);
}

void* mel_shell__plat_native(void) { return NULL; }
