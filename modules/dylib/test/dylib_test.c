#include <dylib/dylib.h>
#include <dylib/status.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <core/platform.h>
#include <test/test.h>

#include <string.h>

#if MEL_PLATFORM_APPLE
#define DYLIB_TEST_PATH "/usr/lib/libSystem.B.dylib"
#define DYLIB_TEST_NAME "System"
#elif MEL_PLATFORM_LINUX || MEL_PLATFORM_ANDROID
#define DYLIB_TEST_PATH "libc.so.6"
#define DYLIB_TEST_NAME "m"
#elif MEL_PLATFORM_WINDOWS
#define DYLIB_TEST_PATH "kernel32.dll"
#define DYLIB_TEST_NAME "kernel32"
#endif

typedef size_t (*Strlen_Fn)(const char*);

MEL_TEST(dylib, status_predicates)
{
    MEL_EXPECT(mel_dylib_status_ok(MEL_DYLIB_OK));
    MEL_EXPECT(!mel_dylib_status_ok(MEL_DYLIB_ERROR | MEL_DYLIB_NOT_FOUND));
    MEL_EXPECT(mel_dylib_status_failed(MEL_DYLIB_ERROR | MEL_DYLIB_NO_SYMBOL));
    MEL_EXPECT(mel_dylib_status_not_found(MEL_DYLIB_ERROR | MEL_DYLIB_NOT_FOUND));
    MEL_EXPECT(mel_dylib_status_no_symbol(MEL_DYLIB_ERROR | MEL_DYLIB_NO_SYMBOL));
    MEL_EXPECT(!mel_dylib_status_no_symbol(MEL_DYLIB_OK));
    MEL_EXPECT(mel_dylib_status_unavailable(MEL_DYLIB_ERROR | MEL_DYLIB_UNAVAILABLE));
}

MEL_TEST(dylib, open_requires_exactly_one_locator)
{
    Mel_Dylib_Open_Result none = mel_dylib_open();
    MEL_EXPECT_NULL(none.value);
    MEL_EXPECT(mel_dylib_status_failed(none.status));

    Mel_Dylib_Open_Result both = mel_dylib_open(.path = "x", .name = "y");
    MEL_EXPECT_NULL(both.value);
    MEL_EXPECT(mel_dylib_status_failed(both.status));
}

MEL_TEST(dylib, open_missing_library_is_loud)
{
    if (!mel_dylib_available())
        MEL_SKIP("no dynamic-loading backend on this platform");

    Mel_Dylib_Open_Result r = mel_dylib_open(.path = "/no/such/melody-bogus-library.so", .alloc = mel_alloc_heap());
    MEL_EXPECT_NULL(r.value);
    MEL_EXPECT(mel_dylib_status_failed(r.status));
    MEL_EXPECT(mel_dylib_status_not_found(r.status));
}

#if defined(DYLIB_TEST_PATH)

MEL_TEST(dylib, open_by_path_resolve_and_use)
{
    if (!mel_dylib_available())
        MEL_SKIP("no dynamic-loading backend on this platform");

    Mel_Dylib_Open_Result r = mel_dylib_open(.path = DYLIB_TEST_PATH, .alloc = mel_alloc_heap());
    MEL_REQUIRE(mel_dylib_status_ok(r.status));
    MEL_REQUIRE_NOT_NULL(r.value);
    MEL_EXPECT_NOT_NULL(mel_dylib_native(r.value));
    MEL_REQUIRE_NOT_NULL(mel_dylib_path(r.value));
    MEL_EXPECT_STR_EQ(mel_dylib_path(r.value), DYLIB_TEST_PATH);

    Mel_Dylib_Symbol sym = mel_dylib_symbol(r.value, "strlen");
    MEL_REQUIRE(mel_dylib_status_ok(sym.status));
    MEL_REQUIRE_NOT_NULL(sym.addr);

    Strlen_Fn fn = (Strlen_Fn)sym.addr;
    MEL_EXPECT_EQ(fn("melody"), (size_t)6);

    mel_dylib_close(r.value);
}

MEL_TEST(dylib, open_by_name_decorates)
{
    if (!mel_dylib_available())
        MEL_SKIP("no dynamic-loading backend on this platform");

    Mel_Dylib_Open_Result r = mel_dylib_open(.name = DYLIB_TEST_NAME, .alloc = mel_alloc_heap());
    MEL_REQUIRE(mel_dylib_status_ok(r.status));
    MEL_REQUIRE_NOT_NULL(r.value);

    const char* path = mel_dylib_path(r.value);
    MEL_REQUIRE_NOT_NULL(path);
    MEL_EXPECT(strstr(path, DYLIB_TEST_NAME) != NULL);

    mel_dylib_close(r.value);
}

MEL_TEST(dylib, missing_symbol_is_loud)
{
    if (!mel_dylib_available())
        MEL_SKIP("no dynamic-loading backend on this platform");

    Mel_Dylib_Open_Result r = mel_dylib_open(.path = DYLIB_TEST_PATH, .alloc = mel_alloc_heap());
    MEL_REQUIRE(mel_dylib_status_ok(r.status));
    MEL_REQUIRE_NOT_NULL(r.value);

    Mel_Dylib_Symbol sym = mel_dylib_symbol(r.value, "mel_dylib_no_such_symbol_xyz");
    MEL_EXPECT_NULL(sym.addr);
    MEL_EXPECT(mel_dylib_status_failed(sym.status));
    MEL_EXPECT(mel_dylib_status_no_symbol(sym.status));

    mel_dylib_close(r.value);
}

#endif

MEL_TEST(dylib, symbol_on_bad_handle_is_loud)
{
    Mel_Dylib_Symbol sym = mel_dylib_symbol(NULL, "strlen");
    MEL_EXPECT_NULL(sym.addr);
    MEL_EXPECT(mel_dylib_status_failed(sym.status));
    MEL_EXPECT((sym.status & MEL_DYLIB_BAD_HANDLE) != 0u);
}

MEL_TEST(dylib, empty_symbol_name_is_loud)
{
    if (!mel_dylib_available())
        MEL_SKIP("no dynamic-loading backend on this platform");
#if defined(DYLIB_TEST_PATH)
    Mel_Dylib_Open_Result r = mel_dylib_open(.path = DYLIB_TEST_PATH, .alloc = mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(r.value);
    Mel_Dylib_Symbol sym = mel_dylib_symbol(r.value, "");
    MEL_EXPECT_NULL(sym.addr);
    MEL_EXPECT(mel_dylib_status_no_symbol(sym.status));
    mel_dylib_close(r.value);
#else
    MEL_SKIP("no known test library for this platform");
#endif
}

MEL_TEST(dylib, close_null_is_safe)
{
    mel_dylib_close(NULL);
    MEL_EXPECT(true);
}
