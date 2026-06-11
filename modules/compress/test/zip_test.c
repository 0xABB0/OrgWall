#include <compress/zip.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <string/str8.h>
#include <test/test.h>

#include <string.h>

MEL_TEST(zip, write_then_read_roundtrip)
{
    const Mel_Alloc*    alloc = mel_alloc_heap();
    Mel_Compress_Status st = MEL_COMPRESS_OK;

    Mel_Zip_Writer* w = mel_zip_writer_create(alloc, &st);
    MEL_REQUIRE(w != NULL);

    str8 text = S8("hello from the archive, with enough repetition repetition repetition to compress");
    u8   binary[256];
    for (usize i = 0; i < sizeof binary; i++)
        binary[i] = (u8)(i * 7);

    MEL_REQUIRE(mel_compress_status_ok(mel_zip_add(w, S8("docs/hello.txt"), text, 9)));
    MEL_REQUIRE(mel_compress_status_ok(mel_zip_add(w, S8("data.bin"), str8_from_parts(binary, sizeof binary), 6)));
    MEL_REQUIRE(mel_compress_status_ok(mel_zip_add(w, S8("empty.txt"), STR8_EMPTY, 1)));

    Mel_Compress_Result archive = mel_zip_finish(w);
    MEL_REQUIRE(mel_compress_status_ok(archive.status));
    MEL_REQUIRE_GT(archive.len, (usize)0);

    Mel_Zip_Reader* r = mel_zip_open(str8_from_parts(archive.data, (size)archive.len), alloc, &st);
    MEL_REQUIRE(r != NULL);
    MEL_REQUIRE_EQ(mel_zip_count(r), (usize)3);

    bool found_text = false;
    for (usize i = 0; i < mel_zip_count(r); i++)
    {
        Mel_Zip_Entry e = mel_zip_entry(r, i);
        if (!str8_equals(e.name, S8("docs/hello.txt")))
            continue;
        found_text = true;
        MEL_EXPECT_EQ(e.size, (u64)text.len);
        Mel_Compress_Result data = mel_zip_extract(r, i);
        MEL_REQUIRE(mel_compress_status_ok(data.status));
        MEL_REQUIRE_EQ(data.len, (usize)text.len);
        MEL_EXPECT_EQ(memcmp(data.data, text.data, data.len), 0);
        mel_compress_result_free(&data, alloc);
    }
    MEL_EXPECT(found_text);

    mel_zip_close(r);
    mel_compress_result_free(&archive, alloc);
}

MEL_TEST(zip, open_rejects_garbage)
{
    const Mel_Alloc*    alloc = mel_alloc_heap();
    Mel_Compress_Status st = MEL_COMPRESS_OK;
    Mel_Zip_Reader*     r = mel_zip_open(S8("this is definitely not a zip archive"), alloc, &st);
    MEL_EXPECT(r == NULL);
    MEL_EXPECT(mel_compress_status_failed(st));
}

MEL_TEST(zip, add_rejects_bad_level)
{
    const Mel_Alloc*    alloc = mel_alloc_heap();
    Mel_Compress_Status st = MEL_COMPRESS_OK;
    Mel_Zip_Writer*     w = mel_zip_writer_create(alloc, &st);
    MEL_REQUIRE(w != NULL);
    MEL_EXPECT(mel_compress_status_failed(mel_zip_add(w, S8("a.txt"), S8("abc"), 99)));
    Mel_Compress_Result archive = mel_zip_finish(w);
    mel_compress_result_free(&archive, alloc);
}
