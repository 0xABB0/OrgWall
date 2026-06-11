#include <power/power.h>

#include "../../src/power_str.h"
#include "../../src/power_sysfs.h"

#include <stdlib.h>
#include <string.h>

Mel_Power_Source mel_power_source_current(void) { return mel_sysfs_power_source(); }

static bool mel_linux_profile_token(char* buf, usize cap) { return mel_sysfs_read_str("/sys/firmware/acpi/platform_profile", buf, cap); }

static f32 mel_linux_profile_bias(const char* t)
{
    if (strcmp(t, "low-power") == 0 || strcmp(t, "quiet") == 0 || strcmp(t, "cool") == 0)
        return -1.0f;
    if (strcmp(t, "balanced-performance") == 0)
        return 0.5f;
    if (strcmp(t, "performance") == 0)
        return 1.0f;
    return 0.0f;
}

Mel_Power_Profile mel_power_profile_current(void)
{
    char tok[32];
    if (!mel_linux_profile_token(tok, sizeof tok))
        return (Mel_Power_Profile){ 0 };
    return (Mel_Power_Profile){ .bias = mel_linux_profile_bias(tok), .present = true };
}

bool mel_power_profile_name(char* buf, usize cap)
{
    char tok[32];
    if (!mel_linux_profile_token(tok, sizeof tok))
        return false;
    return mel_power_name_copy(buf, cap, tok);
}

static bool mel_linux_battery_dir(char* out, usize cap)
{
    DIR* d = opendir("/sys/class/power_supply");
    if (!d)
        return false;

    bool           found = false;
    struct dirent* e;
    while ((e = readdir(d)) != NULL)
    {
        if (e->d_name[0] == '.')
            continue;
        char tp[256], type[32];
        snprintf(tp, sizeof tp, "/sys/class/power_supply/%s/type", e->d_name);
        if (mel_sysfs_read_str(tp, type, sizeof type) && strcmp(type, "Battery") == 0)
        {
            snprintf(out, cap, "/sys/class/power_supply/%s", e->d_name);
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

static bool mel_linux_read_i64(const char* path, long long* out)
{
    char b[32];
    if (!mel_sysfs_read_str(path, b, sizeof b))
        return false;
    *out = atoll(b);
    return true;
}

Mel_Power_Battery mel_power_battery_current(void)
{
    Mel_Power_Battery out = { false, false, 0.0f, -1.0f, -1.0f };

    char base[256];
    if (!mel_linux_battery_dir(base, sizeof base))
        return out;

    char      path[300], status[32];
    long long capacity;
    snprintf(path, sizeof path, "%s/capacity", base);
    if (mel_linux_read_i64(path, &capacity))
    {
        out.present = true;
        out.level = (f32)capacity / 100.0f;
    }

    snprintf(path, sizeof path, "%s/status", base);
    if (mel_sysfs_read_str(path, status, sizeof status))
        out.charging = (strcmp(status, "Charging") == 0);

    char      p_now_path[300], p_full_path[300], p_rate_path[300];
    long long now = -1, full = -1, rate = -1;
    snprintf(p_now_path, sizeof p_now_path, "%s/energy_now", base);
    snprintf(p_full_path, sizeof p_full_path, "%s/energy_full", base);
    snprintf(p_rate_path, sizeof p_rate_path, "%s/power_now", base);
    bool have = mel_linux_read_i64(p_now_path, &now) && mel_linux_read_i64(p_rate_path, &rate);
    if (have)
        mel_linux_read_i64(p_full_path, &full);
    else
    {
        snprintf(p_now_path, sizeof p_now_path, "%s/charge_now", base);
        snprintf(p_full_path, sizeof p_full_path, "%s/charge_full", base);
        snprintf(p_rate_path, sizeof p_rate_path, "%s/current_now", base);
        have = mel_linux_read_i64(p_now_path, &now) && mel_linux_read_i64(p_rate_path, &rate);
        if (have)
            mel_linux_read_i64(p_full_path, &full);
    }

    if (out.present && have && rate > 0)
    {
        if (!out.charging)
            out.seconds_to_empty = (f32)now / (f32)rate * 3600.0f;
        else if (full >= now)
            out.seconds_to_full = (f32)(full - now) / (f32)rate * 3600.0f;
    }
    return out;
}

Mel_Power_Caps mel_power_caps(void)
{
    char buf[32], base[256];
    return (Mel_Power_Caps){
        .power_source_present = mel_sysfs_power_source_present(),
        .profile_present = mel_sysfs_read_str("/sys/firmware/acpi/platform_profile", buf, sizeof buf),
        .battery_present = mel_linux_battery_dir(base, sizeof base),
    };
}
