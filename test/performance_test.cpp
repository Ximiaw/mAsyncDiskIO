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

// 辅助函数：分配 O_DIRECT 需要的对齐内存
// 对齐值取 4096 和块大小的最大值，确保大块 O_DIRECT 也符合要求
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
    size_t block_size;      // 块大小 (字节)
    size_t total_data_mb;   // 总数据量 (MB)
    std::string description; // 测试描述

    // 构造函数，方便初始化
    PerfParams(size_t bs, size_t total_mb, std::string desc)
        : block_size(bs), total_data_mb(total_mb), description(std::move(desc)) {}
};

// 重载输出运算符，让 gtest 在打印时显示可读的参数名
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

    void SetUp() override {
        auto params = GetParam();
        block_size = params.block_size;
        total_bytes = params.total_data_mb * 1000000ULL; // 1 MB = 1,000,000 Bytes
        total_blocks = total_bytes / block_size;
        
        // 动态控制批量大小：限制最大并发内存不超过 64MB
        // 防止大块测试时 OOM (例如 1MB block * 256 batch = 256MB 内存占用)
        size_t max_batch_by_mem = (64 * 1000000ULL) / block_size;
        batch_size = std::min<size_t>({256ULL, max_batch_by_mem, total_blocks});
        if (batch_size == 0) batch_size = 1;

        queue_depth = batch_size * 2;

        fd = open(test_file, O_RDWR | O_CREAT | O_DIRECT | O_TRUNC, 0644);
        if (fd < 0) {
            fd = open(test_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
        }
        ASSERT_GE(fd, 0) << "Failed to open test file";
        
        // 预分配文件空间，防止写入时动态分配导致延迟抖动
        ASSERT_EQ(posix_fallocate(fd, 0, total_bytes), 0) << "Failed to fallocate file";
    }

    void TearDown() override {
        if (fd >= 0) {
            close(fd);
            unlink(test_file);
        }
    }
};

// ==================== 参数组合实例化 ====================
// 覆盖：小块常规、小块大文件、大块常规、大块大文件
INSTANTIATE_TEST_SUITE_P(
    DiskIOBenchmark,
    ParameterizedPerfTest,
    ::testing::Values(
        PerfParams(4096,       100,  "4KB_Block_100MB_File"),    // 经典 4K 随机/顺序场景
        PerfParams(4096,       1000, "4KB_Block_1GB_File"),      // 小块大文件，高 IOPS 测试
        PerfParams(131072,     100,  "128KB_Block_100MB_File"),  // 中等块大小
        PerfParams(1048576,    1000, "1MB_Block_1GB_File"),      // 大块，测试纯带宽极限
        PerfParams(1048576,    2000, "1MB_Block_2GB_File")       // 超大文件顺序读写
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
            auto buf = allocate_aligned_buffer(block_size, block_size);
            bool ok = io.prep_write(
                fd, std::move(buf), block_size,
                blocks_submitted,          // user_data
                blocks_submitted * block_size  // offset
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
            res->transfer_data();
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
        auto buf = allocate_aligned_buffer(block_size, block_size);
        ssize_t w = pwrite(fd, buf.get(), block_size, i * block_size);
        ASSERT_EQ(w, (ssize_t)block_size);
    }

    mAsyncDiskIO::async_io io(queue_depth);

    size_t blocks_submitted = 0;
    size_t blocks_reaped = 0;

    auto start = std::chrono::high_resolution_clock::now();

    while (blocks_reaped < total_blocks) {
        size_t prep_count = 0;
        while (blocks_submitted < total_blocks && prep_count < batch_size) {
            auto buf = allocate_aligned_buffer(block_size, block_size);
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
            res->transfer_data();
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
