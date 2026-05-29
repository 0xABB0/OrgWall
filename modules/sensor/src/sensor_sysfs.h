#pragma once

#include <sensor.thermal/thermal.h>
#include <sensor.power/power.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

static inline bool mel_sysfs_read_str(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    if (n == 0) return false;
    buf[n] = '\0';
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' ' || buf[n - 1] == '\t')) buf[--n] = '\0';
    return true;
}

static inline bool mel_sysfs_read_milli_c(const char *path, f32 *out_c)
{
    char buf[32];
    if (!mel_sysfs_read_str(path, buf, sizeof buf)) return false;
    char *end = NULL;
    long milli = strtol(buf, &end, 10);
    if (end == buf) return false;
    *out_c = (f32)milli / 1000.0f;
    return true;
}

static inline bool mel_sysfs_zone_is_cpu(const char *type)
{
    return strstr(type, "cpu") || strstr(type, "pkg") || strstr(type, "x86")
        || strstr(type, "soc") || strstr(type, "coretemp") || strstr(type, "acpitz");
}

static inline Mel_Sensor_Temperature mel_sysfs_temperature(Mel_Sensor_Temp_Domain domain)
{
    if (domain == MEL_SENSOR_TEMP_DOMAIN_AMBIENT)
        return (Mel_Sensor_Temperature){ .celsius = 0.0f, .fidelity = MEL_SENSOR_TEMP_NONE };

    bool want_gpu = (domain == MEL_SENSOR_TEMP_DOMAIN_GPU);
    f32  matched = 0.0f, hottest = 0.0f;
    bool have_match = false, have_any = false;

    for (int i = 0; i < 64; i++) {
        char tp[64], pp[64], type[64];
        snprintf(tp, sizeof tp, "/sys/class/thermal/thermal_zone%d/type", i);
        snprintf(pp, sizeof pp, "/sys/class/thermal/thermal_zone%d/temp", i);
        if (!mel_sysfs_read_str(tp, type, sizeof type)) break;

        f32 c;
        if (!mel_sysfs_read_milli_c(pp, &c)) continue;
        if (c <= 0.0f || c >= 150.0f) continue;
        have_any = true;
        if (c > hottest) hottest = c;

        bool is_gpu = strstr(type, "gpu") != NULL;
        if (want_gpu ? is_gpu : mel_sysfs_zone_is_cpu(type)) {
            matched = c;
            have_match = true;
        }
    }

    if (have_match) return (Mel_Sensor_Temperature){ .celsius = matched, .fidelity = MEL_SENSOR_TEMP_MEASURED };
    if (want_gpu)   return (Mel_Sensor_Temperature){ .celsius = 0.0f,    .fidelity = MEL_SENSOR_TEMP_NONE };
    if (have_any)   return (Mel_Sensor_Temperature){ .celsius = hottest, .fidelity = MEL_SENSOR_TEMP_DERIVED };
    return (Mel_Sensor_Temperature){ .celsius = 0.0f, .fidelity = MEL_SENSOR_TEMP_NONE };
}

static inline Mel_Sensor_Thermal_Pressure mel_sysfs_tier_from_type(const char *type)
{
    if (strcmp(type, "critical") == 0) return MEL_SENSOR_THERMAL_CRITICAL;
    if (strcmp(type, "hot") == 0)      return MEL_SENSOR_THERMAL_SERIOUS;
    if (strcmp(type, "passive") == 0)  return MEL_SENSOR_THERMAL_FAIR;
    if (strcmp(type, "active") == 0)   return MEL_SENSOR_THERMAL_FAIR;
    return MEL_SENSOR_THERMAL_NOMINAL;
}

static inline Mel_Sensor_Thermal_Pressure mel_sysfs_thermal_tier(void)
{
    int chosen = -1, fallback = -1;
    for (int i = 0; i < 64; i++) {
        char tp[64], type[64];
        snprintf(tp, sizeof tp, "/sys/class/thermal/thermal_zone%d/type", i);
        if (!mel_sysfs_read_str(tp, type, sizeof type)) break;
        if (fallback < 0) fallback = i;
        if (mel_sysfs_zone_is_cpu(type)) { chosen = i; break; }
    }
    if (chosen < 0) chosen = fallback;
    if (chosen < 0) return MEL_SENSOR_THERMAL_UNKNOWN;

    char pp[64];
    f32 now;
    snprintf(pp, sizeof pp, "/sys/class/thermal/thermal_zone%d/temp", chosen);
    if (!mel_sysfs_read_milli_c(pp, &now)) return MEL_SENSOR_THERMAL_UNKNOWN;

    Mel_Sensor_Thermal_Pressure best = MEL_SENSOR_THERMAL_NOMINAL;
    bool any_trip = false;
    for (int j = 0; j < 16; j++) {
        char ttp[80], ttt[80], type[32];
        f32 trip_c;
        snprintf(ttp, sizeof ttp, "/sys/class/thermal/thermal_zone%d/trip_point_%d_temp", chosen, j);
        snprintf(ttt, sizeof ttt, "/sys/class/thermal/thermal_zone%d/trip_point_%d_type", chosen, j);
        if (!mel_sysfs_read_milli_c(ttp, &trip_c)) break;
        any_trip = true;
        if (trip_c <= 0.0f) continue;
        if (now >= trip_c && mel_sysfs_read_str(ttt, type, sizeof type)) {
            Mel_Sensor_Thermal_Pressure t = mel_sysfs_tier_from_type(type);
            if (t > best) best = t;
        }
    }
    if (!any_trip) return MEL_SENSOR_THERMAL_UNKNOWN;
    return best;
}

static inline bool mel_sysfs_thermal_present(void)
{
    char tp[64];
    snprintf(tp, sizeof tp, "/sys/class/thermal/thermal_zone0/trip_point_0_temp");
    char buf[32];
    return mel_sysfs_read_str(tp, buf, sizeof buf);
}

static inline Mel_Sensor_Power_Source mel_sysfs_power_source(void)
{
    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return MEL_SENSOR_POWER_SOURCE_UNKNOWN;

    bool mains_online = false, have_mains = false, have_battery = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char tp[256], type[32];
        snprintf(tp, sizeof tp, "/sys/class/power_supply/%s/type", e->d_name);
        if (!mel_sysfs_read_str(tp, type, sizeof type)) continue;

        if (strcmp(type, "Mains") == 0 || strcmp(type, "USB") == 0) {
            have_mains = true;
            char op[256], online[8];
            snprintf(op, sizeof op, "/sys/class/power_supply/%s/online", e->d_name);
            if (mel_sysfs_read_str(op, online, sizeof online) && online[0] == '1') mains_online = true;
        } else if (strcmp(type, "Battery") == 0) {
            have_battery = true;
        }
    }
    closedir(d);

    if (mains_online)  return MEL_SENSOR_POWER_SOURCE_AC;
    if (have_battery)  return MEL_SENSOR_POWER_SOURCE_BATTERY;
    if (have_mains)    return MEL_SENSOR_POWER_SOURCE_AC;
    return MEL_SENSOR_POWER_SOURCE_UNKNOWN;
}

static inline bool mel_sysfs_power_source_present(void)
{
    DIR *d = opendir("/sys/class/power_supply");
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.') { closedir(d); return true; }
    }
    closedir(d);
    return false;
}
