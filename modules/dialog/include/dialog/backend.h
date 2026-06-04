#pragma once

#include <dialog/dialog.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Dialog_Job Mel_Dialog_Job;

#define MEL_DIALOG_REQUEST_OPEN_FILE  (1u << 0)
#define MEL_DIALOG_REQUEST_MULTI      (1u << 1)
#define MEL_DIALOG_REQUEST_SAVE_FILE  (1u << 2)
#define MEL_DIALOG_REQUEST_OPEN_DIR   (1u << 3)

bool mel_dialog__plat_available(void);
void mel_dialog__plat_run(Mel_Dialog_Job* job);

const Mel_Alloc* mel_dialog_job_alloc(const Mel_Dialog_Job* j);
u64              mel_dialog_job_token(const Mel_Dialog_Job* j);
Mel_Dialog_Job*  mel_dialog__job_from_token(u64 token);

u32         mel_dialog_job_request(const Mel_Dialog_Job* j);
Mel_Window  mel_dialog_job_parent(const Mel_Dialog_Job* j);
const char* mel_dialog_job_title(const Mel_Dialog_Job* j);
const char* mel_dialog_job_default_path(const Mel_Dialog_Job* j);
const char* mel_dialog_job_default_name(const Mel_Dialog_Job* j);

u32         mel_dialog_job_filter_count(const Mel_Dialog_Job* j);
const char* mel_dialog_job_filter_label(const Mel_Dialog_Job* j, u32 filter);
u32         mel_dialog_job_filter_pattern_count(const Mel_Dialog_Job* j, u32 filter);
const char* mel_dialog_job_filter_pattern(const Mel_Dialog_Job* j, u32 filter, u32 pattern);

void mel_dialog_job_emit_path(Mel_Dialog_Job* j, const char* path);
void mel_dialog_job_set_chosen_filter(Mel_Dialog_Job* j, u32 filter);
void mel_dialog_job_add_warning(Mel_Dialog_Job* j, Mel_Dialog_Status warn_bits);
void mel_dialog_job_resolve(Mel_Dialog_Job* j, Mel_Dialog_Status s);

#ifdef __cplusplus
}
#endif
