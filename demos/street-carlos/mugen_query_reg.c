#include "mugen_cns.h"
#include "string.str8.h"
#include <string.h>

static Mugen_Query_Reg query_table[MUGEN_QUERY_MAX];

#define MAX_QUERY_NAME_ENTRIES 128

static Mugen_Query_Name_Entry name_table[MAX_QUERY_NAME_ENTRIES];
static u32 name_count;

void mugen_query_register(u8 id, const char* name, Mugen_Query_Eval_Fn eval)
{
    if (eval) query_table[id].eval = eval;
    if (!query_table[id].name && name) query_table[id].name = name;

    if (name && name_count < MAX_QUERY_NAME_ENTRIES)
        name_table[name_count++] = (Mugen_Query_Name_Entry){ .name = name, .id = id };
}

u8 mugen_query_lookup_name(str8 name)
{
    for (u32 i = 0; i < name_count; i++)
    {
        if (str8_ieq_cstr(name, name_table[i].name))
            return name_table[i].id;
    }
    return 255;
}

Mugen_Query_Reg* mugen_query_get_reg(u8 id)
{
    if (id >= MUGEN_QUERY_MAX) return NULL;
    return &query_table[id];
}

u32 mugen_query_name_count(void)
{
    return name_count;
}

const Mugen_Query_Name_Entry* mugen_query_name_table(void)
{
    return name_table;
}
