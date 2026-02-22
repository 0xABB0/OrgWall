#include "../melody/test.harness.h"
#include "../melody/string.path.h"
#include "../melody/string.str8.h"

MEL_TEST(path_normalize_dot, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/foo/./bar"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo/bar")));
}

MEL_TEST(path_normalize_dotdot, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/foo/baz/../bar"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo/bar")));
}

MEL_TEST(path_normalize_double_slash, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/foo//bar///baz"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo/bar/baz")));
}

MEL_TEST(path_normalize_backslash, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("foo\\bar\\baz"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo/bar/baz")));
}

MEL_TEST(path_normalize_trailing_slash, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/foo/bar/"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo/bar")));
}

MEL_TEST(path_normalize_root_escape_clamps, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/../../../etc/passwd"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/etc/passwd")));
}

MEL_TEST(path_normalize_pure_dotdot, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/../../.."), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/")));
}

MEL_TEST(path_normalize_empty, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8(""), buf, sizeof(buf));
    MEL_ASSERT_EQ(result.len, (size)0);
}

MEL_TEST(path_normalize_just_slash, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/")));
}

MEL_TEST(path_normalize_complex, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_normalize(S8("/a/b/../c/./d//e"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/a/c/d/e")));
}

MEL_TEST(path_parent_basic, .tags = "string")
{
    str8 result = mel_path_parent(S8("/foo/bar"));
    MEL_ASSERT(str8_equals(result, S8("/foo")));
}

MEL_TEST(path_parent_root, .tags = "string")
{
    str8 result = mel_path_parent(S8("/"));
    MEL_ASSERT(str8_equals(result, S8("/")));
}

MEL_TEST(path_parent_trailing_slash, .tags = "string")
{
    str8 result = mel_path_parent(S8("/foo/bar/"));
    MEL_ASSERT(str8_equals(result, S8("/foo")));
}

MEL_TEST(path_filename_basic, .tags = "string")
{
    str8 result = mel_path_filename(S8("/foo/bar/baz.txt"));
    MEL_ASSERT(str8_equals(result, S8("baz.txt")));
}

MEL_TEST(path_filename_no_dir, .tags = "string")
{
    str8 result = mel_path_filename(S8("file.txt"));
    MEL_ASSERT(str8_equals(result, S8("file.txt")));
}

MEL_TEST(path_filename_empty, .tags = "string")
{
    str8 result = mel_path_filename(S8(""));
    MEL_ASSERT_EQ(result.len, (size)0);
}

MEL_TEST(path_extension_basic, .tags = "string")
{
    str8 result = mel_path_extension(S8("/foo/bar.txt"));
    MEL_ASSERT(str8_equals(result, S8("txt")));
}

MEL_TEST(path_extension_multiple_dots, .tags = "string")
{
    str8 result = mel_path_extension(S8("/foo/bar.tar.gz"));
    MEL_ASSERT(str8_equals(result, S8("gz")));
}

MEL_TEST(path_extension_none, .tags = "string")
{
    str8 result = mel_path_extension(S8("/foo/bar"));
    MEL_ASSERT_EQ(result.len, (size)0);
}

MEL_TEST(path_is_absolute_yes, .tags = "string")
{
    MEL_ASSERT(mel_path_is_absolute(S8("/foo/bar")));
    MEL_ASSERT(mel_path_is_absolute(S8("\\windows")));
}

MEL_TEST(path_is_absolute_no, .tags = "string")
{
    MEL_ASSERT(!mel_path_is_absolute(S8("relative/path")));
    MEL_ASSERT(!mel_path_is_absolute(S8("")));
}

MEL_TEST(path_join_basic, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_join(S8("/foo"), S8("bar/baz"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo/bar/baz")));
}

MEL_TEST(path_join_absolute_relative, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_join(S8("/base"), S8("/absolute"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/absolute")));
}

MEL_TEST(path_join_empty_base, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_join(S8(""), S8("foo/bar"), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo/bar")));
}

MEL_TEST(path_join_empty_relative, .tags = "string")
{
    u8 buf[256];
    str8 result = mel_path_join(S8("/foo"), S8(""), buf, sizeof(buf));
    MEL_ASSERT(str8_equals(result, S8("/foo")));
}
