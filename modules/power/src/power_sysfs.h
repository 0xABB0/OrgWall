#pragma once

#include <power/power.h>

#include <stdio.h>
#include <string.h>
#include <dirent.h>

static inline bool mel_sysfs_read_str(const char* path, char* buf, size_t cap)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    if (n == 0)
        return false;
    buf[n] = '\0';
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == ' ' || buf[n - 1] == '\t'))
        buf[--n] = '\0';
    return true;
}

static inline Mel_Power_Source mel_sysfs_power_source(void)
{
    DIR* d = opendir("/sys/class/power_supply");
    if (!d)
        return MEL_POWER_SOURCE_UNKNOWN;

    bool           mains_online = false, have_mains = false, have_battery = false;
    struct dirent* e;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        char tp[256], type[32];
        snprintf(tp, sizeof tp, "/sys/class/power_supply/%s/type", e->d_name);
        if (!mel_sysfs_read_str(tp, type, sizeof type))
            continue;

        if (strcmp(type, "Mains") == 0 || strcmp(type, "USB") == 0)
        {
            have_mains = true;
            char op[256], online[8];
            snprintf(op, sizeof op, "/sys/class/power_supply/%s/online", e->d_name);
            if (mel_sysfs_read_str(op, online, sizeof online) && online[0] == '1')
                mains_online = true;
        }
        else if (strcmp(type, "Battery") == 0)
        {
            have_battery = true;
        }
    }
    closedir(d);

    if (mains_online)
        return MEL_POWER_SOURCE_AC;
    if (have_battery)
        return MEL_POWER_SOURCE_BATTERY;
    if (have_mains)
        return MEL_POWER_SOURCE_AC;
    return MEL_POWER_SOURCE_UNKNOWN;
}

static inline bool mel_sysfs_power_source_present(void)
{
    DIR* d = opendir("/sys/class/power_supply");
    if (!d)
        return false;
    struct dirent* e;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] != '.')
        {
            closedir(d);
            return true;
        }
    }
    closedir(d);
    return false;
}
