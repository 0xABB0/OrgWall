#include "../melody/test.harness.h"
#include "../melody/async.aio.h"
#include "../melody/async.signal.h"

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static const char* TEST_CONTENT = "Hello from Melody's async IO! This is test data that we read back.";

static void spin_wait_counter(Mel_Counter* c)
{
    for (i32 spins = 0; spins < 5000000; spins++)
    {
        if (mel__signal_counter(atomic_load_explicit(&c->signal.state, memory_order_acquire)) == 0)
            return;
    }
    MEL_FAIL("counter did not reach zero in time");
}

static int create_temp_file(const char* path, const void* data, size_t len)
{
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    assert(fd >= 0);
    ssize_t written = write(fd, data, len);
    assert(written == (ssize_t)len);
    close(fd);

    fd = open(path, O_RDONLY);
    assert(fd >= 0);
    return fd;
}

MEL_TEST(aio_read_known_file, .tags = "async")
{
    const char* path = "/tmp/mel_test_aio_read.bin";
    size_t content_len = strlen(TEST_CONTENT);
    int fd = create_temp_file(path, TEST_CONTENT, content_len);

    char buf[256] = {0};
    i64 result = 0;
    i32 error = -1;
    Mel_Counter counter = MEL_COUNTER_INIT;

    mel_counter_increment(&counter);

    Mel_Aio_Op op = {
        .fd = fd,
        .buf = buf,
        .size = (i64)content_len,
        .offset = 0,
        .counter = &counter,
        .result = &result,
        .error = &error,
    };

    mel_aio_submit(&op);

    for (i32 spins = 0; spins < 5000000; spins++)
    {
        i32 drained = mel_aio_drain();
        if (drained > 0) break;
    }

    spin_wait_counter(&counter);

    MEL_ASSERT_EQ(error, 0);
    MEL_ASSERT_EQ(result, (i64)content_len);
    MEL_ASSERT(memcmp(buf, TEST_CONTENT, content_len) == 0);

    close(fd);
    unlink(path);
}

MEL_TEST(aio_read_at_offset, .tags = "async")
{
    const char* path = "/tmp/mel_test_aio_offset.bin";
    size_t content_len = strlen(TEST_CONTENT);
    int fd = create_temp_file(path, TEST_CONTENT, content_len);

    i64 offset = 6;
    i64 read_size = 4;
    char buf[16] = {0};
    i64 result = 0;
    i32 error = -1;
    Mel_Counter counter = MEL_COUNTER_INIT;
    mel_counter_increment(&counter);

    Mel_Aio_Op op = {
        .fd = fd,
        .buf = buf,
        .size = read_size,
        .offset = offset,
        .counter = &counter,
        .result = &result,
        .error = &error,
    };

    mel_aio_submit(&op);

    for (i32 spins = 0; spins < 5000000; spins++)
    {
        i32 drained = mel_aio_drain();
        if (drained > 0) break;
    }

    spin_wait_counter(&counter);

    MEL_ASSERT_EQ(error, 0);
    MEL_ASSERT_EQ(result, read_size);
    MEL_ASSERT(memcmp(buf, "from", 4) == 0);

    close(fd);
    unlink(path);
}

MEL_TEST(aio_read_bad_fd, .tags = "async")
{
    char buf[64] = {0};
    i64 result = 0;
    i32 error = 0;
    Mel_Counter counter = MEL_COUNTER_INIT;
    mel_counter_increment(&counter);

    Mel_Aio_Op op = {
        .fd = 99999,
        .buf = buf,
        .size = 16,
        .offset = 0,
        .counter = &counter,
        .result = &result,
        .error = &error,
    };

    mel_aio_submit(&op);

    for (i32 spins = 0; spins < 5000000; spins++)
    {
        i32 drained = mel_aio_drain();
        if (drained > 0) break;
    }

    spin_wait_counter(&counter);

    MEL_ASSERT_NEQ(error, 0);
}

MEL_TEST(aio_concurrent_reads, .tags = "async")
{
    #define CONCURRENT_COUNT 10
    const char* paths[CONCURRENT_COUNT];
    int fds[CONCURRENT_COUNT];
    char write_bufs[CONCURRENT_COUNT][64];
    char read_bufs[CONCURRENT_COUNT][64];
    i64 results[CONCURRENT_COUNT];
    i32 errors[CONCURRENT_COUNT];
    Mel_Aio_Op ops[CONCURRENT_COUNT];

    Mel_Counter counter = MEL_COUNTER_INIT;

    for (i32 i = 0; i < CONCURRENT_COUNT; i++)
    {
        static char path_bufs[CONCURRENT_COUNT][64];
        snprintf(path_bufs[i], sizeof(path_bufs[i]), "/tmp/mel_test_aio_conc_%d.bin", i);
        paths[i] = path_bufs[i];

        snprintf(write_bufs[i], sizeof(write_bufs[i]), "concurrent_data_%d", i);
        fds[i] = create_temp_file(paths[i], write_bufs[i], strlen(write_bufs[i]));

        memset(read_bufs[i], 0, sizeof(read_bufs[i]));
        results[i] = 0;
        errors[i] = -1;

        mel_counter_increment(&counter);

        ops[i] = (Mel_Aio_Op){
            .fd = fds[i],
            .buf = read_bufs[i],
            .size = (i64)strlen(write_bufs[i]),
            .offset = 0,
            .counter = &counter,
            .result = &results[i],
            .error = &errors[i],
        };
    }

    for (i32 i = 0; i < CONCURRENT_COUNT; i++)
        mel_aio_submit(&ops[i]);

    i32 total_drained = 0;
    for (i32 spins = 0; spins < 10000000 && total_drained < CONCURRENT_COUNT; spins++)
        total_drained += mel_aio_drain();

    spin_wait_counter(&counter);

    MEL_ASSERT_EQ(total_drained, CONCURRENT_COUNT);

    for (i32 i = 0; i < CONCURRENT_COUNT; i++)
    {
        MEL_ASSERT_EQ(errors[i], 0);
        MEL_ASSERT_EQ(results[i], (i64)strlen(write_bufs[i]));
        MEL_ASSERT(memcmp(read_bufs[i], write_bufs[i], (size_t)results[i]) == 0);

        close(fds[i]);
        unlink(paths[i]);
    }

    #undef CONCURRENT_COUNT
}

MEL_TEST(aio_drain_returns_count, .tags = "async")
{
    MEL_ASSERT_EQ(mel_aio_drain(), 0);
}

MEL_TEST(aio_large_read, .tags = "async")
{
    const char* path = "/tmp/mel_test_aio_large.bin";
    #define LARGE_SIZE (64 * 1024)
    static u8 write_data[LARGE_SIZE];
    static u8 read_data[LARGE_SIZE];

    for (i32 i = 0; i < LARGE_SIZE; i++)
        write_data[i] = (u8)(i & 0xFF);

    int fd = create_temp_file(path, write_data, LARGE_SIZE);
    memset(read_data, 0, LARGE_SIZE);

    i64 result = 0;
    i32 error = -1;
    Mel_Counter counter = MEL_COUNTER_INIT;
    mel_counter_increment(&counter);

    Mel_Aio_Op op = {
        .fd = fd,
        .buf = read_data,
        .size = LARGE_SIZE,
        .offset = 0,
        .counter = &counter,
        .result = &result,
        .error = &error,
    };

    mel_aio_submit(&op);

    for (i32 spins = 0; spins < 10000000; spins++)
    {
        if (mel_aio_drain() > 0) break;
    }

    spin_wait_counter(&counter);

    MEL_ASSERT_EQ(error, 0);
    MEL_ASSERT_EQ(result, LARGE_SIZE);
    MEL_ASSERT(memcmp(read_data, write_data, LARGE_SIZE) == 0);

    close(fd);
    unlink(path);
    #undef LARGE_SIZE
}
