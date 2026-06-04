#include <sensor/provider.h>
#include <log/log.h>

#import <Foundation/Foundation.h>

static u32 macos_enumerate(void* user, Mel_Sensor_Raw* out, u32 cap)
{
    (void)user;
    (void)out;
    (void)cap;
    return 0;
}

void mel_sensor__register_host_providers(void)
{
    static const Mel_Sensor_Provider_Desc desc = {
        .name = "macos-none",
        .enumerate = macos_enumerate,
    };
    mel_sensor_provider_register(&desc);
    mel_log_info("sensor", "macos: no built-in IMU; host provider is honest-absent");
}
