#include "render.technique.h"
#include "render.frame_plan.h"
#include "render.source.h"
#include "sprite.pass.h"
#include "text.pass.h"
#include "core.engine.h"
#include "collection.array.h"
#include "allocator.heap.h"
#include "string.str8.h"

typedef struct {
    Mel_Technique_Desc desc;
} Mel__Technique_Registry_Entry;

static Mel_Array(Mel__Technique_Registry_Entry) s_registry;
static bool s_initialized;

static Mel_Technique_Compile_Result mel__compile_sprite(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Sprite_Pass* sprite_pass = ctx->plan_ctx->opt.sprite_pass ? ctx->plan_ctx->opt.sprite_pass : mel_sprite_pass();
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SCHEMA_SPRITE);
    if (!read_lists)
        return MEL_TECHNIQUE_COMPILE_SKIP;

    bool ok = mel_frame_plan_add_render_list_pass(ctx->plan_ctx, ctx->technique->name,
        mel_sprite_pass_execute, sprite_pass, read_lists);
    mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static Mel_Technique_Compile_Result mel__compile_text(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Text_Pass* text_pass = ctx->plan_ctx->opt.text_pass ? ctx->plan_ctx->opt.text_pass : mel_text_pass();
    Mel_Render_List** read_lists = mel_frame_plan_collect_render_lists(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SCHEMA_TEXT);
    if (!read_lists)
        return MEL_TECHNIQUE_COMPILE_SKIP;

    bool ok = mel_frame_plan_add_render_list_pass(ctx->plan_ctx, ctx->technique->name,
        mel_text_pass_execute, text_pass, read_lists);
    mel_frame_plan_free_read_lists(ctx->plan_ctx->plan, read_lists);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

__attribute__((constructor(214)))
static void mel__render_technique_registry_init(void)
{
    mel_array_init(&s_registry, mel_alloc_heap());
    s_initialized = true;

    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_SPRITE,
        .name = S8("sprite"),
        .source_schema = MEL_SCHEMA_SPRITE,
        .compile = mel__compile_sprite,
    });
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_TEXT,
        .name = S8("text"),
        .source_schema = MEL_SCHEMA_TEXT,
        .compile = mel__compile_text,
    });
}

__attribute__((destructor(214)))
static void mel__render_technique_registry_shutdown(void)
{
    if (!s_initialized)
        return;

    for (usize i = 0; i < s_registry.count; i++)
        mel_dealloc(mel_alloc_heap(), s_registry.items[i].desc.name.data);
    mel_array_free(&s_registry);
    s_initialized = false;
}

void mel_render_technique_register(const Mel_Technique_Desc* desc)
{
    assert(s_initialized);
    assert(desc != nullptr);
    assert(desc->family != MEL_TECHNIQUE_NONE);
    assert(desc->compile != nullptr);

    for (usize i = 0; i < s_registry.count; i++)
    {
        if (s_registry.items[i].desc.family == desc->family)
        {
            mel_dealloc(mel_alloc_heap(), s_registry.items[i].desc.name.data);
            s_registry.items[i].desc = *desc;
            s_registry.items[i].desc.name = str8_dup(desc->name, mel_alloc_heap());
            return;
        }
    }

    Mel__Technique_Registry_Entry entry = {
        .desc = *desc,
    };
    entry.desc.name = str8_dup(desc->name, mel_alloc_heap());
    mel_array_push(&s_registry, entry);
}

const Mel_Technique_Desc* mel_render_technique_get(Mel_Technique_Family_Id family)
{
    assert(s_initialized);
    for (usize i = 0; i < s_registry.count; i++)
        if (s_registry.items[i].desc.family == family)
            return &s_registry.items[i].desc;
    return nullptr;
}

str8 mel_render_technique_name(Mel_Technique_Family_Id family)
{
    const Mel_Technique_Desc* desc = mel_render_technique_get(family);
    return desc ? desc->name : S8("");
}
