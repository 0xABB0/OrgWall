#include "game_test.h"

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <cimgui/cimgui.h>

void game_test_imgui(void)
{
    if (!igBegin("Tests", NULL, 0)) { igEnd(); return; }

    if (igButton("Run All", (ImVec2){0, 0}))
        mel_test_run_all();

    u32 total = mel_test_count();
    u32 passed = 0;
    u32 failed = 0;
    bool any_ran = false;

    for (Mel_Test_Entry* e = mel_test_list(); e; e = e->next)
    {
        if (e->status == MEL_TEST_PASSED) { passed++; any_ran = true; }
        else if (e->status == MEL_TEST_FAILED) { failed++; any_ran = true; }
    }

    if (any_ran)
    {
        igSameLine(0, 10);
        if (failed == 0)
            igTextColored((ImVec4){0.2f, 1.0f, 0.2f, 1.0f}, "%u/%u passed", passed, total);
        else
            igTextColored((ImVec4){1.0f, 0.3f, 0.3f, 1.0f}, "%u failed", failed);
    }

    igSeparator();

    const char* cur_tags = NULL;
    u32 idx = 0;
    for (Mel_Test_Entry* e = mel_test_list(); e; e = e->next, idx++)
    {
        const char* tag = e->tags ? e->tags : "untagged";

        if (!cur_tags || strcmp(cur_tags, tag) != 0)
        {
            if (cur_tags) igTreePop();
            cur_tags = tag;
            if (!igTreeNodeEx_Str(tag, ImGuiTreeNodeFlags_DefaultOpen))
            {
                cur_tags = NULL;
                Mel_Test_Entry* skip = e->next;
                while (skip && skip->tags && strcmp(skip->tags, tag) == 0)
                {
                    e = skip;
                    skip = skip->next;
                    idx++;
                }
                continue;
            }
        }

        ImVec4 color;
        const char* icon;
        switch (e->status)
        {
            case MEL_TEST_PASSED:  color = (ImVec4){0.2f, 1.0f, 0.2f, 1.0f}; icon = "[OK]"; break;
            case MEL_TEST_FAILED:  color = (ImVec4){1.0f, 0.3f, 0.3f, 1.0f}; icon = "[XX]"; break;
            default:               color = (ImVec4){0.5f, 0.5f, 0.5f, 1.0f}; icon = "[--]"; break;
        }

        igTextColored(color, "%s %s", icon, e->name);

        igSameLine(0, 5);
        igPushID_Int((int)idx);
        if (igSmallButton("run"))
            mel_test_run_one(e);
        igPopID();
    }

    if (cur_tags) igTreePop();

    igEnd();
}
