#include <audiocapture/audiocapture.h>

#import <AVFoundation/AVFoundation.h>

bool mel_audiocapture_authorized(void)
{
    return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio] == AVAuthorizationStatusAuthorized;
}

bool mel_audiocapture_auth_determined(void)
{
    return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio] != AVAuthorizationStatusNotDetermined;
}
