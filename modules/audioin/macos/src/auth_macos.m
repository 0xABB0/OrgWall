#include "audioin_macos_internal.h"

#import <AVFoundation/AVFoundation.h>

const mel_audioin_auth* mel_audioin__macos_authorization(void)
{
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio])
    {
    case AVAuthorizationStatusAuthorized:
        return &mel_audioin_auth_granted;
    case AVAuthorizationStatusDenied:
        return &mel_audioin_auth_denied;
    case AVAuthorizationStatusRestricted:
        return &mel_audioin_auth_restricted;
    default:
        return &mel_audioin_auth_not_determined;
    }
}

void mel_audioin__macos_authorize(Mel_AudioIn_Sink sink)
{
    if (sink.on_auth == NULL)
        return;
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted) {
                                 sink.on_auth(sink.token, granted ? &mel_audioin_auth_granted : &mel_audioin_auth_denied);
                             }];
}
