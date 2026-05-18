#include <process/process.h>
#include <string/str8.h>

#ifndef MEL_NO_ECHO
static void mel__cmd_echo(Mel_Cmd cmd)
{
    Mel_String_Builder sb = {0};
    for (usize i = 0; i < cmd.count; i++) {
        const char *arg = cmd.items[i];
        if (!arg) break;
        if (i > 0) mel_array_push(&sb, ' ');
        bool needs_quote = strchr(arg, ' ') != NULL;
        if (needs_quote) mel_array_push(&sb, '\'');
        mel_sb_append_cstr(&sb, arg);
        if (needs_quote) mel_array_push(&sb, '\'');
    }
    mel_sb_append_null(&sb);
    mel_log_info("process", "CMD: %s", sb.items);
    mel_sb_free(&sb);
}
#endif

Mel_Proc mel__cmd_start_process(Mel_Cmd cmd, Mel_Fd *fdin, Mel_Fd *fdout, Mel_Fd *fderr)
{
    if (cmd.count < 1) {
        mel_log_error("process", "Could not run empty command");
        return MEL_INVALID_PROC;
    }

#ifndef MEL_NO_ECHO
    mel__cmd_echo(cmd);
#endif

    return mel__cmd_start_process__platform(cmd, fdin, fdout, fderr);
}

bool mel_cmd_run_opt(Mel_Cmd *cmd, Mel_Cmd_Opt opt)
{
    bool result = true;
    Mel_Fd fdin  = MEL_INVALID_FD;
    Mel_Fd fdout = MEL_INVALID_FD;
    Mel_Fd fderr = MEL_INVALID_FD;
    Mel_Fd *opt_fdin  = NULL;
    Mel_Fd *opt_fdout = NULL;
    Mel_Fd *opt_fderr = NULL;

    usize max_procs = opt.max_procs > 0 ? opt.max_procs : (usize)mel_nprocs() + 1;

    if (opt.async && max_procs > 0) {
        while (opt.async->count >= max_procs) {
            for (usize i = 0; i < opt.async->count; i++) {
                i32 ret = mel__proc_wait_async(opt.async->items[i], 1);
                if (ret < 0) return false;
                if (ret) {
                    mel_da_remove_unordered(opt.async, i);
                    break;
                }
            }
        }
    }

    if (opt.stdin_path) {
        fdin = mel_fd_open_for_read(opt.stdin_path);
        if (fdin == MEL_INVALID_FD) return false;
        opt_fdin = &fdin;
    }
    if (opt.stdout_path) {
        fdout = mel_fd_open_for_write(opt.stdout_path);
        if (fdout == MEL_INVALID_FD) return false;
        opt_fdout = &fdout;
    }
    if (opt.stderr_path) {
        fderr = mel_fd_open_for_write(opt.stderr_path);
        if (fderr == MEL_INVALID_FD) return false;
        opt_fderr = &fderr;
    }

    Mel_Proc proc = mel__cmd_start_process(*cmd, opt_fdin, opt_fdout, opt_fderr);

    if (opt.async) {
        if (proc == MEL_INVALID_PROC) return false;
        mel_da_append(opt.async, proc);
    } else {
        if (!mel_proc_wait(proc)) result = false;
    }

    if (opt_fdin)  mel_fd_close(*opt_fdin);
    if (opt_fdout) mel_fd_close(*opt_fdout);
    if (opt_fderr) mel_fd_close(*opt_fderr);
    if (!opt.dont_reset) cmd->count = 0;
    return result;
}

bool mel_procs_wait(Mel_Procs procs)
{
    bool success = true;
    for (usize i = 0; i < procs.count; i++) {
        success = mel_proc_wait(procs.items[i]) && success;
    }
    return success;
}

bool mel_procs_flush(Mel_Procs *procs)
{
    bool success = mel_procs_wait(*procs);
    procs->count = 0;
    return success;
}
