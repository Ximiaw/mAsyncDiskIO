#include <gtest/gtest.h>
#include "async_io.h"
#include <chrono>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <set>
#include <algorithm>
#include <vector>

// 辅助函数：分配 O_DIRECT 需要的对齐内存
mAsyncDiskIO::unique_buf allocate_aligned_buffer(size_t size, size_t alignment = 4096) {
    void* ptr = nullptr;
    size_t actual_alignment = std::max(alignment, (size_t)4096);
    if (posix_memalign(&ptr, actual_alignment, size) != 0) {
        throw std::bad_alloc();
    }
    memset(ptr, 0, size);
    return mAsyncDiskIO::make_unique_buf(
        static_cast<uint8_t*>(ptr),
        [](uint8_t* p) { free(p); }
    );
}

// ==================== 测试参数结构体 ====================
struct PerfParams {
    size_t block_size;
    size_t total_data_mb;
    std::string description;

    PerfParams(size_t bs, size_t total_mb, std::string desc)
        : block_size(bs), total_data_mb(total_mb), description(std::move(desc)) {}
};

std::ostream& operator<<(std::ostream& os, const PerfParams& params) {
    os << params.description;
    return os;
}

// ==================== 参数化测试类 ====================
class ParameterizedPerfTest : public ::testing::TestWithParam<PerfParams> {
protected:
    int fd = -1;
    std::string str{std::string{std::getenv("HOME")}+"/mAsyncDiskIO_param_test.dat"};
    const char* test_file = str.c_str();
    size_t block_size;
    size_t total_bytes;
    size_t total_blocks;
    size_t batch_size;
    size_t queue_depth;

    // ==================== 内存池相关 ====================
    std::vector<mAsyncDiskIO::unique_buf> buffer_pool_;

    // 从池中获取一个缓冲区
    mAsyncDiskIO::unique_buf get_buffer() {
        if (buffer_pool_.empty()) {
            throw std::runtime_error("Buffer pool exhausted! Increase pool size.");
        }
        auto buf = std::move(buffer_pool_.back());
        buffer_pool_.pop_back();
        return buf;
    }

    // 将缓冲区归还到池中（避免析构和 free）
    void return_buffer(mAsyncDiskIO::unique_buf buf) {
        buffer_pool_.push_back(std::move(buf));
    }

    void SetUp() override {
        auto params = GetParam();
        block_size = params.block_size;
        total_bytes = params.total_data_mb * 1000000ULL;
        total_blocks = total_bytes / block_size;

        size_t max_batch_by_mem = (64 * 1000000ULL) / block_size;
        batch_size = std::min<size_t>({256ULL, max_batch_by_mem, total_blocks});
        if (batch_size == 0) batch_size = 1;
        queue_depth = batch_size * 2;

        fd = open(test_file, O_RDWR | O_CREAT | O_DIRECT | O_TRUNC, 0644);
        if (fd < 0) {
            fd = open(test_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
        }
        ASSERT_GE(fd, 0) << "Failed to open test file";
        ASSERT_EQ(posix_fallocate(fd, 0, total_bytes), 0) << "Failed to fallocate file";

        // ==================== 预分配内存池 ====================
        // 一次性分配 queue_depth 个缓冲区，后续只借还，不释放
        buffer_pool_.reserve(queue_depth);
        for (size_t i = 0; i < queue_depth; ++i) {
            buffer_pool_.push_back(allocate_aligned_buffer(block_size, block_size));
        }
    }

    void TearDown() override {
        // buffer_pool_ 析构时会统一释放内存，测试期间零释放
        if (fd >= 0) {
            close(fd);
            unlink(test_file);
        }
    }
};

INSTANTIATE_TEST_SUITE_P(
    DiskIOBenchmark,
    ParameterizedPerfTest,
    ::testing::Values(
        PerfParams(4096, 100, "4KB_Block_100MB_File"),
        PerfParams(4096, 1000, "4KB_Block_1GB_File"),
        PerfParams(131072, 100, "128KB_Block_100MB_File"),
        PerfParams(1048576, 1000, "1MB_Block_1GB_File"),
        PerfParams(1048576, 2000, "1MB_Block_2GB_File")
    )
);

// ==================== 参数化写入测试 ====================
TEST_P(ParameterizedPerfTest, WriteThroughput) {
    mAsyncDiskIO::async_io io(queue_depth);
    size_t blocks_submitted = 0;
    size_t blocks_reaped = 0;

    auto start = std::chrono::high_resolution_clock::now();

    while (blocks_reaped < total_blocks) {
        size_t prep_count = 0;
        while (blocks_submitted < total_blocks && prep_count < batch_size) {
            auto buf = get_buffer(); // 从池中借出
            bool ok = io.prep_write(
                fd, std::move(buf), block_size,
                blocks_submitted,
                blocks_submitted * block_size
            );
            if (!ok) break;
            blocks_submitted++;
            prep_count++;
        }

        ASSERT_TRUE(io.submit());

        while (io.result_count() > 0) {
            auto res = io.get_result();
            ASSERT_TRUE(res != nullptr);
            int bytes = res->wait();
            ASSERT_EQ(bytes, (int)block_size) << "write wait error";
            
            // 假设 transfer_data() 返回了 unique_buf 的所有权
            auto returned_buf = res->transfer_data();
            return_buffer(std::move(returned_buf)); // 归还给池
            
            blocks_reaped++;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    double total_mb = (double)total_bytes / 1000000.0;
    double throughput = total_mb / diff.count();
    double iops = total_blocks / diff.count();
    double lat_us = (diff.count() * 1e6) / total_blocks;

    std::cout << "\n===========================================\n";
    std::cout << "Write Benchmark: " << GetParam().description << "\n";
    std::cout << "Total Data   : " << total_mb << " MB\n";
    std::cout << "Block Size   : " << block_size / 1024 << " KiB\n";
    std::cout << "Queue Depth  : " << queue_depth << " (Batch: " << batch_size << ")\n";
    std::cout << "Total Time   : " << diff.count() << " s\n";
    std::cout << "Throughput   : " << throughput << " MB/s\n";
    std::cout << "IOPS         : " << iops << "\n";
    std::cout << "Avg Latency  : " << lat_us << " us/op\n";
    std::cout << "===========================================\n";
}

// ==================== 参数化读取测试 ====================
TEST_P(ParameterizedPerfTest, ReadThroughput) {
    // 先填满文件数据，确保读测试有真实数据
    for (size_t i = 0; i < total_blocks; ++i) {
        auto buf = get_buffer(); // 从池中借出
        ssize_t w = pwrite(fd, buf.get(), block_size, i * block_size);
        ASSERT_EQ(w, (ssize_t)block_size);
        return_buffer(std::move(buf)); // 立即归还
    }

    mAsyncDiskIO::async_io io(queue_depth);
    size_t blocks_submitted = 0;
    size_t blocks_reaped = 0;

    fsync(fd);
    posix_fadvise(fd, 0, total_bytes, POSIX_FADV_DONTNEED);

    auto start = std::chrono::high_resolution_clock::now();

    while (blocks_reaped < total_blocks) {
        size_t prep_count = 0;
        while (blocks_submitted < total_blocks && prep_count < batch_size) {
            auto buf = get_buffer(); // 从池中借出
            bool ok = io.prep_read(
                fd, std::move(buf), block_size,
                blocks_submitted,
                blocks_submitted * block_size
            );
            if (!ok) break;
            blocks_submitted++;
            prep_count++;
        }

        ASSERT_TRUE(io.submit());

        while (io.result_count() > 0) {
            auto res = io.get_result();
            ASSERT_TRUE(res != nullptr);
            int bytes = res->wait();
            ASSERT_EQ(bytes, (int)block_size) << "read wait error";
            
            auto returned_buf = res->transfer_data();
            return_buffer(std::move(returned_buf)); // 归还给池
            
            blocks_reaped++;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    double total_mb = (double)total_bytes / 1000000.0;
    double throughput = total_mb / diff.count();
    double iops = total_blocks / diff.count();
    double lat_us = (diff.count() * 1e6) / total_blocks;

    std::cout << "\n===========================================\n";
    std::cout << "Read Benchmark: " << GetParam().description << "\n";
    std::cout << "Total Data   : " << total_mb << " MB\n";
    std::cout << "Block Size   : " << block_size / 1024 << " KiB\n";
    std::cout << "Queue Depth  : " << queue_depth << " (Batch: " << batch_size << ")\n";
    std::cout << "Total Time   : " << diff.count() << " s\n";
    std::cout << "Throughput   : " << throughput << " MB/s\n";
    std::cout << "IOPS         : " << iops << "\n";
    std::cout << "Avg Latency  : " << lat_us << " us/op\n";
    std::cout << "===========================================\n";
}
