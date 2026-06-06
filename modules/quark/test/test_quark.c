#include <test/test.h>

#include <quark/quark.h>
#include <string/str8.h>
#include <allocator/heap.h>

#include <stdio.h>

MEL_TEST(quark, intern_is_idempotent)
{
    Mel_Quark_Table* t = mel_quark_table_create(mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(t);

    Mel_Quark first = mel_quark_intern(t, S8("identifier"));
    Mel_Quark again = mel_quark_intern(t, S8("identifier"));

    MEL_EXPECT_NEQ(first, MEL_QUARK_NONE);
    MEL_EXPECT_EQ(first, again);
    MEL_EXPECT_EQ(mel_quark_count(t), 1u);
    MEL_EXPECT_EQ_STR8(mel_quark_get(t, first), S8("identifier"));

    mel_quark_table_destroy(t);
}

MEL_TEST(quark, distinct_strings_distinct_quarks)
{
    Mel_Quark_Table* t = mel_quark_table_create(mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(t);

    Mel_Quark a = mel_quark_intern(t, S8("alpha"));
    Mel_Quark b = mel_quark_intern(t, S8("beta"));
    Mel_Quark g = mel_quark_intern(t, S8("gamma"));

    MEL_EXPECT_NEQ(a, b);
    MEL_EXPECT_NEQ(b, g);
    MEL_EXPECT_NEQ(a, g);
    MEL_EXPECT_EQ(mel_quark_count(t), 3u);

    MEL_EXPECT_EQ_STR8(mel_quark_get(t, a), S8("alpha"));
    MEL_EXPECT_EQ_STR8(mel_quark_get(t, b), S8("beta"));
    MEL_EXPECT_EQ_STR8(mel_quark_get(t, g), S8("gamma"));

    mel_quark_table_destroy(t);
}

MEL_TEST(quark, lookup_never_inserts)
{
    Mel_Quark_Table* t = mel_quark_table_create(mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(t);

    MEL_EXPECT_EQ(mel_quark_lookup(t, S8("absent")), MEL_QUARK_NONE);
    MEL_EXPECT_EQ(mel_quark_count(t), 0u);

    Mel_Quark q = mel_quark_intern(t, S8("present"));
    MEL_EXPECT_EQ(mel_quark_lookup(t, S8("present")), q);
    MEL_EXPECT_EQ(mel_quark_lookup(t, S8("absent")), MEL_QUARK_NONE);
    MEL_EXPECT_EQ(mel_quark_count(t), 1u);

    mel_quark_table_destroy(t);
}

MEL_TEST(quark, empty_string_never_interns)
{
    Mel_Quark_Table* t = mel_quark_table_create(mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(t);

    MEL_EXPECT_EQ(mel_quark_intern(t, STR8_EMPTY), MEL_QUARK_NONE);
    MEL_EXPECT_EQ(mel_quark_intern(t, S8("")), MEL_QUARK_NONE);
    MEL_EXPECT_EQ(mel_quark_lookup(t, STR8_EMPTY), MEL_QUARK_NONE);
    MEL_EXPECT_EQ(mel_quark_count(t), 0u);

    mel_quark_table_destroy(t);
}

MEL_TEST(quark, get_rejects_invalid_quark)
{
    Mel_Quark_Table* t = mel_quark_table_create(mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(t);

    Mel_Quark q = mel_quark_intern(t, S8("only"));

    MEL_EXPECT(str8_is_empty(mel_quark_get(t, MEL_QUARK_NONE)));
    MEL_EXPECT(str8_is_empty(mel_quark_get(t, q + 1u)));
    MEL_EXPECT(str8_is_empty(mel_quark_get(t, (Mel_Quark)9999)));

    mel_quark_table_destroy(t);
}

MEL_TEST(quark, interned_bytes_are_owned)
{
    Mel_Quark_Table* t = mel_quark_table_create(mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(t);

    u8        scratch[8] = { 'm', 'u', 't', 'a', 'b', 'l', 'e', 0 };
    str8      src = str8_from_parts(scratch, 7);
    Mel_Quark q = mel_quark_intern(t, src);

    for (int i = 0; i < 7; i++)
        scratch[i] = 'X';

    MEL_EXPECT_EQ_STR8(mel_quark_get(t, q), S8("mutable"));
    MEL_EXPECT_EQ(mel_quark_lookup(t, S8("mutable")), q);
    MEL_EXPECT_EQ(mel_quark_lookup(t, S8("XXXXXXX")), MEL_QUARK_NONE);

    mel_quark_table_destroy(t);
}

MEL_TEST(quark, null_table_is_safe)
{
    MEL_EXPECT_EQ(mel_quark_intern(NULL, S8("x")), MEL_QUARK_NONE);
    MEL_EXPECT_EQ(mel_quark_lookup(NULL, S8("x")), MEL_QUARK_NONE);
    MEL_EXPECT(str8_is_empty(mel_quark_get(NULL, (Mel_Quark)1)));
    MEL_EXPECT_EQ(mel_quark_count(NULL), 0u);
}

MEL_TEST(quark, grow_preserves_every_entry)
{
    enum
    {
        N = 500
    };

    Mel_Quark_Table* t = mel_quark_table_create(mel_alloc_heap());
    MEL_REQUIRE_NOT_NULL(t);

    Mel_Quark quarks[N];
    char      buf[32];

    for (int i = 0; i < N; i++)
    {
        snprintf(buf, sizeof(buf), "key-%d", i);
        quarks[i] = mel_quark_intern(t, str8_from_cstr(buf));
        MEL_REQUIRE_NEQ(quarks[i], MEL_QUARK_NONE);
    }

    MEL_REQUIRE_EQ(mel_quark_count(t), (u32)N);

    for (int i = 0; i < N; i++)
    {
        snprintf(buf, sizeof(buf), "key-%d", i);
        str8 key = str8_from_cstr(buf);

        MEL_EXPECT_EQ(mel_quark_lookup(t, key), quarks[i]);
        MEL_EXPECT_EQ(mel_quark_intern(t, key), quarks[i]);
        MEL_EXPECT_EQ_STR8(mel_quark_get(t, quarks[i]), key);
    }

    MEL_EXPECT_EQ(mel_quark_count(t), (u32)N);

    for (int i = 0; i < N; i++)
        for (int j = i + 1; j < N; j++)
            if (quarks[i] == quarks[j])
                MEL_FAIL("distinct keys collided to one quark");

    mel_quark_table_destroy(t);
}
