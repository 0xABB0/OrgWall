#include <sensor.power/power.h>

#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <CoreFoundation/CoreFoundation.h>

Mel_Sensor_Power_Source mel_sensor_power_source_current(void)
{
    CFTypeRef blob = IOPSCopyPowerSourcesInfo();
    if (!blob) return MEL_SENSOR_POWER_SOURCE_UNKNOWN;

    CFStringRef providing = IOPSGetProvidingPowerSourceType(blob);
    Mel_Sensor_Power_Source source = MEL_SENSOR_POWER_SOURCE_UNKNOWN;
    if (providing) {
        if (CFEqual(providing, CFSTR(kIOPSACPowerValue)))
            source = MEL_SENSOR_POWER_SOURCE_AC;
        else if (CFEqual(providing, CFSTR(kIOPSBatteryPowerValue)))
            source = MEL_SENSOR_POWER_SOURCE_BATTERY;
    }
    CFRelease(blob);
    return source;
}
