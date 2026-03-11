#include "mugen.cns.h"
#include "string.str8.h"
#include <string.h>

static Mugen_SC_Reg sc_table[MUGEN_SC_MAX];

#define MAX_NAME_ENTRIES 128

typedef struct {
    const char* name;
    u8 id;
} Name_Entry;

static Name_Entry name_table[MAX_NAME_ENTRIES];
static u32 name_count;

void mugen_sc_register(u8 id, const char* name, Mugen_SC_Parse_Fn parse_param, Mugen_SC_Exec_Fn exec)
{
    if (parse_param) sc_table[id].parse_param = parse_param;
    if (exec) sc_table[id].exec = exec;
    if (!sc_table[id].name) sc_table[id].name = name;

    if (name && name_count < MAX_NAME_ENTRIES)
    {
        name_table[name_count++] = (Name_Entry){ .name = name, .id = id };
    }
}

u8 mugen_sc_lookup_name(str8 name)
{
    for (u32 i = 0; i < name_count; i++)
    {
        const char* rname = name_table[i].name;
        size rlen = (size)strlen(rname);
        if (name.len != rlen) continue;
        bool match = true;
        for (size j = 0; j < rlen; j++)
        {
            u8 a = name.data[j]; if (a >= 'A' && a <= 'Z') a += 32;
            u8 b = (u8)rname[j]; if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = false; break; }
        }
        if (match) return name_table[i].id;
    }
    return MUGEN_SC_NULL;
}

Mugen_SC_Reg* mugen_sc_get_reg(u8 id)
{
    if (id >= MUGEN_SC_MAX) return NULL;
    return &sc_table[id];
}
