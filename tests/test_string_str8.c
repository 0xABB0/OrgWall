#include "../melody/test.harness.h"
#include "../melody/string.str8.h"
#include "../melody/hash.xxh.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"

MEL_TEST(literal_macro, .tags = "string")
{
    str8 s = S8("hello");
    MEL_ASSERT_EQ(s.len, 5);
    MEL_ASSERT(memcmp(s.data, "hello", 5) == 0);
}

MEL_TEST(from_cstr, .tags = "string")
{
    str8 s = str8_from_cstr("world");
    MEL_ASSERT_EQ(s.len, 5);
    MEL_ASSERT(memcmp(s.data, "world", 5) == 0);
}

MEL_TEST(from_cstr_empty, .tags = "string")
{
    str8 s = str8_from_cstr("");
    MEL_ASSERT_EQ(s.len, 0);
    MEL_ASSERT_NOT_NULL(s.data);
}

MEL_TEST(from_cstr_null, .tags = "string")
{
    str8 s = str8_from_cstr(nullptr);
    MEL_ASSERT(str8_is_empty(s));
    MEL_ASSERT_NULL(s.data);
}

MEL_TEST(equals_positive, .tags = "string")
{
    MEL_ASSERT(str8_equals(S8("abc"), S8("abc")));
}

MEL_TEST(equals_negative, .tags = "string")
{
    MEL_ASSERT(!str8_equals(S8("abc"), S8("xyz")));
    MEL_ASSERT(!str8_equals(S8("abc"), S8("ab")));
}

MEL_TEST(compare_ordering, .tags = "string")
{
    MEL_ASSERT_LT(str8_compare(S8("abc"), S8("abd")), 0);
    MEL_ASSERT_GT(str8_compare(S8("abd"), S8("abc")), 0);
    MEL_ASSERT_EQ(str8_compare(S8("abc"), S8("abc")), 0);
    MEL_ASSERT_LT(str8_compare(S8("ab"), S8("abc")), 0);
    MEL_ASSERT_GT(str8_compare(S8("abc"), S8("ab")), 0);
}

MEL_TEST(starts_with, .tags = "string")
{
    MEL_ASSERT(str8_starts_with(S8("hello world"), S8("hello")));
    MEL_ASSERT(!str8_starts_with(S8("hello"), S8("hello world")));
    MEL_ASSERT(str8_starts_with(S8("hello"), S8("")));
}

MEL_TEST(ends_with, .tags = "string")
{
    MEL_ASSERT(str8_ends_with(S8("hello world"), S8("world")));
    MEL_ASSERT(!str8_ends_with(S8("world"), S8("hello world")));
    MEL_ASSERT(str8_ends_with(S8("hello"), S8("")));
}

MEL_TEST(contains_and_find, .tags = "string")
{
    str8 haystack = S8("the quick brown fox");
    MEL_ASSERT(str8_contains(haystack, S8("quick")));
    MEL_ASSERT(!str8_contains(haystack, S8("slow")));
    MEL_ASSERT_EQ(str8_find(haystack, S8("quick")), 4);
    MEL_ASSERT_EQ(str8_find(haystack, S8("slow")), -1);
    MEL_ASSERT_EQ(str8_find(haystack, S8("")), 0);
}

MEL_TEST(rfind, .tags = "string")
{
    str8 s = S8("abcabc");
    MEL_ASSERT_EQ(str8_rfind(s, S8("abc")), 3);
    MEL_ASSERT_EQ(str8_rfind(s, S8("xyz")), -1);
}

MEL_TEST(slice_bounds, .tags = "string")
{
    str8 s = S8("hello world");
    str8 sub = str8_slice(s, 6, 5);
    MEL_ASSERT(str8_equals(sub, S8("world")));

    str8 pre = str8_prefix(s, 5);
    MEL_ASSERT(str8_equals(pre, S8("hello")));

    str8 suf = str8_suffix(s, 5);
    MEL_ASSERT(str8_equals(suf, S8("world")));
}

MEL_TEST(trim_whitespace, .tags = "string")
{
    str8 s = S8("  hello  ");
    str8 trimmed = str8_trim(s);
    MEL_ASSERT(str8_equals(trimmed, S8("hello")));

    str8 left = str8_trim_left(S8("\t\nhello"));
    MEL_ASSERT(str8_equals(left, S8("hello")));

    str8 right = str8_trim_right(S8("hello\r\n"));
    MEL_ASSERT(str8_equals(right, S8("hello")));
}

MEL_TEST(hash_matches_xxh3, .tags = "string")
{
    str8 s = S8("hash me");
    u64 expected = mel_xxh3_64(s.data, (usize)s.len);
    MEL_ASSERT_EQ(str8_hash(s), expected);
}

MEL_TEST(dup_creates_copy, .tags = "string")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    str8 original = S8("copy me");
    str8 copy = str8_dup(original, heap);

    MEL_ASSERT(str8_equals(original, copy));
    MEL_ASSERT(original.data != copy.data);

    mel_dealloc(heap, copy.data);
}

MEL_TEST(to_buf_truncation, .tags = "string")
{
    str8 s = S8("hello world");
    char buf[6];
    size written = str8_to_buf(s, buf, sizeof(buf));
    MEL_ASSERT_EQ(written, 5);
    MEL_ASSERT_STR_EQ(buf, "hello");
}

MEL_TEST(to_buf_exact, .tags = "string")
{
    str8 s = S8("hi");
    char buf[3];
    size written = str8_to_buf(s, buf, sizeof(buf));
    MEL_ASSERT_EQ(written, 2);
    MEL_ASSERT_STR_EQ(buf, "hi");
}

MEL_TEST(is_empty, .tags = "string")
{
    MEL_ASSERT(str8_is_empty(STR8_EMPTY));
    MEL_ASSERT(str8_is_empty((str8){0}));
    MEL_ASSERT(str8_is_empty(str8_from_cstr(nullptr)));
    MEL_ASSERT(!str8_is_empty(S8("x")));
}

MEL_TEST(fmt, .tags = "string")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    str8 s = str8_fmt(heap, "hello %s %d", "world", 42);
    MEL_ASSERT(str8_equals(s, S8("hello world 42")));
    mel_dealloc(heap, s.data);
}

MEL_TEST(to_cstr, .tags = "string")
{
    const Mel_Alloc* heap = mel_alloc_heap();
    str8 s = S8("hello");
    const char* cstr = str8_to_cstr(s, heap);
    MEL_ASSERT_STR_EQ(cstr, "hello");
    mel_dealloc(heap, (void*)cstr);
}
