#include "../../melody/test.harness.h"
#include "../../melody/allocator.h"
#include "../../melody/allocator.heap.h"
#include "../../melody/string.str8.h"
#include "mugen_cmd.h"

#include <stdio.h>

static const char* CMD_SIMPLE =
    "[Command]\n"
    "name = \"QCB_a\"\n"
    "command = ~D, DB, B, a\n"
    "\n"
    "[Command]\n"
    "name = \"a\"\n"
    "command = a\n"
    "time = 1\n"
    "\n"
    "[Statedef -1]\n"
    "\n"
    "[State -1, Special]\n"
    "type = ChangeState\n"
    "value = 1090\n"
    "trigger1 = command = \"QCB_a\"\n"
    "trigger1 = statetype != A\n"
    "trigger1 = ctrl\n"
    "\n"
    "[State -1, Stand Light]\n"
    "type = ChangeState\n"
    "value = 200\n"
    "triggerall = command = \"a\"\n"
    "triggerall = command != \"holddown\"\n"
    "trigger1 = statetype = S\n"
    "trigger1 = ctrl\n";

MEL_TEST(mugen_cmd_parse_simple, .tags = "mugen")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    Mugen_Cmd cmd;
    bool ok = mugen_cmd_load(&cmd, str8_from_cstr(CMD_SIMPLE), heap);
    MEL_ASSERT(ok);

    MEL_ASSERT_EQ(cmd.command_count, 2);
    MEL_ASSERT(str8_equals(cmd.commands[0].name, S8("QCB_a")));
    MEL_ASSERT(str8_equals(cmd.commands[0].command, S8("~D, DB, B, a")));
    MEL_ASSERT_EQ(cmd.commands[0].time, 15);
    MEL_ASSERT(str8_equals(cmd.commands[1].name, S8("a")));
    MEL_ASSERT_EQ(cmd.commands[1].time, 1);

    MEL_ASSERT_EQ(cmd.state_entry_count, 2);

    MEL_ASSERT_EQ(cmd.state_entries[0].action_number, 1090);
    MEL_ASSERT(str8_equals(cmd.state_entries[0].command_name, S8("QCB_a")));
    MEL_ASSERT_EQ(cmd.state_entries[0].statetype, MUGEN_STATETYPE_A);
    MEL_ASSERT(cmd.state_entries[0].statetype_not);
    MEL_ASSERT(cmd.state_entries[0].requires_ctrl);

    MEL_ASSERT_EQ(cmd.state_entries[1].action_number, 200);
    MEL_ASSERT(str8_equals(cmd.state_entries[1].command_name, S8("a")));
    MEL_ASSERT(str8_equals(cmd.state_entries[1].neg_command_name, S8("holddown")));
    MEL_ASSERT_EQ(cmd.state_entries[1].statetype, MUGEN_STATETYPE_S);
    MEL_ASSERT(!cmd.state_entries[1].statetype_not);

    mugen_cmd_shutdown(&cmd, heap);
}

static str8 load_file(const char* path, const Mel_Alloc* alloc)
{
    FILE* f = fopen(path, "rb");
    if (!f) return STR8_EMPTY;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* buf = mel_alloc(alloc, (usize)sz);
    fread(buf, 1, (size_t)sz, f);
    fclose(f);
    return str8_from_parts(buf, (size)sz);
}

MEL_TEST(mugen_cmd_load_real_file, .tags = "mugen")
{
    const Mel_Alloc* heap = mel_alloc_heap();

    str8 data = load_file("demos/street-carlos/chars/poi-son/poi-son.cmd", heap);
    if (data.len == 0)
    {
        data = load_file("chars/poi-son/poi-son.cmd", heap);
    }
    if (data.len == 0) return;

    Mugen_Cmd cmd;
    bool ok = mugen_cmd_load(&cmd, data, heap);
    MEL_ASSERT(ok);
    MEL_ASSERT(cmd.command_count > 10);
    MEL_ASSERT(cmd.state_entry_count > 5);

    bool has_200 = false;
    bool has_1090 = false;
    for (u32 i = 0; i < cmd.state_entry_count; i++)
    {
        if (cmd.state_entries[i].action_number == 200) has_200 = true;
        if (cmd.state_entries[i].action_number == 1090) has_1090 = true;
    }
    MEL_ASSERT(has_200);
    MEL_ASSERT(has_1090);

    mugen_cmd_shutdown(&cmd, heap);
    mel_dealloc(heap, data.data);
}
