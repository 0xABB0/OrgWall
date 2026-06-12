#pragma once

#include <core/platform.h>
#include <core/types.h>

#define MEL_AUDIOPOLICY_ANDROID_MODE_NORMAL                   0
#define MEL_AUDIOPOLICY_ANDROID_MODE_IN_COMMUNICATION         3

#define MEL_AUDIOPOLICY_ANDROID_USAGE_MEDIA                   1
#define MEL_AUDIOPOLICY_ANDROID_USAGE_VOICE_COMMUNICATION     2

#define MEL_AUDIOPOLICY_ANDROID_CONTENT_TYPE_SPEECH           1
#define MEL_AUDIOPOLICY_ANDROID_CONTENT_TYPE_MUSIC            2

#define MEL_AUDIOPOLICY_ANDROID_FOCUS_GAIN                    1
#define MEL_AUDIOPOLICY_ANDROID_FOCUS_GAIN_TRANSIENT          2
#define MEL_AUDIOPOLICY_ANDROID_FOCUS_GAIN_TRANSIENT_MAY_DUCK 3

#define MEL_AUDIOPOLICY_ANDROID_FOCUS_LOSS                    (-1)
#define MEL_AUDIOPOLICY_ANDROID_FOCUS_LOSS_TRANSIENT          (-2)
#define MEL_AUDIOPOLICY_ANDROID_FOCUS_LOSS_TRANSIENT_CAN_DUCK (-3)

#define MEL_AUDIOPOLICY_ANDROID_REQUEST_FAILED                0
#define MEL_AUDIOPOLICY_ANDROID_REQUEST_GRANTED               1
#define MEL_AUDIOPOLICY_ANDROID_REQUEST_DELAYED               2

bool mel_audiopolicy_android__jni_set_mode(i32 mode);
i32  mel_audiopolicy_android__jni_request_focus(i32 gain_type, i32 usage, i32 content_type, bool pause_when_ducked);
void mel_audiopolicy_android__jni_abandon_focus(void);
bool mel_audiopolicy_android__jni_speaker_communication_device(void);
void mel_audiopolicy_android__jni_clear_communication_device(void);
void mel_audiopolicy_android__on_focus_change(i32 change);
