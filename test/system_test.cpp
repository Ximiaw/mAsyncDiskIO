#include <gtest/gtest.h>
#include "async_io.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <set>
#include <thread>

mAsyncDiskIO::unique_buf allocate_aligned_buffer(size_t size) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 4096, size) != 0) {
        throw std::bad_alloc();
    }
    memset(ptr, 0, size);
    return mAsyncDiskIO::make_unique_buf(
        static_cast<uint8_t*>(ptr),
        [](uint8_t* p) { free(p); }
    );
}

void fill_pattern(uint8_t* buf, size_t size, uint8_t seed) {
    for (size_t i = 0; i < size; ++i) {
        buf[i] = static_cast<uint8_t>((seed + i) & 0xFF);
    }
}

bool verify_pattern(const uint8_t* buf, size_t size, uint8_t seed) {
    for (size_t i = 0; i < size; ++i) {
        if (buf[i] != static_cast<uint8_t>((seed + i) & 0xFF)) {
            return false;
        }
    }
    return true;
}

namespace {
    void misaligned_deleter(uint8_t* p) {
        free(p - 1);
    }
}

class SystemFunctionalTest : public ::testing::Test {
protected:
    int fd = -1;
    std::string test_file;
    const size_t block_size = 4096;
    bool has_odirect = false;

    void SetUp() override {
        // 唯一文件名：消除并行测试竞争
        auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        test_file = "/tmp/mAsyncDiskIO_sys_" +
                    std::string(info->test_suite_name()) + "_" +
                    std::string(info->name()) + ".dat";

        fd = open(test_file.c_str(), O_RDWR | O_CREAT | O_DIRECT | O_TRUNC, 0644);
        if (fd >= 0) {
            has_odirect = true;
        } else {
            fd = open(test_file.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
            has_odirect = false;
        }
        ASSERT_GE(fd, 0) << "Failed to open test file: " << test_file;
    }

    void TearDown() override {
        if (fd >= 0) {
            close(fd);
            unlink(test_file.c_str());
        }
    }
};

// ==================== 1. 基础数据完整性 ====================
TEST_F(SystemFunctionalTest, SingleWriteReadConsistency) {
    mAsyncDiskIO::async_io io(32);
    ASSERT_EQ(posix_fallocate(fd, 0, block_size), 0);

    auto write_buf = allocate_aligned_buffer(block_size);
    fill_pattern(write_buf.get(), block_size, 0xAA);

    ASSERT_TRUE(io.prep_write(fd, std::move(write_buf), block_size, 1001, 0));
    ASSERT_TRUE(io.submit());

    ASSERT_GT(io.result_count(), 0u);
    auto write_res = io.get_result();
    ASSERT_TRUE(write_res != nullptr);
    int write_bytes = write_res->wait();
    ASSERT_EQ(write_bytes, (int)block_size);
    write_res->transfer_data();

    fsync(fd);

    auto read_buf = allocate_aligned_buffer(block_size);
    ASSERT_TRUE(io.prep_read(fd, std::move(read_buf), block_size, 1002, 0));
    ASSERT_TRUE(io.submit());

    ASSERT_GT(io.result_count(), 0u);
    auto read_res = io.get_result();
    ASSERT_TRUE(read_res != nullptr);
    int read_bytes = read_res->wait();
    ASSERT_EQ(read_bytes, (int)block_size);

    auto data_buf = read_res->transfer_data();
    ASSERT_TRUE(verify_pattern(data_buf.get(), block_size, 0xAA))
        << "Read data does not match written pattern!";
}

// ==================== 2. 乱序完成与 User_data 映射 ====================
TEST_F(SystemFunctionalTest, BatchSubmitWithUserDataMapping) {
    mAsyncDiskIO::async_io io(64);
    const size_t num_ops = 32;

    ASSERT_EQ(posix_fallocate(fd, 0, num_ops * block_size), 0);

    for (size_t i = 0; i < num_ops; ++i) {
        auto buf = allocate_aligned_buffer(block_size);
        fill_pattern(buf.get(), block_size, static_cast<uint8_t>(i));
        bool ok = io.prep_write(fd, std::move(buf), block_size, i, i * block_size);
        ASSERT_TRUE(ok);
    }
    ASSERT_TRUE(io.submit());

    std::set<uint64_t> written_ids;
    while (io.result_count() > 0) {
        auto res = io.get_result();
        ASSERT_TRUE(res != nullptr);
        ASSERT_GT(res->wait(), 0);
        written_ids.insert(res->user_data());
        res->transfer_data();
    }
    ASSERT_EQ(written_ids.size(), num_ops);

    for (size_t i = 0; i < num_ops; ++i) {
        auto buf = allocate_aligned_buffer(block_size);
        bool ok = io.prep_read(fd, std::move(buf), block_size, i, i * block_size);
        ASSERT_TRUE(ok);
    }
    ASSERT_TRUE(io.submit());

    while (io.result_count() > 0) {
        auto res = io.get_result();
        ASSERT_TRUE(res != nullptr);
        ASSERT_GT(res->wait(), 0);

        uint64_t uid = res->user_data();
        ASSERT_LT(uid, num_ops);

        auto data_buf = res->transfer_data();
        ASSERT_TRUE(verify_pattern(data_buf.get(), block_size, static_cast<uint8_t>(uid)))
            << "Data mismatch for user_data=" << uid;
    }
}

// ==================== 3. 错误处理 (非法 FD) ====================
TEST_F(SystemFunctionalTest, InvalidFileDescriptor) {
    mAsyncDiskIO::async_io io(32);
    auto buf = allocate_aligned_buffer(block_size);

    auto res = io.write(-1, std::move(buf), block_size, 9999, 0);
    ASSERT_TRUE(res != nullptr);

    // io_uring 返回 -errno（如 -9 = -EBADF）
    int bytes = res->wait();
    EXPECT_LT(bytes, 0) << "Expected negative errno, got: " << bytes;

    auto ret_buf = res->transfer_data();
    ASSERT_NE(ret_buf.get(), nullptr);
}

// ==================== 4. O_DIRECT 对齐错误 ====================
TEST_F(SystemFunctionalTest, MisalignedBufferWithODirect) {
    if (!has_odirect) {
        GTEST_SKIP() << "O_DIRECT not enabled on this filesystem, skipping.";
    }

    mAsyncDiskIO::async_io io(32);

    void* raw_mem = malloc(block_size + 1);
    ASSERT_NE(raw_mem, nullptr);
    uint8_t* misaligned_ptr = static_cast<uint8_t*>(raw_mem) + 1;

    auto misaligned_buf = mAsyncDiskIO::make_unique_buf(misaligned_ptr, misaligned_deleter);

    bool prep_ok = io.prep_write(fd, std::move(misaligned_buf), block_size, 8888, 0);
    if (prep_ok) {
        ASSERT_TRUE(io.submit());
        ASSERT_GT(io.result_count(), 0u);
        auto res = io.get_result();
        ASSERT_TRUE(res != nullptr);
        int bytes = res->wait();

        // 注意：某些文件系统（如 tmpfs）即使 O_DIRECT 也不强制对齐。
        // 这是内核行为，不是库的 bug。记录但不硬断。
        if (bytes < 0) {
            std::cout << "  [INFO] Kernel correctly rejected misaligned buffer (errno=" << -bytes << ")\n";
        } else {
            std::cout << "  [INFO] Kernel accepted misaligned buffer (bytes=" << bytes
                      << "). Filesystem does not enforce O_DIRECT alignment.\n";
        }
        auto ret_buf = res->transfer_data();
        ASSERT_NE(ret_buf.get(), nullptr);
    }
}

// ==================== 5. 状态机验证 ====================
TEST_F(SystemFunctionalTest, PeekAndWaitStateTransitions) {
    mAsyncDiskIO::async_io io(32);
    ASSERT_EQ(posix_fallocate(fd, 0, block_size), 0);

    auto buf = allocate_aligned_buffer(block_size);
    fill_pattern(buf.get(), block_size, 0xCC);

    ASSERT_TRUE(io.prep_write(fd, std::move(buf), block_size, 7777, 0));
    ASSERT_TRUE(io.submit());

    ASSERT_GT(io.result_count(), 0u);
    auto res = io.get_result();
    ASSERT_TRUE(res != nullptr);
    ASSERT_TRUE(res->valid());

    int max_retries = 1000;
    mAsyncDiskIO::state s = res->peek();
    while (s == mAsyncDiskIO::state::UNFINISHED && max_retries-- > 0) {
        s = res->peek();
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    ASSERT_NE(s, mAsyncDiskIO::state::UNFINISHED);

    int bytes = res->wait();
    ASSERT_EQ(bytes, (int)block_size);
    res->transfer_data();
}

// ==================== 6. 超过 4GB 边界的大偏移 ====================
TEST_F(SystemFunctionalTest, LargeOffsetOver4GB) {
    mAsyncDiskIO::async_io io(32);

    off_t large_offset = 4ULL * 1024 * 1024 * 1024 + 512;
    ASSERT_EQ(posix_fallocate(fd, 0, large_offset + block_size), 0);

    auto write_buf = allocate_aligned_buffer(block_size);
    fill_pattern(write_buf.get(), block_size, 0xDD);

    ASSERT_TRUE(io.prep_write(fd, std::move(write_buf), block_size, 5555, large_offset));
    ASSERT_TRUE(io.submit());

    ASSERT_GT(io.result_count(), 0u);
    auto w_res = io.get_result();
    ASSERT_TRUE(w_res != nullptr);
    ASSERT_EQ(w_res->wait(), (int)block_size);
    w_res->transfer_data();

    auto read_buf = allocate_aligned_buffer(block_size);
    ASSERT_TRUE(io.prep_read(fd, std::move(read_buf), block_size, 5556, large_offset));
    ASSERT_TRUE(io.submit());

    ASSERT_GT(io.result_count(), 0u);
    auto r_res = io.get_result();
    ASSERT_TRUE(r_res != nullptr);
    ASSERT_EQ(r_res->wait(), (int)block_size);

    auto data_buf = r_res->transfer_data();
    ASSERT_TRUE(verify_pattern(data_buf.get(), block_size, 0xDD));
}

// ==================== 7. 内存安全 (不消费 transfer_data) ====================
TEST_F(SystemFunctionalTest, MemorySafetyWithoutConsumingData) {
    mAsyncDiskIO::async_io io(32);
    ASSERT_EQ(posix_fallocate(fd, 0, block_size), 0);

    auto buf = allocate_aligned_buffer(block_size);
    fill_pattern(buf.get(), block_size, 0xEE);

    ASSERT_TRUE(io.prep_write(fd, std::move(buf), block_size, 9999, 0));
    ASSERT_TRUE(io.submit());

    ASSERT_GT(io.result_count(), 0u);
    auto res = io.get_result();
    ASSERT_TRUE(res != nullptr);
    ASSERT_EQ(res->wait(), (int)block_size);
    // 故意不调用 transfer_data()，让 res 析构。ASAN 不应报泄漏。
}

// ==================== 8. EOF 读取 (空文件) ====================
TEST_F(SystemFunctionalTest, ReadBeyondEOF) {
    mAsyncDiskIO::async_io io(32);

    // 不 fallocate，文件大小为 0
    auto buf = allocate_aligned_buffer(block_size);
    ASSERT_TRUE(io.prep_read(fd, std::move(buf), block_size, 1234, 0));
    ASSERT_TRUE(io.submit());

    ASSERT_GT(io.result_count(), 0u);
    auto res = io.get_result();
    ASSERT_TRUE(res != nullptr);

    int bytes = res->wait();
    // 空文件读取应返回 0 字节 (EOF)
    // 但缓冲 I/O 模式下可能返回 0，O_DIRECT 模式下可能返回 -EINVAL
    EXPECT_TRUE(bytes == 0 || bytes < 0)
        << "Expected 0 (EOF) or negative errno, got: " << bytes;

    auto ret_buf = res->transfer_data();
    ASSERT_NE(ret_buf.get(), nullptr);
}
