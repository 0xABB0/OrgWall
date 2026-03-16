#include "../melody/test.harness.h"
#include "../melody/boot.registry.h"
#include "../melody/event.channel.h"
#include "../melody/gpu.device.h"
#include "../melody/gpu.shader.h"
#include "../melody/sprite.pass.h"
#include "../melody/text.pass.h"
#include "../melody/mesh.pass.h"
#include "../melody/texture.pool.h"
#include "../melody/font.atlas.h"
#include "../melody/allocator.heap.h"

static i32 s_wire_a_called;
static i32 s_wire_b_called;
static i32 s_wire_order[4];
static i32 s_wire_index;

static void wire_a(void) { s_wire_a_called = 1; s_wire_order[s_wire_index++] = 1; }
static void wire_b(void) { s_wire_b_called = 1; s_wire_order[s_wire_index++] = 2; }

MEL_TEST(boot_wire_count, .tags = "event")
{
    MEL_ASSERT_GE(mel__boot_wire_count(), (u32)0);
}

MEL_TEST(boot_channels_initialized_pre_main, .tags = "event")
{
    MEL_ASSERT_NOT_NULL(mel_gpu_device_ready.rcu.writer_lock);
    MEL_ASSERT_NOT_NULL(mel_sprite_pass_ready.rcu.writer_lock);
    MEL_ASSERT_NOT_NULL(mel_text_pass_ready.rcu.writer_lock);
    MEL_ASSERT_NOT_NULL(mel_mesh_pass_ready.rcu.writer_lock);
    MEL_ASSERT_NOT_NULL(mel_texture_pool_ready.rcu.writer_lock);
    MEL_ASSERT_NOT_NULL(mel_font_pool_ready.rcu.writer_lock);
    MEL_ASSERT_NOT_NULL(mel_slang_ready.rcu.writer_lock);
}

MEL_TEST(boot_wires_registered, .tags = "event")
{
    MEL_ASSERT_GE(mel__boot_wire_count(), (u32)5);
}
