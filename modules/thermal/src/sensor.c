#include <thermal/thermal.h>

#include <allocator/allocator.h>

void mel_thermal_sensor_list_free(Mel_Thermal_Sensor_List* list, const Mel_Alloc* alloc)
{
    if (!list || !list->items)
        return;
    mel_dealloc(alloc, list->items);
    list->items = NULL;
    list->count = 0;
}
