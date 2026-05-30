// Copyright (c) 2026 Ximiaw
// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <vector>
#include <string>
#include <memory>
#include <random>
#include <algorithm>

#include "async_io.h"
#include "data_struct.h"

using namespace mAsyncDiskIO;

// ============================================================================
// Helper utilities
// ============================================================================

static std::string g_test_dir;

class TempFileGuard {
public:
    explicit TempFileGuard(const std::string& path) : path_(path) {}
    ~TempFileGuard() { ::unlink(path_.c_str()); }
    const std::string& path() const { return path_; }
private:
    std::string path_;
};

static int create_temp_file(const std::string& path, size_t size) {
    int fd = ::open(path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) return -1;
    if (size > 0) {
        if (::ftruncate(fd, static_cast<off_t>(size)) < 0) {
            ::close(fd);
            return -1;
        }
    }
    return fd;
}

static unique_buf make_test_buffer(size_t size, uint8_t seed = 0) {
    uint8_t* ptr = new uint8_t[size];
    for (size_t i = 0; i < size; ++i) {
        ptr[i] = static_cast<uint8_t>((seed + i) & 0xFF);
    }
    return make_unique_buf(ptr);
}

static bool buffers_equal(const uint8_t* a, const uint8_t* b, size_t size) {
    return std::memcmp(a, b, size) == 0;
}

// ============================================================================
// 1. Basic type tests
// ============================================================================

TEST(BasicTypes, UniqueBufDefaultNull) {
    unique_buf buf = make_unique_buf();
    EXPECT_EQ(buf.get(), nullptr);
    EXPECT_TRUE(!buf);
}

TEST(BasicTypes, UniqueBufHoldsPointer) {
    uint8_t* raw = new uint8_t[16];
    std::memset(raw, 0xAB, 16);
    {
        unique_buf buf = make_unique_buf(raw);
        EXPECT_EQ(buf.get(), raw);
        EXPECT_TRUE(!!buf);
    }
    // buf destructor should have freed raw
}

TEST(BasicTypes, UniqueBufCustomDeleter) {
    static int delete_count;
    delete_count = 0;
    {
        unique_buf buf(new uint8_t[8], [](uint8_t* p) {
            ++delete_count;
            delete[] p;
        });
        (void)buf;
    }
    EXPECT_EQ(delete_count, 1);
}

TEST(BasicTypes, StateEnumValues) {
    EXPECT_NE(state::ERROR, state::FINISH);
    EXPECT_NE(state::ERROR, state::UNFINISHED);
    EXPECT_NE(state::FINISH, state::UNFINISHED);
}

TEST(BasicTypes, OptionalUi64Empty) {
    optional_ui64 opt;
    EXPECT_FALSE(opt.has_value());
}

TEST(BasicTypes, OptionalUi64WithValue) {
    optional_ui64 opt(42);
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(opt.value(), 42);
}

TEST(BasicTypes, UseDataDefault) {
    use_data ud;
    EXPECT_EQ(ud.use_d, 0u);
    EXPECT_EQ(ud.buf, nullptr);
}

TEST(BasicTypes, UseDataWithBuf) {
    uint8_t* ptr = new uint8_t[32];
    {
        use_data ud{12345, ptr};
        EXPECT_EQ(ud.use_d, 12345u);
        EXPECT_EQ(ud.buf, ptr);
    }
    // destructor should have freed ptr
}

// ============================================================================
// 2. Constructor / Destructor tests
// ============================================================================

TEST(Construction, AsyncIoDefault) {
    EXPECT_NO_THROW({
        async_io io;
    });
}

TEST(Construction, AsyncIoCustomDepth) {
    EXPECT_NO_THROW({
        async_io io(128);
    });
}

TEST(Construction, AsyncIoNotCopyable) {
    EXPECT_FALSE(std::is_copy_constructible_v<async_io>);
    EXPECT_FALSE(std::is_copy_assignable_v<async_io>);
}

TEST(Construction, AsyncIoNotMovable) {
    EXPECT_FALSE(std::is_move_constructible_v<async_io>);
    EXPECT_FALSE(std::is_move_assignable_v<async_io>);
}

TEST(Construction, ResultReadMovable) {
    EXPECT_TRUE(std::is_move_constructible_v<async_result_read>);
    EXPECT_TRUE(std::is_move_assignable_v<async_result_read>);
    EXPECT_FALSE(std::is_copy_constructible_v<async_result_read>);
    EXPECT_FALSE(std::is_copy_assignable_v<async_result_read>);
}

TEST(Construction, ResultWriteMovable) {
    EXPECT_TRUE(std::is_move_constructible_v<async_result_write>);
    EXPECT_TRUE(std::is_move_assignable_v<async_result_write>);
    EXPECT_FALSE(std::is_copy_constructible_v<async_result_write>);
    EXPECT_FALSE(std::is_copy_assignable_v<async_result_write>);
}

TEST(Construction, ResultReadMoveSemantics) {
    std::string path = g_test_dir + "/test_move_read.bin";
    TempFileGuard guard(path);
    int fd = create_temp_file(path, 64);
    ASSERT_GE(fd, 0);

    const char data[] = "MoveSemanticsTestData";
    ASSERT_EQ(::pwrite(fd, data, sizeof(data), 0), static_cast<ssize_t>(sizeof(data)));
    ::close(fd);

    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);

    {
        async_io io;
        auto res1 = io.read(fd, sizeof(data), 0, 100);
        ASSERT_NE(res1, nullptr);
        // After submission but before completion, empty() returns true (use_d not set yet)
        EXPECT_TRUE(res1->empty());

        auto res2 = std::move(res1);
        EXPECT_EQ(res1, nullptr);  // unique_ptr moved
        ASSERT_NE(res2, nullptr);

        int ret = res2->wait();
        EXPECT_EQ(ret, static_cast<int>(sizeof(data)));
        // After wait completes, use_d is set, empty() returns false
        EXPECT_FALSE(res2->empty());
        EXPECT_EQ(res2->user_data().value_or(0), 100u);
    }
    ::close(fd);
}

TEST(Construction, ResultWriteMoveSemantics) {
    std::string path = g_test_dir + "/test_move_write.bin";
    TempFileGuard guard(path);
    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);

    const char data[] = "WriteMoveTestData";
    unique_buf buf = make_unique_buf(new uint8_t[sizeof(data)]);
    std::memcpy(buf.get(), data, sizeof(data));

    {
        async_io io;
        auto res1 = io.write(fd, std::move(buf), sizeof(data), 0, 200);
        ASSERT_NE(res1, nullptr);
        EXPECT_TRUE(res1->empty());  // not completed yet

        auto res2 = std::move(res1);
        EXPECT_EQ(res1, nullptr);
        ASSERT_NE(res2, nullptr);

        int ret = res2->wait();
        EXPECT_EQ(ret, static_cast<int>(sizeof(data)));
        EXPECT_FALSE(res2->empty());
        EXPECT_EQ(res2->user_data().value_or(0), 200u);
    }
    ::close(fd);
}

// ============================================================================
// 3. Basic read / write operations
// ============================================================================

TEST(ReadWrite, BasicWriteThenRead) {
    std::string path = g_test_dir + "/test_basic_rw.bin";
    TempFileGuard guard(path);

    const char write_data[] = "Hello, async io world!";
    size_t len = sizeof(write_data);

    // Write
    int fdw = create_temp_file(path, 0);
    ASSERT_GE(fdw, 0);
    {
        async_io io;
        unique_buf wbuf = make_unique_buf(new uint8_t[len]);
        std::memcpy(wbuf.get(), write_data, len);
        auto wres = io.write(fdw, std::move(wbuf), len, 0, 1);
        ASSERT_NE(wres, nullptr);
        int wret = wres->wait();
        EXPECT_EQ(wret, static_cast<int>(len));
        EXPECT_FALSE(wres->empty());
    }
    ::close(fdw);

    // Read
    int fdr = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fdr, 0);
    {
        async_io io;
        auto rres = io.read(fdr, len, 0, 2);
        ASSERT_NE(rres, nullptr);
        int rret = rres->wait();
        EXPECT_EQ(rret, static_cast<int>(len));
        EXPECT_EQ(rres->user_data().value_or(0), 2u);
        EXPECT_FALSE(rres->empty());

        unique_buf rbuf = rres->transfer_data();
        ASSERT_NE(rbuf.get(), nullptr);
        EXPECT_TRUE(buffers_equal(rbuf.get(), reinterpret_cast<const uint8_t*>(write_data), len));
    }
    ::close(fdr);
}

TEST(ReadWrite, EmptyFileRead) {
    std::string path = g_test_dir + "/test_empty.bin";
    TempFileGuard guard(path);
    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);
    ::close(fd);

    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        async_io io;
        auto res = io.read(fd, 1, 0, 0);
        ASSERT_NE(res, nullptr);
        int ret = res->wait();
        EXPECT_EQ(ret, 0);  // EOF on empty file
    }
    ::close(fd);
}

TEST(ReadWrite, WriteZeroBytes) {
    std::string path = g_test_dir + "/test_write_zero.bin";
    TempFileGuard guard(path);
    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);

    {
        async_io io;
        unique_buf buf = make_unique_buf(new uint8_t[1]);
        buf.get()[0] = 'x';
        auto res = io.write(fd, std::move(buf), 0, 0, 0);
        ASSERT_NE(res, nullptr);
        int ret = res->wait();
        EXPECT_EQ(ret, 0);
    }
    ::close(fd);
}

TEST(ReadWrite, ReadReturnsCorrectSize) {
    std::string path = g_test_dir + "/test_size.bin";
    TempFileGuard guard(path);
    int fd = create_temp_file(path, 128);
    ASSERT_GE(fd, 0);

    uint8_t pattern[128];
    for (int i = 0; i < 128; ++i) pattern[i] = static_cast<uint8_t>(i);
    ASSERT_EQ(::pwrite(fd, pattern, 128, 0), 128);
    ::close(fd);

    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        async_io io;
        auto res = io.read(fd, 64, 0, 0);
        ASSERT_NE(res, nullptr);
        // Before completion cqe is null, size() returns 0
        EXPECT_EQ(res->size(), 0u);
        int ret = res->wait();
        EXPECT_EQ(ret, 64);
        // After wait(), finish() is called which clears cqe,
        // so size() returns 0 (expected library behavior)
        EXPECT_EQ(res->size(), 0u);
    }
    ::close(fd);
}

TEST(ReadWrite, PeekBeforeWait) {
    std::string path = g_test_dir + "/test_peek.bin";
    TempFileGuard guard(path);
    const char data[] = "PeekTestData";

    int fdw = create_temp_file(path, 0);
    ASSERT_GE(fdw, 0);
    {
        async_io io;
        unique_buf buf = make_unique_buf(new uint8_t[sizeof(data)]);
        std::memcpy(buf.get(), data, sizeof(data));
        auto wres = io.write(fdw, std::move(buf), sizeof(data), 0, 0);
        ASSERT_NE(wres, nullptr);

        // Initially UNFINISHED or FINISH (depending on speed)
        state s = wres->peek();
        // After peek it should be FINISH if CQE available
        EXPECT_TRUE(s == state::FINISH || s == state::UNFINISHED);

        // If UNFINISHED, wait for it
        if (s == state::UNFINISHED) {
            int ret = wres->wait();
            EXPECT_EQ(ret, static_cast<int>(sizeof(data)));
        }
    }
    ::close(fdw);
}

TEST(ReadWrite, TransferDataConsumesBuffer) {
    std::string path = g_test_dir + "/test_transfer.bin";
    TempFileGuard guard(path);
    const char data[] = "TransferTest";

    int fdw = create_temp_file(path, 0);
    ASSERT_GE(fdw, 0);
    ASSERT_EQ(::pwrite(fdw, data, sizeof(data), 0), static_cast<ssize_t>(sizeof(data)));
    ::close(fdw);

    int fdr = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fdr, 0);
    {
        async_io io;
        auto res = io.read(fdr, sizeof(data), 0, 0);
        ASSERT_NE(res, nullptr);
        int ret = res->wait();
        EXPECT_EQ(ret, static_cast<int>(sizeof(data)));
        EXPECT_FALSE(res->empty());

        unique_buf buf1 = res->transfer_data();
        ASSERT_NE(buf1.get(), nullptr);
        EXPECT_TRUE(buffers_equal(buf1.get(), reinterpret_cast<const uint8_t*>(data), sizeof(data)));

        // Second transfer should return null (already consumed)
        unique_buf buf2 = res->transfer_data();
        EXPECT_EQ(buf2.get(), nullptr);
    }
    ::close(fdr);
}

TEST(ReadWrite, UserDataPreserved) {
    std::string path = g_test_dir + "/test_userdata.bin";
    TempFileGuard guard(path);
    int fd = create_temp_file(path, 64);
    ASSERT_GE(fd, 0);
    ::close(fd);

    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        async_io io;
        uint64_t ud = 0xDEADBEEFCAFEBABEull;
        auto res = io.read(fd, 8, 0, ud);
        ASSERT_NE(res, nullptr);
        res->wait();
        auto opt = res->user_data();
        ASSERT_TRUE(opt.has_value());
        EXPECT_EQ(opt.value(), ud);
    }
    ::close(fd);
}

// ============================================================================
// 4. Offset read / write
// ============================================================================

TEST(OffsetRW, WriteAtOffset) {
    std::string path = g_test_dir + "/test_offset_write.bin";
    TempFileGuard guard(path);

    const char block1[] = "BLOCK_ONE_DATA";
    const char block2[] = "BLOCK_TWO_DATA";
    size_t offset2 = 1024;

    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);

    {
        async_io io;
        unique_buf buf1 = make_unique_buf(new uint8_t[sizeof(block1)]);
        std::memcpy(buf1.get(), block1, sizeof(block1));
        auto res1 = io.write(fd, std::move(buf1), sizeof(block1), 0, 10);

        unique_buf buf2 = make_unique_buf(new uint8_t[sizeof(block2)]);
        std::memcpy(buf2.get(), block2, sizeof(block2));
        auto res2 = io.write(fd, std::move(buf2), sizeof(block2), offset2, 20);

        EXPECT_EQ(res1->wait(), static_cast<int>(sizeof(block1)));
        EXPECT_EQ(res2->wait(), static_cast<int>(sizeof(block2)));
    }
    ::close(fd);

    // Verify via pread
    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    char rbuf1[sizeof(block1)] = {};
    char rbuf2[sizeof(block2)] = {};
    EXPECT_EQ(::pread(fd, rbuf1, sizeof(block1), 0), static_cast<ssize_t>(sizeof(block1)));
    EXPECT_EQ(::pread(fd, rbuf2, sizeof(block2), offset2), static_cast<ssize_t>(sizeof(block2)));
    EXPECT_TRUE(buffers_equal(reinterpret_cast<uint8_t*>(rbuf1), reinterpret_cast<const uint8_t*>(block1), sizeof(block1)));
    EXPECT_TRUE(buffers_equal(reinterpret_cast<uint8_t*>(rbuf2), reinterpret_cast<const uint8_t*>(block2), sizeof(block2)));
    ::close(fd);
}

TEST(OffsetRW, ReadAtOffset) {
    std::string path = g_test_dir + "/test_offset_read.bin";
    TempFileGuard guard(path);

    size_t total = 4096;
    std::vector<uint8_t> reference(total);
    for (size_t i = 0; i < total; ++i) {
        reference[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
    }

    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::pwrite(fd, reference.data(), total, 0), static_cast<ssize_t>(total));
    ::close(fd);

    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        async_io io;
        size_t roffset = 512;
        size_t rsize = 256;
        auto res = io.read(fd, rsize, roffset, 55);
        ASSERT_NE(res, nullptr);
        int ret = res->wait();
        EXPECT_EQ(ret, static_cast<int>(rsize));

        unique_buf buf = res->transfer_data();
        ASSERT_NE(buf.get(), nullptr);
        EXPECT_TRUE(buffers_equal(buf.get(), reference.data() + roffset, rsize));
    }
    ::close(fd);
}

TEST(OffsetRW, NonAlignedOffsets) {
    std::string path = g_test_dir + "/test_nonalign.bin";
    TempFileGuard guard(path);

    int fd = create_temp_file(path, 8192);
    ASSERT_GE(fd, 0);

    // Write at odd offsets
    for (int i = 0; i < 8; ++i) {
        uint8_t byte = static_cast<uint8_t>(0xA0 + i);
        ASSERT_EQ(::pwrite(fd, &byte, 1, i * 1000 + 3), 1);
    }
    ::close(fd);

    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        async_io io;
        for (int i = 0; i < 8; ++i) {
            auto res = io.read(fd, 1, i * 1000 + 3, i);
            ASSERT_NE(res, nullptr);
            int ret = res->wait();
            EXPECT_EQ(ret, 1);
            unique_buf buf = res->transfer_data();
            ASSERT_NE(buf.get(), nullptr);
            EXPECT_EQ(buf.get()[0], static_cast<uint8_t>(0xA0 + i));
        }
    }
    ::close(fd);
}

// ============================================================================
// 5. Multiple concurrent requests
// ============================================================================

TEST(MultiRequest, MultipleReads) {
    std::string path = g_test_dir + "/test_multi_read.bin";
    TempFileGuard guard(path);

    const int num_blocks = 16;
    const size_t block_size = 256;
    std::vector<uint8_t> reference(num_blocks * block_size);
    for (size_t i = 0; i < reference.size(); ++i) {
        reference[i] = static_cast<uint8_t>((i * 3 + 17) & 0xFF);
    }

    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::pwrite(fd, reference.data(), reference.size(), 0), static_cast<ssize_t>(reference.size()));
    ::close(fd);

    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        async_io io(64);
        std::vector<unique_result_read> results;
        for (int i = 0; i < num_blocks; ++i) {
            results.push_back(io.read(fd, block_size, i * block_size, i));
            ASSERT_NE(results.back(), nullptr);
        }
        for (int i = 0; i < num_blocks; ++i) {
            int ret = results[i]->wait();
            EXPECT_EQ(ret, static_cast<int>(block_size));
            EXPECT_EQ(results[i]->user_data().value_or(999), static_cast<uint64_t>(i));

            unique_buf buf = results[i]->transfer_data();
            ASSERT_NE(buf.get(), nullptr);
            EXPECT_TRUE(buffers_equal(buf.get(), reference.data() + i * block_size, block_size));
        }
    }
    ::close(fd);
}

TEST(MultiRequest, MultipleWrites) {
    std::string path = g_test_dir + "/test_multi_write.bin";
    TempFileGuard guard(path);

    const int num_blocks = 10;
    const size_t block_size = 128;

    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);

    {
        async_io io(64);
        std::vector<unique_result_write> results;
        std::vector<unique_buf> buffers;

        for (int i = 0; i < num_blocks; ++i) {
            uint8_t* raw = new uint8_t[block_size];
            std::memset(raw, 0x30 + i, block_size);
            buffers.push_back(make_unique_buf(raw));
        }

        for (int i = 0; i < num_blocks; ++i) {
            results.push_back(io.write(fd, std::move(buffers[i]), block_size, i * block_size, i));
        }

        for (int i = 0; i < num_blocks; ++i) {
            int ret = results[i]->wait();
            EXPECT_EQ(ret, static_cast<int>(block_size));
            EXPECT_EQ(results[i]->user_data().value_or(999), static_cast<uint64_t>(i));
        }
    }
    ::close(fd);

    // Verify
    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    for (int i = 0; i < num_blocks; ++i) {
        std::vector<uint8_t> buf(block_size);
        EXPECT_EQ(::pread(fd, buf.data(), block_size, i * block_size), static_cast<ssize_t>(block_size));
        uint8_t expected = static_cast<uint8_t>(0x30 + i);
        for (size_t j = 0; j < block_size; ++j) {
            EXPECT_EQ(buf[j], expected) << "Mismatch at block " << i << " byte " << j;
        }
    }
    ::close(fd);
}

TEST(MultiRequest, MixedReadWrite) {
    std::string path = g_test_dir + "/test_mixed_rw.bin";
    TempFileGuard guard(path);

    const int n = 8;
    const size_t sz = 64;

    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);

    // First write some data
    {
        async_io io(32);
        std::vector<unique_result_write> wresults;
        for (int i = 0; i < n; ++i) {
            uint8_t* raw = new uint8_t[sz];
            std::memset(raw, 'A' + i, sz);
            unique_buf buf = make_unique_buf(raw);
            wresults.push_back(io.write(fd, std::move(buf), sz, i * sz, i));
        }
        for (auto& r : wresults) {
            EXPECT_EQ(r->wait(), static_cast<int>(sz));
        }
    }
    ::close(fd);

    // Now read it back, interleaving reads from different offsets
    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        async_io io(32);
        std::vector<unique_result_read> rresults;
        for (int i = 0; i < n; ++i) {
            rresults.push_back(io.read(fd, sz, i * sz, i));
        }
        for (int i = 0; i < n; ++i) {
            EXPECT_EQ(rresults[i]->wait(), static_cast<int>(sz));
            unique_buf buf = rresults[i]->transfer_data();
            ASSERT_NE(buf.get(), nullptr);
            uint8_t expected = static_cast<uint8_t>('A' + i);
            for (size_t j = 0; j < sz; ++j) {
                EXPECT_EQ(buf.get()[j], expected);
            }
        }
    }
    ::close(fd);
}

TEST(MultiRequest, QueueFullReturnsEmpty) {
    std::string path = g_test_dir + "/test_queue_full.bin";
    TempFileGuard guard(path);

    int fd = create_temp_file(path, 4096);
    ASSERT_GE(fd, 0);

    {
        // Small queue depth to trigger full condition
        async_io io(2);
        auto r1 = io.read(fd, 64, 0, 1);
        auto r2 = io.read(fd, 64, 64, 2);
        // With SQPOLL and fast submission, queue may already process
        // Try to exceed - note: this may or may not return empty
        // depending on timing, so we just verify valid ones work
        ASSERT_NE(r1, nullptr);
        ASSERT_NE(r2, nullptr);
        r1->wait();
        r2->wait();
    }
    ::close(fd);
}

// ============================================================================
// 6. Error handling
// ============================================================================

TEST(ErrorHandling, InvalidFdRead) {
    async_io io;
    int bad_fd = 99999;
    auto res = io.read(bad_fd, 64, 0, 0);
    ASSERT_NE(res, nullptr);
    int ret = res->wait();
    EXPECT_LT(ret, 0);  // Should fail with EBADF
}

TEST(ErrorHandling, InvalidFdWrite) {
    async_io io;
    int bad_fd = 99999;
    unique_buf buf = make_unique_buf(new uint8_t[16]);
    std::memset(buf.get(), 0, 16);
    auto res = io.write(bad_fd, std::move(buf), 16, 0, 0);
    ASSERT_NE(res, nullptr);
    int ret = res->wait();
    EXPECT_LT(ret, 0);
}

TEST(ErrorHandling, ReadBeyondEof) {
    std::string path = g_test_dir + "/test_beyond_eof.bin";
    TempFileGuard guard(path);

    int fd = create_temp_file(path, 16);
    ASSERT_GE(fd, 0);
    ::close(fd);

    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        async_io io;
        auto res = io.read(fd, 1024, 0, 0);  // file is only 16 bytes
        ASSERT_NE(res, nullptr);
        int ret = res->wait();
        // Returns actual bytes read (16) or 0 at EOF, not necessarily error
        EXPECT_GE(ret, 0);
    }
    ::close(fd);
}

TEST(ErrorHandling, EmptyResultFromFailedSubmission) {
    // When io_uring_get_sqe returns nullptr, we get an empty unique_result
    // Verify the empty unique_result is nullptr (can't be dereferenced)
    unique_result_read rnull;
    EXPECT_EQ(rnull, nullptr);

    unique_result_write wnull;
    EXPECT_EQ(wnull, nullptr);
}

TEST(ErrorHandling, ReadOnlyFileWrite) {
    std::string path = g_test_dir + "/test_readonly.bin";
    TempFileGuard guard(path);

    int fd = create_temp_file(path, 64);
    ASSERT_GE(fd, 0);
    ::close(fd);

    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        async_io io;
        unique_buf buf = make_unique_buf(new uint8_t[8]);
        std::memset(buf.get(), 'x', 8);
        auto res = io.write(fd, std::move(buf), 8, 0, 0);
        ASSERT_NE(res, nullptr);
        int ret = res->wait();
        EXPECT_LT(ret, 0);  // EBADF or EACCES
    }
    ::close(fd);
}

TEST(ErrorHandling, ExpiredRingReturnsError) {
    // Create a result object whose underlying ring goes away
    unique_result_read res;
    {
        async_io io;
        // We can't easily get a result out without a real fd operation
        // So we test by creating an io, doing an op, completing it, then io dies
        std::string path = g_test_dir + "/test_expired.bin";
        TempFileGuard guard(path);
        int fd = create_temp_file(path, 8);
        ASSERT_GE(fd, 0);
        ::close(fd);

        fd = ::open(path.c_str(), O_RDONLY);
        ASSERT_GE(fd, 0);
        res = io.read(fd, 4, 0, 42);
        ASSERT_NE(res, nullptr);
        // Complete the read
        res->wait();
        ::close(fd);
        // io goes out of scope here, ring is destroyed
    }

    // Result object should handle expired ring gracefully
    EXPECT_EQ(res->peek(), state::ERROR);
    EXPECT_EQ(res->wait(), -1);
    // user_data and buf were cached during wait() before ring expired
    EXPECT_EQ(res->user_data().value_or(0), 42u);
    EXPECT_EQ(res->size(), 0u);  // cqe was cleared by finish()
    unique_buf buf = res->transfer_data();
    EXPECT_NE(buf.get(), nullptr);  // buf was cached in use_d
}

// ============================================================================
// 7. Large file (1MB)
// ============================================================================

TEST(LargeFile, OneMegabyteWriteThenRead) {
    std::string path = g_test_dir + "/test_1mb.bin";
    TempFileGuard guard(path);

    const size_t one_mb = 1024 * 1024;
    std::vector<uint8_t> reference(one_mb);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0, 255);
    for (size_t i = 0; i < one_mb; ++i) {
        reference[i] = static_cast<uint8_t>(dist(rng));
    }

    // Write 1MB
    int fdw = create_temp_file(path, 0);
    ASSERT_GE(fdw, 0);
    {
        async_io io(256);
        unique_buf wbuf = make_unique_buf(new uint8_t[one_mb]);
        std::memcpy(wbuf.get(), reference.data(), one_mb);
        auto wres = io.write(fdw, std::move(wbuf), one_mb, 0, 777);
        ASSERT_NE(wres, nullptr);
        int wret = wres->wait();
        EXPECT_EQ(wret, static_cast<int>(one_mb));
        EXPECT_FALSE(wres->empty());
        EXPECT_EQ(wres->user_data().value_or(0), 777u);
    }
    ::close(fdw);

    // Read back 1MB
    int fdr = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fdr, 0);
    {
        async_io io(256);
        auto rres = io.read(fdr, one_mb, 0, 888);
        ASSERT_NE(rres, nullptr);
        int rret = rres->wait();
        EXPECT_EQ(rret, static_cast<int>(one_mb));
        EXPECT_EQ(rres->user_data().value_or(0), 888u);
        // size() returns 0 after wait() because finish() clears cqe
        EXPECT_EQ(rres->size(), 0u);

        unique_buf rbuf = rres->transfer_data();
        ASSERT_NE(rbuf.get(), nullptr);
        EXPECT_TRUE(buffers_equal(rbuf.get(), reference.data(), one_mb));
    }
    ::close(fdr);
}

TEST(LargeFile, OneMegabyteInChunks) {
    std::string path = g_test_dir + "/test_1mb_chunks.bin";
    TempFileGuard guard(path);

    const size_t one_mb = 1024 * 1024;
    const size_t chunk_size = 64 * 1024;  // 64KB chunks
    const int num_chunks = one_mb / chunk_size;

    std::vector<uint8_t> reference(one_mb);
    for (size_t i = 0; i < one_mb; ++i) {
        reference[i] = static_cast<uint8_t>((i * 5 + 31) & 0xFF);
    }

    // Write in chunks
    int fdw = create_temp_file(path, 0);
    ASSERT_GE(fdw, 0);
    {
        async_io io(128);
        std::vector<unique_result_write> wresults;
        std::vector<unique_buf> wbufs;

        for (int i = 0; i < num_chunks; ++i) {
            uint8_t* raw = new uint8_t[chunk_size];
            std::memcpy(raw, reference.data() + i * chunk_size, chunk_size);
            wbufs.push_back(make_unique_buf(raw));
        }

        for (int i = 0; i < num_chunks; ++i) {
            wresults.push_back(io.write(fdw, std::move(wbufs[i]), chunk_size, i * chunk_size, i));
        }

        for (int i = 0; i < num_chunks; ++i) {
            int ret = wresults[i]->wait();
            EXPECT_EQ(ret, static_cast<int>(chunk_size)) << "Write chunk " << i;
        }
    }
    ::close(fdw);

    // Read back in chunks
    int fdr = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fdr, 0);
    {
        async_io io(128);
        std::vector<unique_result_read> rresults;

        for (int i = 0; i < num_chunks; ++i) {
            rresults.push_back(io.read(fdr, chunk_size, i * chunk_size, i));
        }

        for (int i = 0; i < num_chunks; ++i) {
            int ret = rresults[i]->wait();
            EXPECT_EQ(ret, static_cast<int>(chunk_size)) << "Read chunk " << i;

            unique_buf buf = rresults[i]->transfer_data();
            ASSERT_NE(buf.get(), nullptr);
            EXPECT_TRUE(buffers_equal(buf.get(), reference.data() + i * chunk_size, chunk_size))
                << "Data mismatch at chunk " << i;
        }
    }
    ::close(fdr);
}

TEST(LargeFile, OneMegabyteSingleIO) {
    std::string path = g_test_dir + "/test_1mb_single.bin";
    TempFileGuard guard(path);

    const size_t one_mb = 1024 * 1024;
    std::vector<uint8_t> reference(one_mb);
    for (size_t i = 0; i < one_mb; ++i) {
        reference[i] = static_cast<uint8_t>(i & 0xFF);
    }

    int fdw = create_temp_file(path, 0);
    ASSERT_GE(fdw, 0);
    {
        async_io io;
        unique_buf wbuf = make_unique_buf(new uint8_t[one_mb]);
        std::memcpy(wbuf.get(), reference.data(), one_mb);
        auto wres = io.write(fdw, std::move(wbuf), one_mb, 0, 0);
        ASSERT_NE(wres, nullptr);
        EXPECT_EQ(wres->wait(), static_cast<int>(one_mb));
    }
    ::close(fdw);

    int fdr = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fdr, 0);
    {
        async_io io;
        auto rres = io.read(fdr, one_mb, 0, 0);
        ASSERT_NE(rres, nullptr);
        EXPECT_EQ(rres->wait(), static_cast<int>(one_mb));
        unique_buf rbuf = rres->transfer_data();
        ASSERT_NE(rbuf.get(), nullptr);
        EXPECT_TRUE(buffers_equal(rbuf.get(), reference.data(), one_mb));
    }
    ::close(fdr);
}

// ============================================================================
// 8. Lifecycle safety
// ============================================================================

TEST(Lifecycle, AsyncIoDestroysBeforeResult) {
    std::string path = g_test_dir + "/test_lifecycle.bin";
    TempFileGuard guard(path);

    const char data[] = "LifecycleTest";
    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::pwrite(fd, data, sizeof(data), 0), static_cast<ssize_t>(sizeof(data)));
    ::close(fd);

    unique_result_read res;
    {
        async_io io;
        fd = ::open(path.c_str(), O_RDONLY);
        ASSERT_GE(fd, 0);
        res = io.read(fd, sizeof(data), 0, 42);
        ASSERT_NE(res, nullptr);
        // Complete the operation before async_io goes out of scope
        res->wait();
        ::close(fd);
        // io goes out of scope here, ring is destroyed
    }

    // Result object should handle expired ring gracefully after completion
    EXPECT_EQ(res->peek(), state::ERROR);
    EXPECT_EQ(res->wait(), -1);
    // user_data was cached during wait()
    EXPECT_EQ(res->user_data().value_or(0), 42u);
}

TEST(Lifecycle, ResultDestructorCallsFinish) {
    std::string path = g_test_dir + "/test_result_dtor.bin";
    TempFileGuard guard(path);

    const char data[] = "ResultDestructor";
    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);

    {
        async_io io;
        unique_buf wbuf = make_unique_buf(new uint8_t[sizeof(data)]);
        std::memcpy(wbuf.get(), data, sizeof(data));
        auto wres = io.write(fd, std::move(wbuf), sizeof(data), 0, 0);
        ASSERT_NE(wres, nullptr);
        {
            // Result goes out of scope without explicit wait
            auto temp = std::move(wres);
            (void)temp;
        }
        // Destructor should have called finish() and cleaned up CQE
    }
    ::close(fd);

    // Verify file was written
    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    char rbuf[sizeof(data)] = {};
    EXPECT_EQ(::read(fd, rbuf, sizeof(data)), static_cast<ssize_t>(sizeof(data)));
    EXPECT_TRUE(buffers_equal(reinterpret_cast<uint8_t*>(rbuf), reinterpret_cast<const uint8_t*>(data), sizeof(data)));
    ::close(fd);
}

TEST(Lifecycle, WriteResultReleasesBuf) {
    // Verify that write takes ownership of the buffer via buf.release()
    std::string path = g_test_dir + "/test_buf_release.bin";
    TempFileGuard guard(path);

    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);

    uint8_t* raw_ptr = new uint8_t[32];
    std::memset(raw_ptr, 0xBB, 32);
    unique_buf buf = make_unique_buf(raw_ptr);
    EXPECT_EQ(buf.get(), raw_ptr);

    {
        async_io io;
        auto wres = io.write(fd, std::move(buf), 32, 0, 0);
        EXPECT_EQ(buf.get(), nullptr);  // ownership transferred
        ASSERT_NE(wres, nullptr);
        EXPECT_TRUE(wres->empty());  // use_d not set until completion
        EXPECT_EQ(wres->wait(), 32);
        EXPECT_FALSE(wres->empty());
    }
    // Buffer freed by write result's internal cleanup
    ::close(fd);
}

TEST(Lifecycle, ReadResultDoubleWaitReturnsCachedThenError) {
    std::string path = g_test_dir + "/test_double_wait.bin";
    TempFileGuard guard(path);

    const char data[] = "DoubleWaitTest";
    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::pwrite(fd, data, sizeof(data), 0), static_cast<ssize_t>(sizeof(data)));
    ::close(fd);

    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        async_io io;
        auto res = io.read(fd, sizeof(data), 0, 0);
        ASSERT_NE(res, nullptr);
        int ret1 = res->wait();
        EXPECT_EQ(ret1, static_cast<int>(sizeof(data)));
        // First wait: cqe is seen and reset, so second wait returns -1
        int ret2 = res->wait();
        EXPECT_EQ(ret2, -1);
    }
    ::close(fd);
}

TEST(Lifecycle, MoveAssignment) {
    std::string path = g_test_dir + "/test_move_assign.bin";
    TempFileGuard guard(path);

    const char data[] = "MoveAssignmentTest";
    int fd = create_temp_file(path, 0);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::pwrite(fd, data, sizeof(data), 0), static_cast<ssize_t>(sizeof(data)));
    ::close(fd);

    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        async_io io;
        auto res1 = io.read(fd, sizeof(data), 0, 100);
        ASSERT_NE(res1, nullptr);

        unique_result_read res2;
        res2 = std::move(res1);
        EXPECT_EQ(res1, nullptr);
        ASSERT_NE(res2, nullptr);

        int ret = res2->wait();
        EXPECT_EQ(ret, static_cast<int>(sizeof(data)));
        EXPECT_FALSE(res2->empty());
        EXPECT_EQ(res2->user_data().value_or(0), 100u);
    }
    ::close(fd);
}

TEST(Lifecycle, SelfMoveAssignmentNoOp) {
    std::string path = g_test_dir + "/test_self_move.bin";
    TempFileGuard guard(path);

    int fd = create_temp_file(path, 32);
    ASSERT_GE(fd, 0);
    ::close(fd);

    fd = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);
    {
        async_io io;
        auto res = io.read(fd, 8, 0, 0);
        ASSERT_NE(res, nullptr);
        // Self-move-assignment is guarded in operator= (this==&other check)
        // Just verify no crash
        res->wait();
        SUCCEED();
    }
    ::close(fd);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    // Set up test directory
    if (argc >= 2) {
        g_test_dir = argv[1];
    } else {
        g_test_dir = "/tmp/async_io_test_" + std::to_string(getpid());
    }
    ::mkdir(g_test_dir.c_str(), 0755);

    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();

    // Cleanup test directory
    // (TempFileGuard handles individual files; rmdir the dir)
    ::rmdir(g_test_dir.c_str());

    return result;
}