#include "dialog_linux.h"

#include <dialog/backend.h>
#include <log/log.h>

bool mel_dialog__plat_available(void) { return true; }

void mel_dialog__plat_run(Mel_Dialog_Job* job)
{
    if (mel_dialog_job_parent(job).index != 0)
        mel_dialog_job_add_warning(job, MEL_DIALOG_WARN_PARENT_IGNORED);

    if (mel_dialog__portal_run(job))
        return;
    mel_log_warn("dialog", "linux: xdg-desktop-portal unavailable, falling back to GTK");
    if (mel_dialog__gtk_run(job))
        return;
    mel_log_error("dialog", "linux: neither xdg-desktop-portal nor GTK could service the request");
    mel_dialog_job_resolve(job, MEL_DIALOG_ERROR | MEL_DIALOG_NO_BACKEND);
}
