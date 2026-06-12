#pragma once

#include <audioin/audioin.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Future Mel_Future;

typedef struct mel_audioin_auth mel_audioin_auth;

extern const mel_audioin_auth mel_audioin_auth_granted;
extern const mel_audioin_auth mel_audioin_auth_denied;
extern const mel_audioin_auth mel_audioin_auth_not_determined;
extern const mel_audioin_auth mel_audioin_auth_restricted;

const char* mel_audioin_auth_name(const mel_audioin_auth* a);
bool        mel_audioin_auth_is_granted(const mel_audioin_auth* a);

const mel_audioin_auth* mel_audioin_authorization(void);
Mel_Future*             mel_audioin_authorize(const Mel_Alloc* a);
const mel_audioin_auth* mel_audioin_future_auth(const Mel_Future* f);
void                    mel_audioin_future_free(Mel_Future* f);

#ifdef __cplusplus
}
#endif
