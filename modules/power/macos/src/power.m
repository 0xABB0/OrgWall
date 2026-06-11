#include <power/power.h>

#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>

Mel_Power_Source mel_power_source_current(void)
{
    CFTypeRef blob = IOPSCopyPowerSourcesInfo();
    if (!blob)
        return MEL_POWER_SOURCE_UNKNOWN;

    CFStringRef      providing = IOPSGetProvidingPowerSourceType(blob);
    Mel_Power_Source source = MEL_POWER_SOURCE_UNKNOWN;
    if (providing)
    {
        if (CFEqual(providing, CFSTR(kIOPSACPowerValue)))
            source = MEL_POWER_SOURCE_AC;
        else if (CFEqual(providing, CFSTR(kIOPSBatteryPowerValue)))
            source = MEL_POWER_SOURCE_BATTERY;
    }
    CFRelease(blob);
    return source;
}

Mel_Power_Battery mel_power_battery_current(void)
{
    Mel_Power_Battery out = { false, false, 0.0f, -1.0f, -1.0f };

    CFTypeRef blob = IOPSCopyPowerSourcesInfo();
    if (!blob)
        return out;
    CFArrayRef list = IOPSCopyPowerSourcesList(blob);
    if (list)
    {
        CFIndex n = CFArrayGetCount(list);
        for (CFIndex i = 0; i < n; i++)
        {
            CFDictionaryRef d = IOPSGetPowerSourceDescription(blob, CFArrayGetValueAtIndex(list, i));
            if (!d)
                continue;
            CFStringRef type = CFDictionaryGetValue(d, CFSTR(kIOPSTypeKey));
            if (!type || !CFEqual(type, CFSTR(kIOPSInternalBatteryType)))
                continue;

            out.present = true;

            CFNumberRef cur = CFDictionaryGetValue(d, CFSTR(kIOPSCurrentCapacityKey));
            CFNumberRef max = CFDictionaryGetValue(d, CFSTR(kIOPSMaxCapacityKey));
            int         curv = 0, maxv = 0;
            if (cur && max && CFNumberGetValue(cur, kCFNumberIntType, &curv) && CFNumberGetValue(max, kCFNumberIntType, &maxv) && maxv > 0)
                out.level = (f32)curv / (f32)maxv;

            CFBooleanRef charging = CFDictionaryGetValue(d, CFSTR(kIOPSIsChargingKey));
            out.charging = (charging == kCFBooleanTrue);

            CFNumberRef te = CFDictionaryGetValue(d, CFSTR(kIOPSTimeToEmptyKey));
            CFNumberRef tf = CFDictionaryGetValue(d, CFSTR(kIOPSTimeToFullChargeKey));
            int         tev = -1, tfv = -1;
            if (te && CFNumberGetValue(te, kCFNumberIntType, &tev) && tev > 0)
                out.seconds_to_empty = (f32)tev * 60.0f;
            if (tf && CFNumberGetValue(tf, kCFNumberIntType, &tfv) && tfv > 0)
                out.seconds_to_full = (f32)tfv * 60.0f;
            break;
        }
        CFRelease(list);
    }
    CFRelease(blob);
    return out;
}

Mel_Power_Caps mel_power_caps(void)
{
    NSProcessInfo*    info = [NSProcessInfo processInfo];
    Mel_Power_Battery b = mel_power_battery_current();
    return (Mel_Power_Caps) { .power_source_present = true, .profile_present = [info respondsToSelector:@selector(isLowPowerModeEnabled)], .battery_present = b.present, };
}
