#include "street_carlos_flow.h"

#include <assert.h>

#include "street_carlos_title_stage.h"
#include "street_carlos_main_menu_stage.h"
#include "street_carlos_char_select_stage.h"
#include "street_carlos_stage_select_stage.h"
#include "street_carlos_loading_stage.h"
#include "street_carlos_fight_stage.h"
#include "street_carlos_pause_stage.h"

static void street_carlos_show_flow_stage(Street_Carlos_Ctx* ctx, str8 name)
{
    mel_stage_registry_enable_exclusive(&ctx->stage_registry, name, STREET_CARLOS_STAGE_TAG_FLOW);
    mel_stage_registry_disable_tagged(&ctx->stage_registry, STREET_CARLOS_STAGE_TAG_MODAL);
}

static u64 street_carlos_menu_rand_next(Street_Carlos_Ctx* ctx)
{
    if (ctx->menu_rng_state == 0)
        ctx->menu_rng_state = ((u64)SDL_GetTicks() << 32) ^ 0x9E3779B97F4A7C15ull;

    ctx->menu_rng_state ^= ctx->menu_rng_state << 13;
    ctx->menu_rng_state ^= ctx->menu_rng_state >> 7;
    ctx->menu_rng_state ^= ctx->menu_rng_state << 17;
    return ctx->menu_rng_state;
}

u32 street_carlos_menu_rand_index(Street_Carlos_Ctx* ctx, u32 count)
{
    assert(count > 0);
    return (u32)(street_carlos_menu_rand_next(ctx) % count);
}

void street_carlos_show_title(Street_Carlos_Ctx* ctx)
{
    street_carlos_show_flow_stage(ctx, S8("title"));
}

void street_carlos_show_main_menu(Street_Carlos_Ctx* ctx)
{
    street_carlos_show_flow_stage(ctx, S8("main_menu"));
}

void street_carlos_show_char_select(Street_Carlos_Ctx* ctx)
{
    street_carlos_show_flow_stage(ctx, S8("char_select"));
}

void street_carlos_show_stage_select(Street_Carlos_Ctx* ctx)
{
    street_carlos_show_flow_stage(ctx, S8("stage_select"));
}

void street_carlos_start_loading(Street_Carlos_Ctx* ctx, Mugen_Char* p1_char, Mugen_Char* p2_char, str8 stage_path)
{
    if (!p1_char || !p2_char || stage_path.len == 0)
        return;

    street_carlos_fight_stage_prepare(ctx->fight_stage, ctx, p1_char, p2_char, stage_path);

    mel_progress_clear(&ctx->load_progress);
    mel_progress_add_custom(&ctx->load_progress, street_carlos_fight_stage_progress, ctx->fight_stage, 1.0f);

    street_carlos_show_flow_stage(ctx, S8("loading"));
}

void street_carlos_start_quick_fight(Street_Carlos_Ctx* ctx)
{
    u32 roster_count = mugen_roster_count(&ctx->roster);
    if (roster_count == 0 || ctx->stage_choice_count == 0)
        return;

    u32 p1_index = street_carlos_menu_rand_index(ctx, roster_count);
    u32 p2_index = street_carlos_menu_rand_index(ctx, roster_count);
    if (roster_count > 1 && p2_index == p1_index)
        p2_index = (p2_index + 1) % roster_count;

    Mugen_Char* p1_char = mugen_roster_at(&ctx->roster, p1_index);
    Mugen_Char* p2_char = mugen_roster_at(&ctx->roster, p2_index);
    str8 stage_path = ctx->stage_choices[street_carlos_menu_rand_index(ctx, ctx->stage_choice_count)].path;
    street_carlos_start_loading(ctx, p1_char, p2_char, stage_path);
}
