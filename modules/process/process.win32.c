#include "process.h"

#ifdef _WIN32

#ifndef MEL_WIN32_ERR_MSG_SIZE
#define MEL_WIN32_ERR_MSG_SIZE (4 * 1024)
#endif

static char *mel_win32_error_message(DWORD err)
{
    static char msg[MEL_WIN32_ERR_MSG_SIZE] = {0};
    DWORD size = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, err, LANG_USER_DEFAULT, msg, MEL_WIN32_ERR_MSG_SIZE, NULL);

    if (size == 0) {
        if (GetLastError() != ERROR_MR_MID_NOT_FOUND) {
            snprintf(msg, MEL_WIN32_ERR_MSG_SIZE, "Could not get error message for 0x%lX", err);
            return msg;
        } else {
            snprintf(msg, MEL_WIN32_ERR_MSG_SIZE, "Invalid Windows Error code (0x%lX)", err);
            return msg;
        }
    }

    while (size > 1 && isspace(msg[size - 1])) {
        msg[--size] = '\0';
    }

    return msg;
}

static void mel__win32_cmd_quote(Mel_Cmd cmd, char *buf, usize buf_size)
{
    usize off = 0;
    for (usize i = 0; i < cmd.count; i++) {
        const char *arg = cmd.items[i];
        if (!arg) break;
        usize len = strlen(arg);
        if (i > 0 && off < buf_size - 1) buf[off++] = ' ';
        if (len != 0 && strpbrk(arg, " \t\n\v\"") == NULL) {
            for (usize j = 0; j < len && off < buf_size - 1; j++)
                buf[off++] = arg[j];
        } else {
            usize backslashes = 0;
            if (off < buf_size - 1) buf[off++] = '\"';
            for (usize j = 0; j < len; j++) {
                char x = arg[j];
                if (x == '\\') {
                    backslashes += 1;
                } else {
                    if (x == '\"') {
                        for (usize k = 0; k < 1 + backslashes && off < buf_size - 1; k++)
                            buf[off++] = '\\';
                    }
                    backslashes = 0;
                }
                if (off < buf_size - 1) buf[off++] = x;
            }
            for (usize k = 0; k < backslashes && off < buf_size - 1; k++)
                buf[off++] = '\\';
            if (off < buf_size - 1) buf[off++] = '\"';
        }
    }
    buf[off] = '\0';
}

Mel_Proc mel__cmd_start_process__platform(Mel_Cmd cmd, Mel_Fd *fdin, Mel_Fd *fdout, Mel_Fd *fderr)
{
    STARTUPINFO siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(siStartInfo));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError  = fderr ? *fderr : GetStdHandle(STD_ERROR_HANDLE);
    siStartInfo.hStdOutput = fdout ? *fdout : GetStdHandle(STD_OUTPUT_HANDLE);
    siStartInfo.hStdInput  = fdin  ? *fdin  : GetStdHandle(STD_INPUT_HANDLE);
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    char quoted[32768];
    mel__win32_cmd_quote(cmd, quoted, sizeof(quoted));

    BOOL bSuccess = CreateProcessA(NULL, quoted, NULL, NULL, TRUE, 0, NULL, NULL,
                                   &siStartInfo, &piProcInfo);

    if (!bSuccess) {
        mel_log_error("process", "Could not create child process for %s: %s",
                      cmd.items[0], mel_win32_error_message(GetLastError()));
        return MEL_INVALID_PROC;
    }

    CloseHandle(piProcInfo.hThread);

    return piProcInfo.hProcess;
}

i32 mel__proc_wait_async(Mel_Proc proc, i32 ms)
{
    if (proc == MEL_INVALID_PROC) return -1;

    DWORD result = WaitForSingleObject(proc, (DWORD)ms);

    if (result == WAIT_TIMEOUT) {
        return 0;
    }

    if (result == WAIT_FAILED) {
        mel_log_error("process", "could not wait on child process: %s",
                      mel_win32_error_message(GetLastError()));
        return -1;
    }

    DWORD exit_status;
    if (!GetExitCodeProcess(proc, &exit_status)) {
        mel_log_error("process", "could not get process exit code: %s",
                      mel_win32_error_message(GetLastError()));
        return -1;
    }

    if (exit_status != 0) {
        mel_log_error("process", "command exited with exit code %lu", exit_status);
        return -1;
    }

    CloseHandle(proc);

    return 1;
}

bool mel_proc_wait(Mel_Proc proc)
{
    if (proc == MEL_INVALID_PROC) return false;

    DWORD result = WaitForSingleObject(proc, INFINITE);

    if (result == WAIT_FAILED) {
        mel_log_error("process", "could not wait on child process: %s",
                      mel_win32_error_message(GetLastError()));
        return false;
    }

    DWORD exit_status;
    if (!GetExitCodeProcess(proc, &exit_status)) {
        mel_log_error("process", "could not get process exit code: %s",
                      mel_win32_error_message(GetLastError()));
        return false;
    }

    if (exit_status != 0) {
        mel_log_error("process", "command exited with exit code %lu", exit_status);
        return false;
    }

    CloseHandle(proc);

    return true;
}

Mel_Fd mel_fd_open_for_read(const char *path)
{
    SECURITY_ATTRIBUTES saAttr = {0};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    Mel_Fd result = CreateFileA(path, GENERIC_READ, 0, &saAttr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_READONLY, NULL);

    if (result == INVALID_HANDLE_VALUE) {
        mel_log_error("process", "Could not open file %s: %s",
                      path, mel_win32_error_message(GetLastError()));
        return MEL_INVALID_FD;
    }

    return result;
}

Mel_Fd mel_fd_open_for_write(const char *path)
{
    SECURITY_ATTRIBUTES saAttr = {0};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    Mel_Fd result = CreateFileA(path, GENERIC_WRITE, 0, &saAttr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);

    if (result == INVALID_HANDLE_VALUE) {
        mel_log_error("process", "Could not open file %s: %s",
                      path, mel_win32_error_message(GetLastError()));
        return MEL_INVALID_FD;
    }

    return result;
}

void mel_fd_close(Mel_Fd fd)
{
    CloseHandle(fd);
}

i32 mel_nprocs(void)
{
    SYSTEM_INFO siSysInfo;
    GetSystemInfo(&siSysInfo);
    return (i32)siSysInfo.dwNumberOfProcessors;
}

#endif
