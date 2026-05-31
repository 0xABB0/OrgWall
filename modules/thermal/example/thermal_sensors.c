#include <thermal/thermal.h>

#include <allocator/heap.h>

#include <assert.h>
#include <stdio.h>

static const char* fidelity_name(Mel_Thermal_Temp_Fidelity f)
{
    if (f == MEL_THERMAL_TEMP_MEASURED)
        return "measured";
    if (f == MEL_THERMAL_TEMP_DERIVED)
        return "derived";
    return "none";
}

int main(void)
{
    const Mel_Alloc*        alloc = mel_alloc_heap();
    Mel_Thermal_Sensor_List list = mel_thermal_sensor_enumerate(alloc);

    printf("thermal: enumerated %zu sensor(s)\n", (size_t)list.count);
    for (usize i = 0; i < list.count; i++)
    {
        Mel_Thermal_Sensor* s = &list.items[i];
        assert(s->name != NULL);
        assert(s->get != NULL);

        Mel_Thermal_Reading r = mel_thermal_sensor_read(s, NULL);
        double              c = mel_degrees_to_celsius(r.value);
        printf("  %-6s domain=%d %-8s %7.2f C  %7.2f F\n", s->name, (int)s->domain, fidelity_name(r.fidelity), c, mel_degrees_to_fahrenheit(r.value));

        if (r.fidelity == MEL_THERMAL_TEMP_NONE)
            assert(mel_degrees_is_absolute_zero(r.value));
        else
            assert(c > -50.0 && c < 150.0);
    }

    mel_thermal_sensor_list_free(&list, alloc);
    assert(list.items == NULL);
    assert(list.count == 0);
    printf("thermal-sensors: ok\n");
    return 0;
}
