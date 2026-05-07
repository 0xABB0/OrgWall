#include "process.h"

extern Mel_Proc mel__cmd_start_process__platform(Mel_Cmd cmd, Mel_Fd *fdin, Mel_Fd *fdout, Mel_Fd *fderr);

Mel_Proc mel__cmd_start_process(Mel_Cmd cmd, Mel_Fd *fdin, Mel_Fd *fdout, Mel_Fd *fderr)
{
    if (cmd.count < 1) {
        nob_log(NOB_ERROR, "Could not run empty command");
        return NOB_INVALID_PROC;
    }

#ifndef NOB_NO_ECHO
    Nob_String_Builder sb = {0};
    nob_cmd_render(cmd, &sb);
    nob_sb_append_null(&sb);
    nob_log(NOB_INFO, "CMD: %s", sb.items);
    nob_sb_free(sb);
    memset(&sb, 0, sizeof(sb));
#endif // NOB_NO_ECHO

    return mel__cmd_start_process__platform(cmd, fdin, fdout, fderr);
}

