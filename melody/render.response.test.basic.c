#include "test.harness.h"
#include "render.response.h"
#include "render.viewport.h"
#include "render.view.registry.h"

typedef struct {
    const Mel_Render_Response_Op_Type* items[8];
    u32 count;
} Response_Test_Visit_Log;

static void response_test_noop(Mel_Render_Response_Ctx* ctx, const void* params)
{
    MEL_UNUSED(ctx);
    MEL_UNUSED(params);
}

static const Mel_Render_Response_Op_Type s_response_test_type_a = {
    .name = S8("test.a"),
    .params_size = 0,
    .params_align = 1,
    .run = response_test_noop,
};

static const Mel_Render_Response_Op_Type s_response_test_type_b = {
    .name = S8("test.b"),
    .params_size = 0,
    .params_align = 1,
    .run = response_test_noop,
};

static const Mel_Render_Response_Op_Type s_response_test_type_c = {
    .name = S8("test.c"),
    .params_size = 0,
    .params_align = 1,
    .run = response_test_noop,
};

static Mel_Render_View_Handle response_test_make_view(void)
{
    Mel_Render_View view = {0};
    Mel_SlotMap_Handle raw = mel__view_registry_insert(&view);
    return (Mel_Render_View_Handle){ .handle = raw };
}

static void response_test_visit(const Mel_Render_Response_Op_Type* type,
                                const void* params,
                                void* user_data,
                                void* ctx)
{
    MEL_UNUSED(params);
    MEL_UNUSED(user_data);

    Response_Test_Visit_Log* log = ctx;
    log->items[log->count++] = type;
}

MEL_TEST(render_response_ordering_and_clear, .tags = "render")
{
    Mel_Render_View_Handle view = response_test_make_view();

    Mel_Render_View_Response_Op_Handle a = mel_render_view_response_op_add_impl(
        (Mel_Render_View_Response_Op_Desc){
            .view = view,
            .type = &s_response_test_type_a,
            .order = 10,
        });
    Mel_Render_View_Response_Op_Handle b = mel_render_view_response_op_add_impl(
        (Mel_Render_View_Response_Op_Desc){
            .view = view,
            .type = &s_response_test_type_b,
            .order = -5,
        });
    Mel_Render_View_Response_Op_Handle c = mel_render_view_response_op_add_impl(
        (Mel_Render_View_Response_Op_Desc){
            .view = view,
            .type = &s_response_test_type_c,
            .order = 10,
        });

    MEL_ASSERT(mel_render_view_response_op_alive(a));
    MEL_ASSERT(mel_render_view_response_op_alive(b));
    MEL_ASSERT(mel_render_view_response_op_alive(c));
    MEL_ASSERT_EQ(mel_render_view_response_op_count(view), 3u);

    Response_Test_Visit_Log log = {0};
    mel__render_view_response_visit(view, response_test_visit, &log);
    MEL_ASSERT_EQ(log.count, 3u);
    MEL_ASSERT_EQ(log.items[0], &s_response_test_type_b);
    MEL_ASSERT_EQ(log.items[1], &s_response_test_type_a);
    MEL_ASSERT_EQ(log.items[2], &s_response_test_type_c);

    mel_render_view_response_op_clear(view, &s_response_test_type_a);
    MEL_ASSERT(!mel_render_view_response_op_alive(a));
    MEL_ASSERT_EQ(mel_render_view_response_op_count(view), 2u);

    log = (Response_Test_Visit_Log){0};
    mel__render_view_response_visit(view, response_test_visit, &log);
    MEL_ASSERT_EQ(log.count, 2u);
    MEL_ASSERT_EQ(log.items[0], &s_response_test_type_b);
    MEL_ASSERT_EQ(log.items[1], &s_response_test_type_c);

    mel_render_view_destroy(view);
}

MEL_TEST(render_response_handle_lifetime, .tags = "render")
{
    Mel_Render_View_Handle view = response_test_make_view();

    Mel_Render_View_Response_Op_Handle handle = mel_render_view_response_op_add_impl(
        (Mel_Render_View_Response_Op_Desc){
            .view = view,
            .type = &s_response_test_type_a,
            .order = 0,
        });

    MEL_ASSERT(mel_render_view_response_op_alive(handle));
    MEL_ASSERT_EQ(mel_render_view_response_op_count(view), 1u);

    mel_render_view_response_op_remove(handle);
    MEL_ASSERT(!mel_render_view_response_op_alive(handle));
    MEL_ASSERT_EQ(mel_render_view_response_op_count(view), 0u);

    mel_render_view_destroy(view);
}

MEL_TEST(render_response_cleanup_on_view_destroy, .tags = "render")
{
    Mel_Render_View_Handle view = response_test_make_view();

    Mel_Render_View_Response_Op_Handle handle = mel_render_view_response_op_add_impl(
        (Mel_Render_View_Response_Op_Desc){
            .view = view,
            .type = &s_response_test_type_a,
            .order = 0,
        });

    MEL_ASSERT(mel_render_view_response_op_alive(handle));

    mel_render_view_destroy(view);

    MEL_ASSERT(!mel_render_view_response_op_alive(handle));
}
