#include "test.harness.h"
#include <stdlib.h>

Mel_Test_Context s_test_ctx = {0};

static Mel_Test_Entry* s_test_list = NULL;
static u32 s_test_count = 0;

void mel__test_register(Mel_Test_Entry* entry)
{
    entry->next = s_test_list;
    s_test_list = entry;
    s_test_count++;
}

Mel_Test_Entry* mel_test_list(void) { return s_test_list; }
u32 mel_test_count(void) { return s_test_count; }

void mel_test_run_one(Mel_Test_Entry* entry)
{
    u32 failed_before = s_test_ctx.failed;
    s_test_ctx.current_test = entry->name;
    entry->func();
    entry->status = (s_test_ctx.failed > failed_before) ? MEL_TEST_FAILED : MEL_TEST_PASSED;
}

void mel_test_run_all(void)
{
    for (Mel_Test_Entry* e = s_test_list; e; e = e->next)
        mel_test_run_one(e);
}

static bool tag_matches(const char* tags, const char* filter)
{
    if (!tags) return false;

    const char* p = tags;
    while (*p)
    {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;

        const char* start = p;
        while (*p && *p != ',') p++;

        usize len = (usize)(p - start);
        while (len > 0 && start[len - 1] == ' ') len--;

        if (strlen(filter) == len && strncmp(start, filter, len) == 0)
            return true;
    }
    return false;
}

int mel_test_main(int argc, char** argv)
{
    bool list_only = false;
    const char* filter = NULL;
    const char* tag = NULL;
    i32 run_id = -1;
    bool include_visual = false;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--list") == 0)
            list_only = true;
        else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc)
            filter = argv[++i];
        else if (strcmp(argv[i], "--tag") == 0 && i + 1 < argc)
            tag = argv[++i];
        else if (strcmp(argv[i], "--id") == 0 && i + 1 < argc)
            run_id = atoi(argv[++i]);
        else if (strcmp(argv[i], "--visual") == 0)
            include_visual = true;
    }

    {
        Mel_Test_Entry* prev = NULL;
        Mel_Test_Entry* curr = s_test_list;
        while (curr)
        {
            Mel_Test_Entry* next = curr->next;
            curr->next = prev;
            prev = curr;
            curr = next;
        }
        s_test_list = prev;
    }

    u32 id = 0;
    for (Mel_Test_Entry* e = s_test_list; e; e = e->next)
        e->id = id++;

    if (list_only)
    {
        for (Mel_Test_Entry* e = s_test_list; e; e = e->next)
            printf("%4u  %-40s  %-50s  [%s]\n", e->id, e->name, e->file, e->tags ? e->tags : "");
        return 0;
    }

    u32 passed = 0;
    u32 failed = 0;
    u32 skipped = 0;
    u32 total = 0;

    for (Mel_Test_Entry* e = s_test_list; e; e = e->next)
    {
        if (!include_visual && e->tags && tag_matches(e->tags, "visual"))
        {
            skipped++;
            continue;
        }

        if (filter && !strstr(e->name, filter))
            continue;

        if (tag && !tag_matches(e->tags, tag))
            continue;

        if (run_id >= 0 && e->id != (u32)run_id)
            continue;

        total++;

        mel_test_run_one(e);

        if (e->status == MEL_TEST_FAILED)
        {
            failed++;
        }
        else
        {
            passed++;
            printf("  PASS: %s\n", e->name);
        }
    }

    printf("\nResults: %u/%u passed", passed, total);
    if (failed > 0) printf(" (%u FAILED)", failed);
    if (skipped > 0) printf(" (%u skipped)", skipped);
    printf("\n");

    return failed > 0 ? 1 : 0;
}
