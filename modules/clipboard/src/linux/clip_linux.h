#pragma once

#include <clipboard/backend.h>

bool  mel_clip__x11_init(void);
void  mel_clip__x11_shutdown(void);
bool  mel_clip__x11_owns(Mel_Clip_Channel ch);
u64   mel_clip__x11_sequence(Mel_Clip_Channel ch);
void  mel_clip__x11_read(Mel_Clip_Job* job);
void  mel_clip__x11_write(Mel_Clip_Job* job);
void  mel_clip__x11_clear(Mel_Clip_Job* job);
void  mel_clip__x11_query(Mel_Clip_Job* job);
void  mel_clip__x11_has(Mel_Clip_Job* job);
void* mel_clip__x11_native(void);

bool  mel_clip__wl_init(void);
void  mel_clip__wl_shutdown(void);
u64   mel_clip__wl_sequence(Mel_Clip_Channel ch);
void  mel_clip__wl_read(Mel_Clip_Job* job);
void  mel_clip__wl_write(Mel_Clip_Job* job);
void  mel_clip__wl_clear(Mel_Clip_Job* job);
void  mel_clip__wl_query(Mel_Clip_Job* job);
void  mel_clip__wl_has(Mel_Clip_Job* job);
void* mel_clip__wl_native(void);
