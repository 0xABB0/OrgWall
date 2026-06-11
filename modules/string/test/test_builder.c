#include <test/test.h>

#include <string/builder.h>
#include <allocator/heap.h>

MEL_TEST(builder, empty_produces_empty_str8)
{
    Mel_String_Builder sb;
    mel_sb_init(&sb, mel_alloc_heap());
    str8 result = mel_sb_to_str8(&sb, mel_alloc_heap());
    MEL_EXPECT(str8_is_empty(result));
    mel_sb_reset(&sb);
}

MEL_TEST(builder, append_cstr_round_trips)
{
    Mel_String_Builder sb;
    mel_sb_init(&sb, mel_alloc_heap());
    MEL_REQUIRE(mel_sb_append_cstr(&sb, "hello"));
    str8 result = mel_sb_to_str8(&sb, mel_alloc_heap());
    MEL_EXPECT_EQ(result.len, 5);
    MEL_EXPECT(str8_equals(result, S8("hello")));
    mel_dealloc(mel_alloc_heap(), result.data);
    mel_sb_reset(&sb);
}

MEL_TEST(builder, append_multiple_parts)
{
    Mel_String_Builder sb;
    mel_sb_init(&sb, mel_alloc_heap());
    MEL_REQUIRE(mel_sb_append_cstr(&sb, "foo"));
    MEL_REQUIRE(mel_sb_append_cstr(&sb, "bar"));
    MEL_REQUIRE(mel_sb_append_cstr(&sb, "baz"));
    str8 result = mel_sb_to_str8(&sb, mel_alloc_heap());
    MEL_EXPECT_EQ(result.len, 9);
    MEL_EXPECT(str8_equals(result, S8("foobarbaz")));
    mel_dealloc(mel_alloc_heap(), result.data);
    mel_sb_reset(&sb);
}

MEL_TEST(builder, append_null_terminates)
{
    Mel_String_Builder sb;
    mel_sb_init(&sb, mel_alloc_heap());
    MEL_REQUIRE(mel_sb_append_cstr(&sb, "hello"));
    MEL_REQUIRE(mel_sb_append_null(&sb));
    str8 result = mel_sb_to_str8(&sb, mel_alloc_heap());
    MEL_EXPECT_EQ(result.len, 6);
    MEL_EXPECT_EQ(result.data[5], '\0');
    mel_dealloc(mel_alloc_heap(), result.data);
    mel_sb_reset(&sb);
}

MEL_TEST(builder, append_str8)
{
    Mel_String_Builder sb;
    mel_sb_init(&sb, mel_alloc_heap());
    MEL_REQUIRE(mel_sb_append_str8(&sb, S8("hello")));
    MEL_REQUIRE(mel_sb_append_str8(&sb, S8(" world")));
    str8 result = mel_sb_to_str8(&sb, mel_alloc_heap());
    MEL_EXPECT(str8_equals(result, S8("hello world")));
    mel_dealloc(mel_alloc_heap(), result.data);
    mel_sb_reset(&sb);
}

MEL_TEST(builder, append_buf)
{
    Mel_String_Builder sb;
    mel_sb_init(&sb, mel_alloc_heap());
    const u8 bytes[] = { 0x01, 0x02, 0x03 };
    MEL_REQUIRE(mel_sb_append_buf(&sb, bytes, 3));
    str8 result = mel_sb_to_str8(&sb, mel_alloc_heap());
    MEL_EXPECT_EQ(result.len, 3);
    MEL_EXPECT_EQ(result.data[0], 0x01);
    MEL_EXPECT_EQ(result.data[1], 0x02);
    MEL_EXPECT_EQ(result.data[2], 0x03);
    mel_dealloc(mel_alloc_heap(), result.data);
    mel_sb_reset(&sb);
}

MEL_TEST(builder, append_fmt)
{
    Mel_String_Builder sb;
    mel_sb_init(&sb, mel_alloc_heap());
    MEL_REQUIRE(mel_sb_append_fmt(&sb, "x=%d y=%d", 3, 7));
    str8 result = mel_sb_to_str8(&sb, mel_alloc_heap());
    MEL_EXPECT(str8_equals(result, S8("x=3 y=7")));
    mel_dealloc(mel_alloc_heap(), result.data);
    mel_sb_reset(&sb);
}

MEL_TEST(builder, reset_allows_reuse)
{
    Mel_String_Builder sb;
    mel_sb_init(&sb, mel_alloc_heap());
    MEL_REQUIRE(mel_sb_append_cstr(&sb, "first"));
    mel_sb_reset(&sb);
    MEL_REQUIRE(mel_sb_append_cstr(&sb, "second"));
    str8 result = mel_sb_to_str8(&sb, mel_alloc_heap());
    MEL_EXPECT(str8_equals(result, S8("second")));
    mel_dealloc(mel_alloc_heap(), result.data);
    mel_sb_reset(&sb);
}

MEL_TEST(builder, total_tracks_correctly)
{
    Mel_String_Builder sb;
    mel_sb_init(&sb, mel_alloc_heap());
    MEL_REQUIRE(mel_sb_append_cstr(&sb, "ab"));
    MEL_REQUIRE(mel_sb_append_cstr(&sb, "cde"));
    MEL_EXPECT_EQ(sb.total, 5);
    mel_sb_reset(&sb);
    MEL_EXPECT_EQ(sb.total, 0);
}
