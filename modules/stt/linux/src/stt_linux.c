#include <stt/provider.h>

#include <log/log.h>

void mel_stt__register_host_providers(void)
{
    mel_log_info("stt", "no blessed host recognition engine on linux; registry starts empty");
}
