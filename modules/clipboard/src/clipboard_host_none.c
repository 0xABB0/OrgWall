#include <clipboard/backend.h>

bool  mel_clip__plat_available(void) { return false; }
u64   mel_clip__plat_sequence(void) { return 0; }
void* mel_clip__plat_native(void) { return NULL; }

void mel_clip__plat_read(Mel_Clip_Job* job) { mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD); }
void mel_clip__plat_write(Mel_Clip_Job* job) { mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD); }
void mel_clip__plat_clear(Mel_Clip_Job* job) { mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD); }
void mel_clip__plat_query(Mel_Clip_Job* job) { mel_clip_job_resolve(job, MEL_CLIP_ERROR | MEL_CLIP_RESULT_NO_CLIPBOARD); }
