#include <gtest/gtest.h>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <cstring>
#include <chrono>
#include <set>

#include "async_io.h"
#include "result_read.h"
#include "result_write.h"
#include "data_struct.h"

using namespace mAsyncDiskIO;

// ============================================================================
// Test Fixture: Creates a temporary file for I/O testing
// ============================================================================
class AsyncIOTest : public ::testing::Test {
protected:
    std::string temp_file_path;
    int fd = -1;
    const char* test_data = "Hello, io_uring async I/O!";
    size_t test_data_len = 0;

    void SetUp() override {
        test_data_len = strlen(test_data);
        temp_file_path = "/tmp/async_io_test_" + std::to_string(getpid()) + ".dat";
        fd = open(temp_file_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(fd, 0) << "Failed to create temp file";
        // Pre-write data so we have something to read
        ssize_t written = write(fd, test_data, test_data_len);
        ASSERT_EQ(static_cast<size_t>(written), test_data_len);
        fsync(fd);
    }

    void TearDown() override {
        if (fd >= 0) close(fd);
        std::filesystem::remove(temp_file_path);
    }
};

// ============================================================================
// Test Group 1: async_io construction / destruction
// ============================================================================

TEST_F(AsyncIOTest, ConstructWithDefaultDepth) {
    EXPECT_NO_THROW({
        async_io io;
    });
}

TEST_F(AsyncIOTest, ConstructWithCustomDepth) {
    EXPECT_NO_THROW({
        async_io io(128);
    });
}

// ============================================================================
// Test Group 2: write operation
// ============================================================================

TEST_F(AsyncIOTest, WriteBasic) {
    async_io io;

    uint8_t* buf = new uint8_t[test_data_len + 1];
    memcpy(buf, test_data, test_data_len);
    unique_buf ubuf = make_unique_buf(buf);

    auto weak = io.write(fd, std::move(ubuf), test_data_len, 100, 42);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    int res = shared->wait();
    EXPECT_EQ(res, static_cast<int>(test_data_len));
    EXPECT_EQ(shared->size(), test_data_len);
    EXPECT_FALSE(shared->empty());

    auto ud = shared->user_data();
    ASSERT_TRUE(ud.has_value());
    EXPECT_EQ(ud.value(), 42u);

    shared->finish();
}

TEST_F(AsyncIOTest, WriteAtOffsetZero) {
    async_io io;

    uint8_t* buf = new uint8_t[test_data_len + 1];
    memcpy(buf, test_data, test_data_len);
    unique_buf ubuf = make_unique_buf(buf);

    auto weak = io.write(fd, std::move(ubuf), test_data_len, 0, 99);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    int res = shared->wait();
    EXPECT_EQ(res, static_cast<int>(test_data_len));

    auto ud = shared->user_data();
    ASSERT_TRUE(ud.has_value());
    EXPECT_EQ(ud.value(), 99u);

    shared->finish();
}

TEST_F(AsyncIOTest, WritePeekReturnsFinish) {
    async_io io;

    uint8_t* buf = new uint8_t[test_data_len + 1];
    memcpy(buf, test_data, test_data_len);
    unique_buf ubuf = make_unique_buf(buf);

    auto weak = io.write(fd, std::move(ubuf), test_data_len, 0, 1);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    // Poll until operation completes
    state s = state::UNFINISHED;
    int attempts = 1000;
    while (s == state::UNFINISHED && attempts-- > 0) {
        s = shared->peek();
    }
    EXPECT_EQ(s, state::FINISH);
    EXPECT_FALSE(shared->empty());

    shared->finish();
}

TEST_F(AsyncIOTest, WriteUserDataNotSet) {
    async_io io;

    uint8_t* buf = new uint8_t[test_data_len + 1];
    memcpy(buf, test_data, test_data_len);
    unique_buf ubuf = make_unique_buf(buf);

    auto weak = io.write(fd, std::move(ubuf), test_data_len, 0, 0);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    shared->wait();
    auto ud = shared->user_data();
    ASSERT_TRUE(ud.has_value());
    EXPECT_EQ(ud.value(), 0u);

    shared->finish();
}

TEST_F(AsyncIOTest, WriteSizeBeforeWaitReturnsZero) {
    async_io io;

    uint8_t* buf = new uint8_t[test_data_len + 1];
    memcpy(buf, test_data, test_data_len);
    unique_buf ubuf = make_unique_buf(buf);

    auto weak = io.write(fd, std::move(ubuf), test_data_len, 0, 1);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    EXPECT_EQ(shared->size(), 0u);  // CQE not yet available
    shared->wait();
    EXPECT_EQ(shared->size(), test_data_len);

    shared->finish();
}

// ============================================================================
// Test Group 3: read operation
// ============================================================================

TEST_F(AsyncIOTest, ReadBasic) {
    async_io io;

    auto weak = io.read(fd, test_data_len, 0, 77);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    int res = shared->wait();
    EXPECT_EQ(res, static_cast<int>(test_data_len));
    EXPECT_EQ(shared->size(), test_data_len);
    EXPECT_FALSE(shared->empty());

    auto ud = shared->user_data();
    ASSERT_TRUE(ud.has_value());
    EXPECT_EQ(ud.value(), 77u);

    unique_buf data = shared->transfer_data();
    ASSERT_NE(data.get(), nullptr);
    EXPECT_EQ(std::memcmp(data.get(), test_data, test_data_len), 0);

    shared->finish();
}

TEST_F(AsyncIOTest, ReadAtOffset) {
    async_io io;

    // Write data at offset 512 first
    uint8_t* wbuf = new uint8_t[test_data_len + 1];
    memcpy(wbuf, test_data, test_data_len);
    unique_buf ubuf = make_unique_buf(wbuf);
    auto wweak = io.write(fd, std::move(ubuf), test_data_len, 512, 1);
    auto wshared = wweak.lock();
    wshared->wait();
    wshared->finish();

    // Read it back from offset 512
    auto weak = io.read(fd, test_data_len, 512, 88);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    int res = shared->wait();
    EXPECT_EQ(res, static_cast<int>(test_data_len));

    unique_buf data = shared->transfer_data();
    ASSERT_NE(data.get(), nullptr);
    EXPECT_EQ(std::memcmp(data.get(), test_data, test_data_len), 0);

    shared->finish();
}

TEST_F(AsyncIOTest, ReadPeekReturnsFinish) {
    async_io io;

    auto weak = io.read(fd, test_data_len, 0, 1);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    state s = state::UNFINISHED;
    int attempts = 1000;
    while (s == state::UNFINISHED && attempts-- > 0) {
        s = shared->peek();
    }
    EXPECT_EQ(s, state::FINISH);

    shared->finish();
}

TEST_F(AsyncIOTest, ReadTransferDataConsumesBuffer) {
    async_io io;

    auto weak = io.read(fd, test_data_len, 0, 1);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    shared->wait();

    unique_buf data1 = shared->transfer_data();
    ASSERT_NE(data1.get(), nullptr);

    // Second call returns nullptr since buf was consumed
    unique_buf data2 = shared->transfer_data();
    EXPECT_EQ(data2.get(), nullptr);

    shared->finish();
}

TEST_F(AsyncIOTest, ReadUserDataDefaultValue) {
    async_io io;

    auto weak = io.read(fd, test_data_len, 0, 0);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    shared->wait();
    auto ud = shared->user_data();
    ASSERT_TRUE(ud.has_value());
    EXPECT_EQ(ud.value(), 0u);

    shared->finish();
}

// ============================================================================
// Test Group 4: Single-threaded sequential operations
// ============================================================================

TEST_F(AsyncIOTest, SequentialReadsSameThread) {
    async_io io;

    const int num_reads = 10;
    std::vector<shared_result_read> reads;

    // Step 1: Submit all reads in the same thread
    for (int i = 0; i < num_reads; ++i) {
        auto weak = io.read(fd, test_data_len, 0, 100 + i);
        auto shared = weak.lock();
        ASSERT_TRUE(shared);
        reads.push_back(shared);
    }

    // Step 2: Wait for each in the same thread (order may vary)
    for (int i = 0; i < num_reads; ++i) {
        int res = reads[i]->wait();
        EXPECT_EQ(res, static_cast<int>(test_data_len));

        auto ud = reads[i]->user_data();
        ASSERT_TRUE(ud.has_value());
        EXPECT_EQ(ud.value(), static_cast<uint64_t>(100 + i));

        reads[i]->finish();
    }
}

TEST_F(AsyncIOTest, SequentialWritesSameThread) {
    async_io io;

    const int num_writes = 10;
    std::vector<shared_result_write> writes;
    size_t offset_step = test_data_len + 16;

    // Step 1: Submit all writes sequentially on the same thread
    for (int i = 0; i < num_writes; ++i) {
        uint8_t* buf = new uint8_t[test_data_len + 1];
        memcpy(buf, test_data, test_data_len);
        unique_buf ubuf = make_unique_buf(buf);

        auto weak = io.write(fd, std::move(ubuf), test_data_len, i * offset_step, 200 + i);
        auto shared = weak.lock();
        ASSERT_TRUE(shared);
        writes.push_back(shared);
    }

    // Step 2: Wait for each sequentially on the same thread
    for (int i = 0; i < num_writes; ++i) {
        int res = writes[i]->wait();
        EXPECT_EQ(res, static_cast<int>(test_data_len));

        auto ud = writes[i]->user_data();
        ASSERT_TRUE(ud.has_value());
        EXPECT_EQ(ud.value(), static_cast<uint64_t>(200 + i));

        writes[i]->finish();
    }
}

TEST_F(AsyncIOTest, InterleavedReadWrite) {
    async_io io;

    const char* write_str = "Interleaved test data";
    size_t wlen = strlen(write_str);

    // Write
    uint8_t* buf = new uint8_t[wlen + 1];
    memcpy(buf, write_str, wlen);
    unique_buf ubuf = make_unique_buf(buf);

    auto wweak = io.write(fd, std::move(ubuf), wlen, 0, 1);
    auto wshared = wweak.lock();
    ASSERT_TRUE(wshared);
    wshared->wait();
    wshared->finish();

    // Read back
    auto rweak = io.read(fd, wlen, 0, 2);
    auto rshared = rweak.lock();
    ASSERT_TRUE(rshared);
    int res = rshared->wait();
    EXPECT_EQ(res, static_cast<int>(wlen));

    unique_buf data = rshared->transfer_data();
    ASSERT_NE(data.get(), nullptr);
    EXPECT_EQ(std::memcmp(data.get(), write_str, wlen), 0);

    rshared->finish();
}

TEST_F(AsyncIOTest, WriteReadVerifySingleThread) {
    async_io io;

    // Phase 1: Write data at various offsets
    const int num_blocks = 5;
    std::string base_str = "BlockData_";
    std::vector<std::string> expected_data;
    std::vector<shared_result_write> write_results;

    for (int i = 0; i < num_blocks; ++i) {
        std::string block = base_str + std::to_string(i);
        expected_data.push_back(block);

        uint8_t* buf = new uint8_t[block.size()];
        memcpy(buf, block.c_str(), block.size());
        unique_buf ubuf = make_unique_buf(buf);

        auto weak = io.write(fd, std::move(ubuf), block.size(), i * 256, i);
        auto shared = weak.lock();
        ASSERT_TRUE(shared);
        write_results.push_back(shared);
    }

    // Phase 2: Wait all writes on the same thread
    for (int i = 0; i < num_blocks; ++i) {
        int res = write_results[i]->wait();
        EXPECT_EQ(res, static_cast<int>(expected_data[i].size()));
        write_results[i]->finish();
    }

    // Phase 3: Read back and verify on the same thread
    for (int i = 0; i < num_blocks; ++i) {
        auto weak = io.read(fd, expected_data[i].size(), i * 256, i);
        auto shared = weak.lock();
        ASSERT_TRUE(shared);

        int res = shared->wait();
        EXPECT_EQ(res, static_cast<int>(expected_data[i].size()));

        auto ud = shared->user_data();
        ASSERT_TRUE(ud.has_value());
        EXPECT_EQ(ud.value(), static_cast<uint64_t>(i));

        unique_buf data = shared->transfer_data();
        ASSERT_NE(data.get(), nullptr);
        EXPECT_EQ(std::memcmp(data.get(), expected_data[i].c_str(), expected_data[i].size()), 0);

        shared->finish();
    }
}

// ============================================================================
// Test Group 5: Single-threaded batch submit + batch reap pattern
// ============================================================================

TEST_F(AsyncIOTest, BatchSubmitBatchReapReads) {
    async_io io;

    const int batch_size = 8;
    std::vector<shared_result_read> results;

    // Batch submit: all reads submitted sequentially on the same thread
    for (int i = 0; i < batch_size; ++i) {
        auto weak = io.read(fd, test_data_len, 0, i);
        auto shared = weak.lock();
        ASSERT_TRUE(shared);
        results.push_back(shared);
    }

    // Batch reap: collect all CQEs on the same thread
    for (int i = 0; i < batch_size; ++i) {
        int res = results[i]->wait();
        EXPECT_EQ(res, static_cast<int>(test_data_len));
        results[i]->finish();
    }
}

TEST_F(AsyncIOTest, BatchSubmitBatchReapWrites) {
    async_io io;

    const int batch_size = 8;
    std::vector<shared_result_write> results;

    // Batch submit
    for (int i = 0; i < batch_size; ++i) {
        uint8_t* buf = new uint8_t[test_data_len + 1];
        memcpy(buf, test_data, test_data_len);
        unique_buf ubuf = make_unique_buf(buf);

        auto weak = io.write(fd, std::move(ubuf), test_data_len, i * 128, i);
        auto shared = weak.lock();
        ASSERT_TRUE(shared);
        results.push_back(shared);
    }

    // Batch reap
    for (int i = 0; i < batch_size; ++i) {
        int res = results[i]->wait();
        EXPECT_EQ(res, static_cast<int>(test_data_len));
        results[i]->finish();
    }
}

TEST_F(AsyncIOTest, SubmitThenPeekAllThenWaitAll) {
    async_io io;

    const int batch_size = 6;
    std::vector<shared_result_read> results;

    // Submit all
    for (int i = 0; i < batch_size; ++i) {
        auto weak = io.read(fd, test_data_len, 0, i);
        auto shared = weak.lock();
        ASSERT_TRUE(shared);
        results.push_back(shared);
    }

    // Non-blocking peek: poll until all are finished (single thread)
    int finished_count = 0;
    int attempts = 10000;
    std::set<size_t> finished_indices;
    while (finished_count < batch_size && attempts-- > 0) {
        for (int i = 0; i < batch_size; ++i) {
            if (finished_indices.count(i)) continue;
            state s = results[i]->peek();
            if (s == state::FINISH) {
                finished_indices.insert(i);
                ++finished_count;
            }
        }
    }
    EXPECT_EQ(finished_count, batch_size);

    // Cleanup all
    for (int i = 0; i < batch_size; ++i) {
        results[i]->finish();
    }
}

// ============================================================================
// Test Group 6: RAII and lifecycle (single thread)
// ============================================================================

TEST_F(AsyncIOTest, ResultWriteAutoFinishOnDestruct) {
    async_io io;

    uint8_t* buf = new uint8_t[test_data_len + 1];
    memcpy(buf, test_data, test_data_len);
    unique_buf ubuf = make_unique_buf(buf);

    auto weak = io.write(fd, std::move(ubuf), test_data_len, 0, 1);
    {
        auto shared = weak.lock();
        ASSERT_TRUE(shared);
        shared->wait();
        // shared goes out of scope, destructor calls finish()
    }
    // After finish(), weak_ptr should be expired
    EXPECT_TRUE(weak.expired());
}

TEST_F(AsyncIOTest, ResultReadAutoFinishOnDestruct) {
    async_io io;

    auto weak = io.read(fd, test_data_len, 0, 1);
    {
        auto shared = weak.lock();
        ASSERT_TRUE(shared);
        shared->wait();
        // shared goes out of scope, destructor calls finish()
    }
    // After finish(), weak_ptr should be expired
    EXPECT_TRUE(weak.expired());
}

// ============================================================================
// Test Group 7: Move semantics
// ============================================================================

TEST_F(AsyncIOTest, ResultWriteMoveConstructor) {
    async_io io;

    uint8_t* buf = new uint8_t[test_data_len + 1];
    memcpy(buf, test_data, test_data_len);
    unique_buf ubuf = make_unique_buf(buf);

    auto weak = io.write(fd, std::move(ubuf), test_data_len, 0, 1);
    auto shared1 = weak.lock();
    ASSERT_TRUE(shared1);
    shared1->wait();

    // Move construct
    auto shared2 = std::move(*shared1);
    // After move, the object state is transferred
    EXPECT_FALSE(shared2.empty());
    shared2.finish();
}

TEST_F(AsyncIOTest, ResultWriteMoveAssignment) {
    async_io io;

    uint8_t* buf1 = new uint8_t[test_data_len + 1];
    memcpy(buf1, test_data, test_data_len);
    unique_buf ubuf1 = make_unique_buf(buf1);

    auto weak1 = io.write(fd, std::move(ubuf1), test_data_len, 0, 1);
    auto shared_a = weak1.lock();
    ASSERT_TRUE(shared_a);
    shared_a->wait();

    uint8_t* buf2 = new uint8_t[test_data_len + 1];
    memcpy(buf2, test_data, test_data_len);
    unique_buf ubuf2 = make_unique_buf(buf2);

    auto weak2 = io.write(fd, std::move(ubuf2), test_data_len, 0, 2);
    auto shared_b = weak2.lock();

    *shared_b = std::move(*shared_a);
    // shared_a was moved-from, but finish should be safe
    shared_b->finish();
}

TEST_F(AsyncIOTest, ResultReadMoveConstructor) {
    async_io io;

    auto weak = io.read(fd, test_data_len, 0, 1);
    auto shared1 = weak.lock();
    ASSERT_TRUE(shared1);
    shared1->wait();

    auto shared2 = std::move(*shared1);
    EXPECT_FALSE(shared2.empty());
    shared2.finish();
}

TEST_F(AsyncIOTest, ResultReadMoveAssignment) {
    async_io io;

    auto weak1 = io.read(fd, test_data_len, 0, 1);
    auto shared_a = weak1.lock();
    ASSERT_TRUE(shared_a);
    shared_a->wait();

    auto weak2 = io.read(fd, test_data_len, 0, 2);
    auto shared_b = weak2.lock();

    *shared_b = std::move(*shared_a);
    shared_b->finish();
}

// ============================================================================
// Test Group 8: Edge cases
// ============================================================================

TEST_F(AsyncIOTest, ReadSizeZero) {
    async_io io;

    auto weak = io.read(fd, 0, 0, 1);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    int res = shared->wait();
    EXPECT_EQ(res, 0);
    EXPECT_EQ(shared->size(), 0u);

    shared->finish();
}

TEST_F(AsyncIOTest, WriteEmptyBuffer) {
    async_io io;

    uint8_t* buf = new uint8_t[1];
    buf[0] = 0;
    unique_buf ubuf = make_unique_buf(buf);

    auto weak = io.write(fd, std::move(ubuf), 0, 0, 1);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    int res = shared->wait();
    EXPECT_EQ(res, 0);

    shared->finish();
}

TEST_F(AsyncIOTest, EmptyResultBeforeWait) {
    async_io io;

    auto weak = io.read(fd, test_data_len, 0, 1);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    // Before wait/peek, CQE is not available
    EXPECT_TRUE(shared->empty());

    shared->wait();
    EXPECT_FALSE(shared->empty());

    shared->finish();
}

TEST_F(AsyncIOTest, UserDataBeforeWaitReturnsNullopt) {
    async_io io;

    auto weak = io.read(fd, test_data_len, 0, 42);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    // Before wait/peek, user_data should return nullopt
    auto ud = shared->user_data();
    EXPECT_FALSE(ud.has_value());

    shared->wait();
    ud = shared->user_data();
    EXPECT_TRUE(ud.has_value());
    EXPECT_EQ(ud.value(), 42u);

    shared->finish();
}

TEST_F(AsyncIOTest, DoubleFinishIsSafe) {
    async_io io;

    auto weak = io.read(fd, test_data_len, 0, 1);
    auto shared = weak.lock();
    ASSERT_TRUE(shared);

    shared->wait();
    EXPECT_NO_THROW(shared->finish());
    // Second finish should be safe (ring is null after first finish)
    EXPECT_NO_THROW(shared->finish());
}

TEST_F(AsyncIOTest, LargeReadWrite) {
    async_io io;
    const size_t large_size = 64 * 1024; // 64KB

    // Prepare large buffer
    uint8_t* wbuf = new uint8_t[large_size];
    for (size_t i = 0; i < large_size; ++i) {
        wbuf[i] = static_cast<uint8_t>(i & 0xFF);
    }
    unique_buf ubuf = make_unique_buf(wbuf);

    // Write
    auto wweak = io.write(fd, std::move(ubuf), large_size, 0, 1);
    auto wshared = wweak.lock();
    ASSERT_TRUE(wshared);
    int wres = wshared->wait();
    EXPECT_EQ(wres, static_cast<int>(large_size));
    wshared->finish();

    // Read back
    auto rweak = io.read(fd, large_size, 0, 2);
    auto rshared = rweak.lock();
    ASSERT_TRUE(rshared);
    int rres = rshared->wait();
    EXPECT_EQ(rres, static_cast<int>(large_size));

    unique_buf rdata = rshared->transfer_data();
    ASSERT_NE(rdata.get(), nullptr);

    bool match = true;
    for (size_t i = 0; i < large_size; ++i) {
        if (rdata[i] != static_cast<uint8_t>(i & 0xFF)) {
            match = false;
            break;
        }
    }
    EXPECT_TRUE(match);

    rshared->finish();
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}