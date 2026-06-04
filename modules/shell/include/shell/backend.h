#pragma once

#include <shell/shell.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Shell_Job Mel_Shell_Job;

bool  mel_shell__plat_available(void);
void  mel_shell__plat_open_url(Mel_Shell_Job* job);
void  mel_shell__plat_reveal_path(Mel_Shell_Job* job);
void* mel_shell__plat_native(void);

const Mel_Alloc* mel_shell_job_alloc(const Mel_Shell_Job* j);
u64              mel_shell_job_token(const Mel_Shell_Job* j);
Mel_Shell_Job*   mel_shell__job_from_token(u64 token);
str8             mel_shell_job_target(const Mel_Shell_Job* j);

void mel_shell_job_add_warning(Mel_Shell_Job* j, Mel_Shell_Status warn_bits);
void mel_shell_job_resolve(Mel_Shell_Job* j, Mel_Shell_Status s);

#ifdef __cplusplus
}
#endif
