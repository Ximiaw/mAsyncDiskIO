// Copyright (c) 2026 Ximiaw
// SPDX-License-Identifier: MIT
// 吞吐量测试：peek 与 wait 分开测，采用流水线提交控制 inflight

#include <gtest/gtest.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <vector>
#include <memory>
#include <numeric>
#include <cstring>
#include <iostream>
#include <iomanip>

#include "async_io.h"
#include "data_struct.h"

using namespace mAsyncDiskIO;
using namespace std::chrono;

// ────────────────────── 测试配置 ──────────────────────
constexpr size_t kQueueDepth = 256;
constexpr size_t kBlockSize = 4096;
constexpr size_t kTotalIoOps = 4096;
constexpr size_t kFileSize = kBlockSize * kTotalIoOps;
constexpr size_t kWarmupOps = 64;
constexpr size_t kMaxInflight = kQueueDepth - 16; // 留余量避免队列满

// ────────────────────── 工具函数 ──────────────────────

// 对齐分配（O_DIRECT 需要 512 字节对齐，这里直接用页大小）
static uint8_t* aligned_alloc_buf(size_t size) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 4096, size) != 0) {
        return nullptr;
    }
    return static_cast<uint8_t*>(ptr);
}

static int open_test_file(size_t size) {
    char path[] = "/tmp/masyncdiskio_bench_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    unlink(path); // 进程退出后自动清理

    if (posix_fallocate(fd, 0, size) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void prep_data(uint8_t* buf, size_t size, size_t seed) {
    for (size_t i = 0; i < size; ++i) {
        buf[i] = static_cast<uint8_t>((seed + i) & 0xFF);
    }
}

#define PRINT_RESULT(io_type, wait_type, ops, bytes, elapsed_us)             \
    do {                                                                     \
        double sec = static_cast<double>(elapsed_us) / 1'000'000.0;          \
        double iops = static_cast<double>(ops) / sec;                        \
        double bw_mbps = (static_cast<double>(bytes) / (1024.0 * 1024.0)) / sec; \
        double us_per_op = static_cast<double>(elapsed_us) / ops;            \
        std::cout << "\n[Throughput] " << io_type << " + " << wait_type       \
                  << "\n  Operations: " << ops                               \
                  << "\n  Total data: " << (bytes / (1024 * 1024)) << " MB" \
                  << "\n  Elapsed:    " << std::fixed << std::setprecision(3) \
                  << sec << " s"                                             \
                  << "\n  IOPS:       " << static_cast<size_t>(iops)         \
                  << "\n  Bandwidth:  " << std::fixed << std::setprecision(2) \
                  << bw_mbps << " MB/s"                                      \
                  << "\n  Latency:    " << std::fixed << std::setprecision(2) \
                  << us_per_op << " us/op\n";                               \
    } while (0)

// ────────────────────── 流水线提交 + peek 轮询 ──────────────────────

template<typename SubmitFn, typename PollFn>
static double run_pipeline_peek(SubmitFn&& submit, PollFn&& poll_done, size_t total_ops) {
    std::vector<decltype(submit(0))> inflight;
    inflight.reserve(kMaxInflight);

    size_t submitted = 0;
    size_t completed = 0;

    auto t_start = high_resolution_clock::now();

    while (completed < total_ops) {
        // 提交新请求，维持 inflight
        while (submitted < total_ops && inflight.size() < kMaxInflight) {
            auto r = submit(submitted);
            if (!r) break; // 队列满
            inflight.push_back(std::move(r));
            ++submitted;
        }

        // peek 轮询已完成的
        for (auto it = inflight.begin(); it != inflight.end(); ) {
            auto st = (*it)->peek();
            if (st == state::FINISH) {
                poll_done(*it);
                (*it)->finish();
                *it = std::move(inflight.back());
                inflight.pop_back();
                ++completed;
            } else if (st == state::ERROR) {
                (*it)->finish();
                *it = std::move(inflight.back());
                inflight.pop_back();
                ++completed;
            } else {
                ++it;
            }
        }
    }

    auto t_end = high_resolution_clock::now();
    return duration_cast<microseconds>(t_end - t_start).count();
}

// ────────────────────── 流水线提交 + wait 阻塞 ──────────────────────

template<typename SubmitFn, typename WaitFn>
static double run_pipeline_wait(SubmitFn&& submit, WaitFn&& wait_done, size_t total_ops) {
    using ResultT = decltype(submit(0));
    std::vector<ResultT> inflight;
    inflight.reserve(kMaxInflight);

    size_t submitted = 0;
    size_t completed = 0;

    auto t_start = high_resolution_clock::now();

    while (completed < total_ops) {
        // 提交新请求
        while (submitted < total_ops && inflight.size() < kMaxInflight) {
            auto r = submit(submitted);
            if (!r) break;
            inflight.push_back(std::move(r));
            ++submitted;
        }

        // wait 最早提交的（FIFO，减少延迟累积）
        if (!inflight.empty()) {
            auto& r = inflight.front();
            r->wait();  // 阻塞等待这个完成
            wait_done(r);
            r->finish();
            // swap-pop 到队首
            r = std::move(inflight.back());
            inflight.pop_back();
            ++completed;
        }
    }

    auto t_end = high_resolution_clock::now();
    return duration_cast<microseconds>(t_end - t_start).count();
}

// ────────────────────── 串行提交 + wait（每次系统调用） ──────────────────────

template<typename SubmitFn, typename WaitFn>
static double run_serial_wait(SubmitFn&& submit, WaitFn&& wait_done, size_t total_ops) {
    auto t_start = high_resolution_clock::now();

    for (size_t i = 0; i < total_ops; ++i) {
        auto r = submit(i);
        EXPECT_NE(r, nullptr);
        r->wait();
        wait_done(r);
        r->finish();
    }

    auto t_end = high_resolution_clock::now();
    return duration_cast<microseconds>(t_end - t_start).count();
}

// ────────────────────── 测试夹具 ──────────────────────

class ThroughputTest : public ::testing::Test {
protected:
    int fd_ = -1;

    void SetUp() override {
        fd_ = open_test_file(kFileSize);
        ASSERT_GE(fd_, 0) << "Failed to create test file";
    }

    void TearDown() override {
        if (fd_ >= 0) close(fd_);
    }
};

// ────────────────────── Write + peek（流水线，无系统调用） ──────────────────────

TEST_F(ThroughputTest, WriteThroughputPeek) {
    async_io io(kQueueDepth);

    // 预热
    for (size_t i = 0; i < kWarmupOps; ++i) {
        uint8_t* buf = aligned_alloc_buf(kBlockSize);
        ASSERT_NE(buf, nullptr);
        prep_data(buf, kBlockSize, i);
        auto w = io.write(fd_, make_unique_buf(buf), kBlockSize, i * kBlockSize, i);
        ASSERT_NE(w, nullptr);
        while (w->peek() == state::UNFINISHED) {}
        w->finish();
    }

    // 正式测试
    auto submit_fn = [&](size_t idx) -> shared_result_write {
        uint8_t* buf = aligned_alloc_buf(kBlockSize);
        if (!buf) return nullptr;
        prep_data(buf, kBlockSize, idx);
        return io.write(fd_, make_unique_buf(buf), kBlockSize, idx * kBlockSize, idx);
    };
    auto poll_fn = [&](shared_result_write& r) {
        EXPECT_EQ(r->size(), kBlockSize);
    };

    double us = run_pipeline_peek(submit_fn, poll_fn, kTotalIoOps);
    fdatasync(fd_);
    PRINT_RESULT("Write", "peek(pipeline)", kTotalIoOps, kFileSize, us);
}

// ────────────────────── Write + wait（流水线，wait 系统调用） ──────────────────────

TEST_F(ThroughputTest, WriteThroughputWait) {
    async_io io(kQueueDepth);

    for (size_t i = 0; i < kWarmupOps; ++i) {
        uint8_t* buf = aligned_alloc_buf(kBlockSize);
        ASSERT_NE(buf, nullptr);
        prep_data(buf, kBlockSize, i);
        auto w = io.write(fd_, make_unique_buf(buf), kBlockSize, i * kBlockSize, i);
        ASSERT_NE(w, nullptr);
        EXPECT_EQ(w->wait(), static_cast<int>(kBlockSize));
        w->finish();
    }

    auto submit_fn = [&](size_t idx) -> shared_result_write {
        uint8_t* buf = aligned_alloc_buf(kBlockSize);
        if (!buf) return nullptr;
        prep_data(buf, kBlockSize, idx);
        return io.write(fd_, make_unique_buf(buf), kBlockSize, idx * kBlockSize, idx);
    };
    auto wait_fn = [&](shared_result_write& r) {
        EXPECT_EQ(r->size(), kBlockSize);
    };

    double us = run_pipeline_wait(submit_fn, wait_fn, kTotalIoOps);
    fdatasync(fd_);
    PRINT_RESULT("Write", "wait(pipeline)", kTotalIoOps, kFileSize, us);
}

// ────────────────────── Write + wait（串行，每次 wait 系统调用） ──────────────────────

TEST_F(ThroughputTest, WriteThroughputWaitSerial) {
    async_io io(kQueueDepth);

    auto submit_fn = [&](size_t idx) -> shared_result_write {
        uint8_t* buf = aligned_alloc_buf(kBlockSize);
        if (!buf) return nullptr;
        prep_data(buf, kBlockSize, idx);
        return io.write(fd_, make_unique_buf(buf), kBlockSize, idx * kBlockSize, idx);
    };
    auto wait_fn = [&](shared_result_write& r) {
        EXPECT_EQ(r->size(), static_cast<int>(kBlockSize));
    };

    // 串行模式只测少量 ops，否则太慢
    constexpr size_t kSerialOps = 256;
    double us = run_serial_wait(submit_fn, wait_fn, kSerialOps);
    fdatasync(fd_);
    PRINT_RESULT("Write", "wait(serial)", kSerialOps, kSerialOps * kBlockSize, us);
}

// ────────────────────── Read + peek（流水线） ──────────────────────

TEST_F(ThroughputTest, ReadThroughputPeek) {
    async_io io(kQueueDepth);

    // 1. 先写入测试数据 —— 流水线控制 inflight
    {
        std::vector<shared_result_write> writes;
        writes.reserve(kMaxInflight);
        size_t wr_sub = 0, wr_done = 0;
        while (wr_done < kTotalIoOps) {
            while (wr_sub < kTotalIoOps && writes.size() < kMaxInflight) {
                uint8_t* buf = aligned_alloc_buf(kBlockSize);
                if (!buf) break;
                prep_data(buf, kBlockSize, wr_sub);
                auto w = io.write(fd_, make_unique_buf(buf), kBlockSize,
                                  wr_sub * kBlockSize, wr_sub);
                if (!w) break;
                writes.push_back(w);
                ++wr_sub;
            }
            for (auto it = writes.begin(); it != writes.end(); ) {
                if ((*it)->peek() == state::FINISH) {
                    (*it)->finish();
                    *it = std::move(writes.back());
                    writes.pop_back();
                    ++wr_done;
                } else {
                    ++it;
                }
            }
        }
        fdatasync(fd_);
    }

    // 2. 清 page cache（需要 root；失败则继续，只是测 page cache 性能）
    sync();
    int drop_fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
    if (drop_fd >= 0) {
        write(drop_fd, "3", 1);
        close(drop_fd);
    }

    // 3. 回到文件开头开始读
    lseek(fd_, 0, SEEK_SET);

    // 4. 预热
    for (size_t i = 0; i < kWarmupOps; ++i) {
        auto r = io.read(fd_, kBlockSize, i * kBlockSize, i);
        ASSERT_NE(r, nullptr);
        while (r->peek() == state::UNFINISHED) {}
        EXPECT_EQ(r->size(), kBlockSize);
        r->finish();
    }

    // 5. 正式测试
    auto submit_fn = [&](size_t idx) -> shared_result_read {
        return io.read(fd_, kBlockSize, idx * kBlockSize, idx);
    };
    auto poll_fn = [&](shared_result_read& r) {
        EXPECT_EQ(r->size(), kBlockSize);
        auto data = r->transfer_data();
        (void)data; // 实际消费数据
    };

    double us = run_pipeline_peek(submit_fn, poll_fn, kTotalIoOps);
    PRINT_RESULT("Read", "peek(pipeline)", kTotalIoOps, kFileSize, us);
}

// ────────────────────── Read + wait（流水线） ──────────────────────

TEST_F(ThroughputTest, ReadThroughputWait) {
    async_io io(kQueueDepth);

    // 1. 写入 —— 流水线控制 inflight
    {
        std::vector<shared_result_write> writes;
        writes.reserve(kMaxInflight);
        size_t wr_sub = 0, wr_done = 0;
        while (wr_done < kTotalIoOps) {
            while (wr_sub < kTotalIoOps && writes.size() < kMaxInflight) {
                uint8_t* buf = aligned_alloc_buf(kBlockSize);
                if (!buf) break;
                prep_data(buf, kBlockSize, wr_sub);
                auto w = io.write(fd_, make_unique_buf(buf), kBlockSize,
                                  wr_sub * kBlockSize, wr_sub);
                if (!w) break;
                writes.push_back(w);
                ++wr_sub;
            }
            for (auto it = writes.begin(); it != writes.end(); ) {
                if ((*it)->peek() == state::FINISH) {
                    (*it)->finish();
                    *it = std::move(writes.back());
                    writes.pop_back();
                    ++wr_done;
                } else {
                    ++it;
                }
            }
        }
        fdatasync(fd_);
    }

    sync();
    int drop_fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
    if (drop_fd >= 0) {
        write(drop_fd, "3", 1);
        close(drop_fd);
    }

    lseek(fd_, 0, SEEK_SET);

    for (size_t i = 0; i < kWarmupOps; ++i) {
        auto r = io.read(fd_, kBlockSize, i * kBlockSize, i);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(r->wait(), static_cast<int>(kBlockSize));
        r->finish();
    }

    auto submit_fn = [&](size_t idx) -> shared_result_read {
        return io.read(fd_, kBlockSize, idx * kBlockSize, idx);
    };
    auto wait_fn = [&](shared_result_read& r) {
        EXPECT_EQ(r->size(), kBlockSize);
        auto data = r->transfer_data();
        (void)data;
    };

    double us = run_pipeline_wait(submit_fn, wait_fn, kTotalIoOps);
    PRINT_RESULT("Read", "wait(pipeline)", kTotalIoOps, kFileSize, us);
}

// ────────────────────── Mixed RW + peek ──────────────────────

TEST_F(ThroughputTest, MixedReadWritePeek) {
    async_io io(kQueueDepth);
    constexpr size_t kHalfOps = kTotalIoOps / 2;
    const size_t kWriteBytes = kHalfOps * kBlockSize;

    // 先预写后半段（给读用）—— 用流水线控制 inflight
    {
        std::vector<shared_result_write> prewrites;
        prewrites.reserve(kMaxInflight);
        size_t pre_sub = 0, pre_done = 0;
        while (pre_done < kHalfOps) {
            while (pre_sub < kHalfOps && prewrites.size() < kMaxInflight) {
                uint8_t* buf = aligned_alloc_buf(kBlockSize);
                if (!buf) break;
                prep_data(buf, kBlockSize, pre_sub + kHalfOps);
                auto w = io.write(fd_, make_unique_buf(buf), kBlockSize,
                                  (kHalfOps + pre_sub) * kBlockSize, kHalfOps + pre_sub);
                if (!w) break;
                prewrites.push_back(w);
                ++pre_sub;
            }
            for (auto it = prewrites.begin(); it != prewrites.end(); ) {
                if ((*it)->peek() == state::FINISH) {
                    (*it)->finish();
                    *it = std::move(prewrites.back());
                    prewrites.pop_back();
                    ++pre_done;
                } else {
                    ++it;
                }
            }
        }
        fdatasync(fd_);
    }

    lseek(fd_, 0, SEEK_SET);

    // 预热（串行，inflight 始终为 1，不会爆队列）
    for (size_t i = 0; i < kWarmupOps / 2; ++i) {
        uint8_t* buf = aligned_alloc_buf(kBlockSize);
        ASSERT_NE(buf, nullptr);
        auto w = io.write(fd_, make_unique_buf(buf), kBlockSize, i * kBlockSize, i);
        ASSERT_NE(w, nullptr);
        while (w->peek() == state::UNFINISHED) {}
        w->finish();
    }
    for (size_t i = 0; i < kWarmupOps / 2; ++i) {
        auto r = io.read(fd_, kBlockSize, (kHalfOps + i) * kBlockSize, kHalfOps + i);
        ASSERT_NE(r, nullptr);
        while (r->peek() == state::UNFINISHED) {}
        r->finish();
    }

    // 混合提交：写前半段 + 读后半段
    struct MixedReq {
        shared_result_write w;
        shared_result_read r;
        bool is_write;
    };
    std::vector<MixedReq> inflight;
    inflight.reserve(kMaxInflight);

    size_t wr_sub = 0, rd_sub = 0;
    size_t wr_done = 0, rd_done = 0;

    auto t_start = high_resolution_clock::now();

    while (wr_done < kHalfOps || rd_done < kHalfOps) {
        // 提交写
        while (wr_sub < kHalfOps && inflight.size() < kMaxInflight) {
            uint8_t* buf = aligned_alloc_buf(kBlockSize);
            if (!buf) break;
            prep_data(buf, kBlockSize, wr_sub);
            auto w = io.write(fd_, make_unique_buf(buf), kBlockSize,
                              wr_sub * kBlockSize, wr_sub);
            if (!w) break;
            inflight.push_back({w, nullptr, true});
            ++wr_sub;
        }
        // 提交读
        while (rd_sub < kHalfOps && inflight.size() < kMaxInflight) {
            auto r = io.read(fd_, kBlockSize,
                             (kHalfOps + rd_sub) * kBlockSize, kHalfOps + rd_sub);
            if (!r) break;
            inflight.push_back({nullptr, r, false});
            ++rd_sub;
        }

        // peek 轮询
        for (auto it = inflight.begin(); it != inflight.end(); ) {
            state st;
            if (it->is_write) {
                st = it->w->peek();
            } else {
                st = it->r->peek();
            }
            if (st == state::FINISH) {
                if (it->is_write) {
                    EXPECT_EQ(it->w->size(), kBlockSize);
                    it->w->finish();
                    ++wr_done;
                } else {
                    EXPECT_EQ(it->r->size(), kBlockSize);
                    auto data = it->r->transfer_data();
                    (void)data;
                    it->r->finish();
                    ++rd_done;
                }
                *it = std::move(inflight.back());
                inflight.pop_back();
            } else if (st == state::ERROR) {
                if (it->is_write) it->w->finish(); else it->r->finish();
                *it = std::move(inflight.back());
                inflight.pop_back();
                (it->is_write ? wr_done : rd_done)++;
            } else {
                ++it;
            }
        }
    }

    auto t_end = high_resolution_clock::now();
    double us = duration_cast<microseconds>(t_end - t_start).count();
    size_t total_ops = kHalfOps * 2;
    size_t total_bytes = kFileSize;

    fdatasync(fd_);
    PRINT_RESULT("Mixed RW", "peek(pipeline)", total_ops, total_bytes, us);
}

// ────────────────────── Mixed RW + wait ──────────────────────

TEST_F(ThroughputTest, MixedReadWriteWait) {
    async_io io(kQueueDepth);
    constexpr size_t kHalfOps = kTotalIoOps / 2;

    // 预写后半段 —— 流水线控制 inflight
    {
        std::vector<shared_result_write> prewrites;
        prewrites.reserve(kMaxInflight);
        size_t pre_sub = 0, pre_done = 0;
        while (pre_done < kHalfOps) {
            while (pre_sub < kHalfOps && prewrites.size() < kMaxInflight) {
                uint8_t* buf = aligned_alloc_buf(kBlockSize);
                if (!buf) break;
                prep_data(buf, kBlockSize, pre_sub + kHalfOps);
                auto w = io.write(fd_, make_unique_buf(buf), kBlockSize,
                                  (kHalfOps + pre_sub) * kBlockSize, kHalfOps + pre_sub);
                if (!w) break;
                prewrites.push_back(w);
                ++pre_sub;
            }
            for (auto it = prewrites.begin(); it != prewrites.end(); ) {
                if ((*it)->peek() == state::FINISH) {
                    (*it)->finish();
                    *it = std::move(prewrites.back());
                    prewrites.pop_back();
                    ++pre_done;
                } else {
                    ++it;
                }
            }
        }
        fdatasync(fd_);
    }

    lseek(fd_, 0, SEEK_SET);

    std::vector<shared_result_write> writes;
    std::vector<shared_result_read> reads;
    writes.reserve(kHalfOps);
    reads.reserve(kHalfOps);

    // 准备所有写缓冲区
    std::vector<uint8_t*> write_bufs;
    write_bufs.reserve(kHalfOps);
    for (size_t i = 0; i < kHalfOps; ++i) {
        uint8_t* buf = aligned_alloc_buf(kBlockSize);
        ASSERT_NE(buf, nullptr);
        prep_data(buf, kBlockSize, i);
        write_bufs.push_back(buf);
    }

    auto t_start = high_resolution_clock::now();

    // 全部提交（流水线控制 inflight）
    size_t wr_sub = 0, rd_sub = 0;
    size_t wr_done = 0, rd_done = 0;

    while (wr_done < kHalfOps || rd_done < kHalfOps) {
        while (wr_sub < kHalfOps &&
               (writes.size() - wr_done + reads.size() - rd_done) < kMaxInflight) {
            auto w = io.write(fd_, make_unique_buf(write_bufs[wr_sub]),
                              kBlockSize, wr_sub * kBlockSize, wr_sub);
            if (!w) break;
            writes.push_back(w);
            ++wr_sub;
        }
        while (rd_sub < kHalfOps &&
               (writes.size() - wr_done + reads.size() - rd_done) < kMaxInflight) {
            auto r = io.read(fd_, kBlockSize,
                             (kHalfOps + rd_sub) * kBlockSize, kHalfOps + rd_sub);
            if (!r) break;
            reads.push_back(r);
            ++rd_sub;
        }

        // wait 最早提交的写
        if (wr_done < wr_sub) {
            EXPECT_EQ(writes[wr_done]->wait(), static_cast<int>(kBlockSize));
            writes[wr_done]->finish();
            ++wr_done;
        }
        // wait 最早提交的读
        if (rd_done < rd_sub) {
            EXPECT_EQ(reads[rd_done]->wait(), static_cast<int>(kBlockSize));
            auto data = reads[rd_done]->transfer_data();
            (void)data;
            reads[rd_done]->finish();
            ++rd_done;
        }
    }

    auto t_end = high_resolution_clock::now();
    double us = duration_cast<microseconds>(t_end - t_start).count();
    size_t total_ops = kHalfOps * 2;

    fdatasync(fd_);
    PRINT_RESULT("Mixed RW", "wait(pipeline)", total_ops, kFileSize, us);
}

// ────────────────────── 不同 Block Size 对比（peek 流水线） ──────────────────────

TEST_F(ThroughputTest, BlockSizeComparisonPeek) {
    async_io io(kQueueDepth);
    const std::vector<size_t> block_sizes = {512, 4096, 65536, 262144};

    std::cout << "\n[Block Size Comparison - Write + peek(pipeline)]\n";
    std::cout << "BlockSize  Ops     Data(MB)  Time(s)   IOPS      MB/s      us/op\n";
    std::cout << "---------  ------  --------  --------  --------  --------  --------\n";

    for (size_t bs : block_sizes) {
        size_t nops = kFileSize / bs;
        if (nops == 0) continue;

        auto submit_fn = [&](size_t idx) -> shared_result_write {
            uint8_t* buf = aligned_alloc_buf(bs);
            if (!buf) return nullptr;
            prep_data(buf, bs, idx);
            return io.write(fd_, make_unique_buf(buf), bs, idx * bs, idx);
        };
        auto poll_fn = [&](shared_result_write& r) {
            EXPECT_EQ(r->size(), bs);
        };

        double us = run_pipeline_peek(submit_fn, poll_fn, nops);
        fdatasync(fd_);

        double sec = us / 1'000'000.0;
        double iops = nops / sec;
        double mbps = (static_cast<double>(nops * bs) / (1024.0 * 1024.0)) / sec;
        double lat = us / nops;

        std::cout << std::setw(9) << bs << "  " << std::setw(6) << nops << "  "
                  << std::setw(8) << std::fixed << std::setprecision(2)
                  << (nops * bs / (1024.0 * 1024.0)) << "  "
                  << std::setw(8) << sec << "  "
                  << std::setw(8) << static_cast<size_t>(iops) << "  "
                  << std::setw(8) << std::setprecision(2) << mbps << "  "
                  << std::setw(8) << std::setprecision(2) << lat << "\n";
    }
}

TEST_F(ThroughputTest, BlockSizeComparisonWait) {
    async_io io(kQueueDepth);
    const std::vector<size_t> block_sizes = {512, 4096, 65536, 262144};

    std::cout << "\n[Block Size Comparison - Write + wait(pipeline)]\n";
    std::cout << "BlockSize  Ops     Data(MB)  Time(s)   IOPS      MB/s      us/op\n";
    std::cout << "---------  ------  --------  --------  --------  --------  --------\n";

    for (size_t bs : block_sizes) {
        size_t nops = kFileSize / bs;
        if (nops == 0) continue;

        auto submit_fn = [&](size_t idx) -> shared_result_write {
            uint8_t* buf = aligned_alloc_buf(bs);
            if (!buf) return nullptr;
            prep_data(buf, bs, idx);
            return io.write(fd_, make_unique_buf(buf), bs, idx * bs, idx);
        };
        auto wait_fn = [&](shared_result_write& r) {
            EXPECT_EQ(r->size(), bs);
        };

        double us = run_pipeline_wait(submit_fn, wait_fn, nops);
        fdatasync(fd_);

        double sec = us / 1'000'000.0;
        double iops = nops / sec;
        double mbps = (static_cast<double>(nops * bs) / (1024.0 * 1024.0)) / sec;
        double lat = us / nops;

        std::cout << std::setw(9) << bs << "  " << std::setw(6) << nops << "  "
                  << std::setw(8) << std::fixed << std::setprecision(2)
                  << (nops * bs / (1024.0 * 1024.0)) << "  "
                  << std::setw(8) << sec << "  "
                  << std::setw(8) << static_cast<size_t>(iops) << "  "
                  << std::setw(8) << std::setprecision(2) << mbps << "  "
                  << std::setw(8) << std::setprecision(2) << lat << "\n";
    }
}

// ────────────────────── 主函数 ──────────────────────

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}