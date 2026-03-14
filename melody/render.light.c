#include "render.light.h"
#include "allocator.heap.h"

Mel_Point_Light_Gpu_Record mel_point_light_pack_gpu_record(Mel_Point_Light light)
{
    return (Mel_Point_Light_Gpu_Record){
        .position_radius = mel_vec4(light.position.x, light.position.y, light.position.z,
            light.radius > 0.0f ? light.radius : 0.0f),
        .color_intensity = mel_vec4(light.color.x, light.color.y, light.color.z, light.intensity),
    };
}

bool mel_light_table_init_opt(Mel_Light_Table* table, Mel_Light_Table_Init_Opt opt)
{
    assert(table != nullptr);
    assert(opt.dev != nullptr);

    u32 capacity = opt.capacity > 0 ? opt.capacity : 64;
    *table = (Mel_Light_Table){
        .dev = opt.dev,
        .capacity = capacity,
        .records = mel_alloc(mel_alloc_heap(), sizeof(Mel_Point_Light_Gpu_Record) * capacity),
    };

    mel_gpu_buffer_init(&table->buffer, opt.dev,
        .size = sizeof(Mel_Point_Light_Gpu_Record) * capacity,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
        .map_on_create = true);

    return true;
}

void mel_light_table_shutdown(Mel_Light_Table* table)
{
    assert(table != nullptr);
    if (table->dev)
        mel_gpu_buffer_shutdown(&table->buffer, table->dev);
    if (table->records)
        mel_dealloc(mel_alloc_heap(), table->records);
    *table = (Mel_Light_Table){0};
}

void mel_light_table_clear(Mel_Light_Table* table)
{
    assert(table != nullptr);
    table->count = 0;
}

u32 mel_light_table_push(Mel_Light_Table* table, Mel_Point_Light light)
{
    return mel_light_table_push_record(table, mel_point_light_pack_gpu_record(light));
}

u32 mel_light_table_push_record(Mel_Light_Table* table, Mel_Point_Light_Gpu_Record record)
{
    assert(table != nullptr);
    assert(table->count < table->capacity);
    u32 index = table->count++;
    table->records[index] = record;
    return index;
}

void mel_light_table_upload(Mel_Light_Table* table)
{
    assert(table != nullptr);
    if (table->count == 0)
        return;
    mel_gpu_buffer_upload(&table->buffer, table->dev, table->records,
        sizeof(Mel_Point_Light_Gpu_Record) * table->count, 0);
}

Mel_Point_Light_Gpu_Record* mel_light_table_records(Mel_Light_Table* table)
{
    assert(table != nullptr);
    return table->records;
}

u32 mel_light_table_count(Mel_Light_Table* table)
{
    assert(table != nullptr);
    return table->count;
}
