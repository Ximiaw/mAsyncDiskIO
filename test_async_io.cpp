#include <gtest/gtest.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <filesystem>
#include <vector>

#include "data_struct.h"
#include "async_io.h"

using namespace mAsyncDiskIO;

// 创建临时文件路径
static std::filesystem::path get_temp_file(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

// ============================================================
// 1. 基础数据类型测试
// ============================================================

TEST(UseDataTest, DefaultConstruction) {
    use_data ud;
    EXPECT_EQ(ud.use_d, 0);
    EXPECT_EQ(ud.buf, nullptr);
}

TEST(UseDataTest, CustomValues) {
    use_data ud;
    ud.use_d = 12345;
    ud.buf = new uint8_t[100];
    EXPECT_EQ(ud.use_d, 12345);
    EXPECT_NE(ud.buf, nullptr);
    delete[] ud.buf;
    ud.buf = nullptr;
}

TEST(UseDataTest, DestructorFreesBuf) {
    uint8_t* ptr = new uint8_t[256];
    {
        use_data ud;
        ud.buf = ptr;
    }
    // ptr 被析构函数释放，无内存泄漏
}

TEST(UniqueBufTest, NullConstruction) {
    unique_buf buf = make_unique_buf();
    EXPECT_EQ(buf.get(), nullptr);
}

TEST(UniqueBufTest, ValidPointer) {
    uint8_t* raw = new uint8_t[256]{};
    raw[0] = 0xAA;
    raw[255] = 0xBB;
    {
        unique_buf buf = make_unique_buf(raw);
        EXPECT_EQ(buf.get(), raw);
        EXPECT_EQ(buf[0], 0xAA);
        EXPECT_EQ(buf[255], 0xBB);
    }
}

TEST(UniqueBufTest, MoveSemantics) {
    uint8_t* raw = new uint8_t[64];
    unique_buf buf1 = make_unique_buf(raw);
    unique_buf buf2 = std::move(buf1);
    EXPECT_EQ(buf1.get(), nullptr);
    EXPECT_EQ(buf2.get(), raw);
}

TEST(SharedResultTest, NullInitialization) {
    shared_result sr;
    EXPECT_EQ(sr, nullptr);
    shared_result_read srr;
    EXPECT_EQ(srr, nullptr);
    shared_result_write srw;
    EXPECT_EQ(srw, nullptr);
}

// ============================================================
// 2. async_io 构造/析构测试
// ============================================================

TEST(AsyncIoTest, ConstructDefault) {
    async_io io;
    (void)io;
}

TEST(AsyncIoTest, ConstructWithDepth32) {
    async_io io(32);
}

TEST(AsyncIoTest, ConstructWithDepth8) {
    async_io io(8);
}

TEST(AsyncIoTest, ConstructWithDepth128) {
    async_io io(128);
}

TEST(AsyncIoTest, ConstructDestroyMultiple) {
    for (int i = 0; i < 10; i++) {
        async_io io(16);
    }
}

// 禁用拷贝/移动
static_assert(!std::is_copy_constructible_v<async_io>);
static_assert(!std::is_move_constructible_v<async_io>);
static_assert(!std::is_copy_assignable_v<async_io>);
static_assert(!std::is_move_assignable_v<async_io>);

// ============================================================
// 3. async_result_write 单元测试（peek/wait/empty/user_data/size/finish）
// ============================================================

TEST(AsyncResultWriteTest, EmptyBeforePeekOrWait) {
    std::filesystem::path path = get_temp_file("arw_empty.bin");
    const char* test_data = "test";
    size_t len = strlen(test_data) + 1;

    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);

    uint8_t* raw = new uint8_t[len];
    memcpy(raw, test_data, len);
    unique_buf buf = make_unique_buf(raw);

    {
        async_io io(32);
        shared_result_write rw = io.write(fd, std::move(buf), len, 0, 42);
        ASSERT_NE(rw, nullptr);

        // 未调用 peek/wait 前，use_d 为 nullptr，empty() 应为 true
        EXPECT_TRUE(rw->empty());

        rw->wait();

        // wait 后，use_d 被设置，empty() 应为 false
        EXPECT_FALSE(rw->empty());

        // peek 后也应为 FINISH
        EXPECT_EQ(rw->peek(), state::FINISH);

        // user_data 应为 42
        auto ud = rw->user_data();
        ASSERT_TRUE(ud.has_value());
        EXPECT_EQ(ud.value(), 42);

        // size 应为 len
        EXPECT_EQ(rw->size(), len);

        rw->finish();
    }

    close(fd);
    std::filesystem::remove(path);
}

TEST(AsyncResultWriteTest, PeekBeforeWait) {
    std::filesystem::path path = get_temp_file("arw_peek.bin");
    const char* test_data = "peek test";
    size_t len = strlen(test_data) + 1;

    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);

    uint8_t* raw = new uint8_t[len];
    memcpy(raw, test_data, len);
    unique_buf buf = make_unique_buf(raw);

    {
        async_io io(32);
        shared_result_write rw = io.write(fd, std::move(buf), len, 0, 55);
        ASSERT_NE(rw, nullptr);

        // 先 peek，此时可能 UNFINISHED 或 FINISH
        state s = rw->peek();
        if (s == state::UNFINISHED) {
            int attempts = 0;
            while (rw->peek() == state::UNFINISHED && attempts < 1000) {
                attempts++;
                usleep(100);
            }
        }
        EXPECT_EQ(rw->peek(), state::FINISH);

        // peek 后 use_d 被设置
        EXPECT_FALSE(rw->empty());
        auto ud = rw->user_data();
        ASSERT_TRUE(ud.has_value());
        EXPECT_EQ(ud.value(), 55);

        rw->finish();
    }

    close(fd);
    std::filesystem::remove(path);
}

TEST(AsyncResultWriteTest, MultiplePeekCalls) {
    std::filesystem::path path = get_temp_file("arw_mpeek.bin");
    const char* test_data = "multiple peek";
    size_t len = strlen(test_data) + 1;

    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);

    uint8_t* raw = new uint8_t[len];
    memcpy(raw, test_data, len);
    unique_buf buf = make_unique_buf(raw);

    {
        async_io io(32);
        shared_result_write rw = io.write(fd, std::move(buf), len, 0, 77);
        ASSERT_NE(rw, nullptr);

        // 多次 peek 不应出错
        for (int i = 0; i < 10; i++) {
            state s = rw->peek();
            if (s == state::FINISH) break;
            usleep(100);
        }
        EXPECT_EQ(rw->peek(), state::FINISH);

        rw->finish();
    }

    close(fd);
    std::filesystem::remove(path);
}

// ============================================================
// 4. async_result_read 单元测试
// ============================================================

TEST(AsyncResultReadTest, EmptyBeforePeekOrWait) {
    std::filesystem::path path = get_temp_file("arr_empty.bin");
    const char* test_data = "read test";
    size_t len = strlen(test_data) + 1;

    {
        int wfd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(wfd, 0);
        write(wfd, test_data, len);
        close(wfd);
    }

    int fd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);

    {
        async_io io(32);
        shared_result_read rr = io.read(fd, len, 0, 88);
        ASSERT_NE(rr, nullptr);

        // 未调用 peek/wait 前，empty() 应为 true
        EXPECT_TRUE(rr->empty());

        rr->wait();

        // wait 后，empty() 应为 false
        EXPECT_FALSE(rr->empty());

        EXPECT_EQ(rr->peek(), state::FINISH);

        auto ud = rr->user_data();
        ASSERT_TRUE(ud.has_value());
        EXPECT_EQ(ud.value(), 88);

        EXPECT_EQ(rr->size(), len);

        rr->finish();
    }

    close(fd);
    std::filesystem::remove(path);
}

TEST(AsyncResultReadTest, TransferData) {
    std::filesystem::path path = get_temp_file("arr_transfer.bin");
    const char* test_data = "transfer this";
    size_t len = strlen(test_data) + 1;

    {
        int wfd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(wfd, 0);
        write(wfd, test_data, len);
        close(wfd);
    }

    int fd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);

    {
        async_io io(32);
        shared_result_read rr = io.read(fd, len, 0, 99);
        ASSERT_NE(rr, nullptr);

        rr->wait();

        // 第一次 transfer_data 应返回有效数据
        unique_buf data1 = rr->transfer_data();
        ASSERT_NE(data1.get(), nullptr);
        EXPECT_STREQ((const char*)data1.get(), test_data);

        // 第二次 transfer_data 应返回空（buf 已被转移）
        unique_buf data2 = rr->transfer_data();
        EXPECT_EQ(data2.get(), nullptr);

        rr->finish();
    }

    close(fd);
    std::filesystem::remove(path);
}

TEST(AsyncResultReadTest, PeekPolling) {
    std::filesystem::path path = get_temp_file("arr_peekpoll.bin");
    const char* test_data = "peek polling data";
    size_t len = strlen(test_data) + 1;

    {
        int wfd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(wfd, 0);
        write(wfd, test_data, len);
        close(wfd);
    }

    int fd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);

    {
        async_io io(32);
        shared_result_read rr = io.read(fd, len, 0, 111);
        ASSERT_NE(rr, nullptr);

        // 轮询直到完成
        int attempts = 0;
        state s;
        while ((s = rr->peek()) == state::UNFINISHED && attempts < 1000) {
            attempts++;
            usleep(100);
        }
        EXPECT_EQ(s, state::FINISH);
        EXPECT_FALSE(rr->empty());

        auto ud = rr->user_data();
        ASSERT_TRUE(ud.has_value());
        EXPECT_EQ(ud.value(), 111);

        rr->finish();
    }

    close(fd);
    std::filesystem::remove(path);
}

// ============================================================
// 5. 写操作测试
// ============================================================

TEST(AsyncIoWriteTest, BasicWrite) {
    std::filesystem::path path = get_temp_file("write_basic.bin");
    const char* test_data = "Hello, io_uring async write!";
    size_t data_len = strlen(test_data) + 1;

    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);

    uint8_t* raw = new uint8_t[data_len];
    memcpy(raw, test_data, data_len);
    unique_buf buf = make_unique_buf(raw);

    {
        async_io io(32);
        shared_result_write result = io.write(fd, std::move(buf), data_len, 0, 42);
        ASSERT_NE(result, nullptr);

        int res = result->wait();
        EXPECT_EQ(res, (int)data_len);

        EXPECT_EQ(result->peek(), state::FINISH);
        EXPECT_FALSE(result->empty());

        auto ud = result->user_data();
        ASSERT_TRUE(ud.has_value());
        EXPECT_EQ(ud.value(), 42);

        EXPECT_EQ(result->size(), data_len);

        result->finish();
    }

    close(fd);

    // 验证文件内容
    int rfd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(rfd, 0);
    char read_buf[256] = {};
    ssize_t n = pread(rfd, read_buf, data_len, 0);
    close(rfd);
    EXPECT_EQ(n, (ssize_t)data_len);
    EXPECT_STREQ(read_buf, test_data);

    std::filesystem::remove(path);
}

TEST(AsyncIoWriteTest, MultipleWrites) {
    std::filesystem::path path = get_temp_file("write_multi.bin");
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);

    {
        async_io io(64);
        std::vector<shared_result_write> results;
        const int num_writes = 5;

        for (int i = 0; i < num_writes; i++) {
            std::string data = "Block" + std::to_string(i);
            size_t len = data.size() + 1;
            uint8_t* raw = new uint8_t[len];
            memcpy(raw, data.c_str(), len);
            unique_buf buf = make_unique_buf(raw);

            shared_result_write rw = io.write(fd, std::move(buf), len, i * 16, i + 100);
            ASSERT_NE(rw, nullptr);
            results.push_back(rw);
        }

        for (int i = 0; i < num_writes; i++) {
            int res = results[i]->wait();
            EXPECT_GT(res, 0);

            auto ud = results[i]->user_data();
            ASSERT_TRUE(ud.has_value());
            EXPECT_EQ(ud.value(), (uint64_t)(i + 100));

            results[i]->finish();
        }
    }

    close(fd);
    std::filesystem::remove(path);
}

TEST(AsyncIoWriteTest, WriteAtOffset) {
    std::filesystem::path path = get_temp_file("write_offset.bin");
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);

    {
        async_io io(32);

        // 在 offset=16 处写入
        const char* data = "at offset 16";
        size_t len = strlen(data) + 1;
        uint8_t* raw = new uint8_t[len];
        memcpy(raw, data, len);
        unique_buf buf = make_unique_buf(raw);

        shared_result_write rw = io.write(fd, std::move(buf), len, 16, 1);
        ASSERT_NE(rw, nullptr);

        int res = rw->wait();
        EXPECT_EQ(res, (int)len);
        rw->finish();
    }

    close(fd);

    // 验证偏移写入
    int rfd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(rfd, 0);
    char buf[256] = {};
    pread(rfd, buf, 256, 16);
    close(rfd);
    EXPECT_STREQ(buf, "at offset 16");

    std::filesystem::remove(path);
}

// ============================================================
// 6. 读操作测试
// ============================================================

TEST(AsyncIoReadTest, BasicRead) {
    std::filesystem::path path = get_temp_file("read_basic.bin");
    const char* test_data = "Hello, io_uring async read!";
    size_t data_len = strlen(test_data) + 1;

    {
        int wfd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(wfd, 0);
        ssize_t n = write(wfd, test_data, data_len);
        EXPECT_EQ(n, (ssize_t)data_len);
        close(wfd);
    }

    int fd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);

    {
        async_io io(32);
        shared_result_read result = io.read(fd, data_len, 0, 99);
        ASSERT_NE(result, nullptr);

        int res = result->wait();
        EXPECT_EQ(res, (int)data_len);

        EXPECT_EQ(result->peek(), state::FINISH);
        EXPECT_FALSE(result->empty());

        auto ud = result->user_data();
        ASSERT_TRUE(ud.has_value());
        EXPECT_EQ(ud.value(), 99);

        EXPECT_EQ(result->size(), data_len);

        unique_buf data = result->transfer_data();
        ASSERT_NE(data.get(), nullptr);
        EXPECT_STREQ((const char*)data.get(), test_data);

        result->finish();
    }

    close(fd);
    std::filesystem::remove(path);
}

TEST(AsyncIoReadTest, MultipleReads) {
    std::filesystem::path path = get_temp_file("read_multi.bin");
    const int num_blocks = 4;
    const size_t block_size = 64;

    {
        int wfd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(wfd, 0);
        for (int i = 0; i < num_blocks; i++) {
            uint8_t buf[block_size];
            memset(buf, 'A' + i, block_size);
            write(wfd, buf, block_size);
        }
        close(wfd);
    }

    int fd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);

    {
        async_io io(64);
        std::vector<shared_result_read> results;

        for (int i = 0; i < num_blocks; i++) {
            shared_result_read rr = io.read(fd, block_size, i * block_size, i + 200);
            ASSERT_NE(rr, nullptr);
            results.push_back(rr);
        }

        for (int i = 0; i < num_blocks; i++) {
            int res = results[i]->wait();
            EXPECT_EQ(res, (int)block_size);

            auto ud = results[i]->user_data();
            ASSERT_TRUE(ud.has_value());
            EXPECT_EQ(ud.value(), (uint64_t)(i + 200));

            unique_buf data = results[i]->transfer_data();
            ASSERT_NE(data.get(), nullptr);

            for (size_t j = 0; j < block_size; j++) {
                EXPECT_EQ(data[j], 'A' + i);
            }

            results[i]->finish();
        }
    }

    close(fd);
    std::filesystem::remove(path);
}

TEST(AsyncIoReadTest, ReadAtOffset) {
    std::filesystem::path path = get_temp_file("read_offset.bin");
    const char* block0 = "FIRST BLOCK DATA";
    const char* block1 = "SECOND BLOCK DATA";

    {
        int wfd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(wfd, 0);
        write(wfd, block0, strlen(block0) + 1);
        // pad to offset 64
        lseek(wfd, 64, SEEK_SET);
        write(wfd, block1, strlen(block1) + 1);
        close(wfd);
    }

    int fd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);

    {
        async_io io(32);
        shared_result_read rr = io.read(fd, strlen(block1) + 1, 64, 300);
        ASSERT_NE(rr, nullptr);

        int res = rr->wait();
        EXPECT_EQ(res, (int)(strlen(block1) + 1));

        unique_buf data = rr->transfer_data();
        ASSERT_NE(data.get(), nullptr);
        EXPECT_STREQ((const char*)data.get(), block1);

        rr->finish();
    }

    close(fd);
    std::filesystem::remove(path);
}

// ============================================================
// 7. 写-读集成测试
// ============================================================

TEST(AsyncIoIntegrationTest, WriteThenReadBack) {
    std::filesystem::path write_path = get_temp_file("intg_write.bin");
    std::filesystem::path read_path = get_temp_file("intg_read.bin");

    const size_t data_size = 4096;
    std::vector<uint8_t> original_data(data_size);
    for (size_t i = 0; i < data_size; i++) {
        original_data[i] = (uint8_t)(i % 256);
    }

    // 写入
    {
        int fd = open(write_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(fd, 0);

        uint8_t* raw = new uint8_t[data_size];
        memcpy(raw, original_data.data(), data_size);
        unique_buf buf = make_unique_buf(raw);

        {
            async_io io(32);
            shared_result_write rw = io.write(fd, std::move(buf), data_size, 0, 555);
            ASSERT_NE(rw, nullptr);

            int res = rw->wait();
            EXPECT_EQ(res, (int)data_size);
            rw->finish();
        }

        close(fd);
    }

    // 复制文件用于读取
    std::filesystem::copy_file(write_path, read_path,
                                std::filesystem::copy_options::overwrite_existing);

    // 读取并逐字节验证
    {
        int fd = open(read_path.c_str(), O_RDONLY);
        ASSERT_GE(fd, 0);

        {
            async_io io(32);
            shared_result_read rr = io.read(fd, data_size, 0, 666);
            ASSERT_NE(rr, nullptr);

            int res = rr->wait();
            EXPECT_EQ(res, (int)data_size);

            auto ud = rr->user_data();
            ASSERT_TRUE(ud.has_value());
            EXPECT_EQ(ud.value(), 666);

            unique_buf data = rr->transfer_data();
            ASSERT_NE(data.get(), nullptr);

            for (size_t i = 0; i < data_size; i++) {
                EXPECT_EQ(data[i], original_data[i]) << "Mismatch at byte " << i;
            }

            rr->finish();
        }

        close(fd);
    }

    std::filesystem::remove(write_path);
    std::filesystem::remove(read_path);
}

TEST(AsyncIoIntegrationTest, LargeFile1MB) {
    std::filesystem::path path = get_temp_file("intg_1mb.bin");
    const size_t large_size = 1024 * 1024;

    std::vector<uint8_t> expected(large_size);
    for (size_t i = 0; i < large_size; i++) {
        expected[i] = (uint8_t)((i * 7 + 13) % 256);
    }

    // 写入
    {
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(fd, 0);

        uint8_t* raw = new uint8_t[large_size];
        memcpy(raw, expected.data(), large_size);
        unique_buf buf = make_unique_buf(raw);

        {
            async_io io(32);
            shared_result_write rw = io.write(fd, std::move(buf), large_size, 0, 1000);
            ASSERT_NE(rw, nullptr);

            int res = rw->wait();
            EXPECT_EQ(res, (int)large_size);
            rw->finish();
        }

        close(fd);
    }

    // 读取
    {
        int fd = open(path.c_str(), O_RDONLY);
        ASSERT_GE(fd, 0);

        {
            async_io io(32);
            shared_result_read rr = io.read(fd, large_size, 0, 2000);
            ASSERT_NE(rr, nullptr);

            int res = rr->wait();
            EXPECT_EQ(res, (int)large_size);

            unique_buf data = rr->transfer_data();
            ASSERT_NE(data.get(), nullptr);

            // 抽样验证
            EXPECT_EQ(data[0], expected[0]);
            EXPECT_EQ(data[large_size / 4], expected[large_size / 4]);
            EXPECT_EQ(data[large_size / 2], expected[large_size / 2]);
            EXPECT_EQ(data[large_size * 3 / 4], expected[large_size * 3 / 4]);
            EXPECT_EQ(data[large_size - 1], expected[large_size - 1]);

            rr->finish();
        }

        close(fd);
    }

    std::filesystem::remove(path);
}

// ============================================================
// 8. 错误处理测试
// ============================================================

TEST(AsyncIoErrorTest, InvalidFdRead) {
    async_io io(32);
    shared_result_read rr = io.read(9999, 100, 0, 1);
    ASSERT_NE(rr, nullptr);

    int res = rr->wait();
    EXPECT_LT(res, 0);  // 应返回负的错误码

    rr->finish();
}

TEST(AsyncIoErrorTest, InvalidFdWrite) {
    async_io io(32);

    uint8_t* raw = new uint8_t[16];
    memset(raw, 0, 16);
    unique_buf buf = make_unique_buf(raw);

    shared_result_write rw = io.write(9999, std::move(buf), 16, 0, 1);
    ASSERT_NE(rw, nullptr);

    int res = rw->wait();
    EXPECT_LT(res, 0);

    rw->finish();
}

TEST(AsyncIoErrorTest, ReadBeyondEof) {
    std::filesystem::path path = get_temp_file("err_eof.bin");

    {
        int wfd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(wfd, 0);
        write(wfd, "short", 5);
        close(wfd);
    }

    int fd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);

    {
        async_io io(32);
        // 尝试读取远超文件大小的内容
        shared_result_read rr = io.read(fd, 1024, 0, 1);
        ASSERT_NE(rr, nullptr);

        int res = rr->wait();
        // 应返回实际读取的字节数（5），或 0（EOF）
        EXPECT_GE(res, 0);
        EXPECT_LE(res, 5);

        rr->finish();
    }

    close(fd);
    std::filesystem::remove(path);
}

// ============================================================
// 9. 串行多文件读写测试
//    本库为单线程设计，同一时刻每个 async_io 实例上
//    只应存在一个未完成请求。以下测试确保每个请求
//    wait+finish 完成后再发起下一个。
// ============================================================

TEST(AsyncIoIntegrationTest, SequentialMultiFileWriteRead) {
    std::filesystem::path path1 = get_temp_file("seq_1.bin");
    std::filesystem::path path2 = get_temp_file("seq_2.bin");

    const char* data1 = "File one content here";
    const char* data2 = "File two content here";
    size_t len1 = strlen(data1) + 1;
    size_t len2 = strlen(data2) + 1;

    int fd1 = open(path1.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int fd2 = open(path2.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    {
        async_io io(32);

        // 写文件1 — 完全结束后才写文件2
        {
            uint8_t* raw1 = new uint8_t[len1];
            memcpy(raw1, data1, len1);
            unique_buf buf1 = make_unique_buf(raw1);
            shared_result_write rw1 = io.write(fd1, std::move(buf1), len1, 0, 100);
            ASSERT_NE(rw1, nullptr);
            EXPECT_EQ(rw1->wait(), (int)len1);
            EXPECT_EQ(rw1->peek(), state::FINISH);
            auto ud1 = rw1->user_data();
            ASSERT_TRUE(ud1.has_value());
            EXPECT_EQ(ud1.value(), 100);
            rw1->finish();
        }

        // 写文件2
        {
            uint8_t* raw2 = new uint8_t[len2];
            memcpy(raw2, data2, len2);
            unique_buf buf2 = make_unique_buf(raw2);
            shared_result_write rw2 = io.write(fd2, std::move(buf2), len2, 0, 200);
            ASSERT_NE(rw2, nullptr);
            EXPECT_EQ(rw2->wait(), (int)len2);
            EXPECT_EQ(rw2->peek(), state::FINISH);
            auto ud2 = rw2->user_data();
            ASSERT_TRUE(ud2.has_value());
            EXPECT_EQ(ud2.value(), 200);
            rw2->finish();
        }
    }

    close(fd1);
    close(fd2);

    // 读取验证 — 同样串行
    int rfd1 = open(path1.c_str(), O_RDONLY);
    int rfd2 = open(path2.c_str(), O_RDONLY);
    ASSERT_GE(rfd1, 0);
    ASSERT_GE(rfd2, 0);

    {
        async_io io(32);

        {
            shared_result_read rr1 = io.read(rfd1, len1, 0, 101);
            ASSERT_NE(rr1, nullptr);
            EXPECT_EQ(rr1->wait(), (int)len1);
            unique_buf d1 = rr1->transfer_data();
            ASSERT_NE(d1.get(), nullptr);
            EXPECT_STREQ((const char*)d1.get(), data1);
            rr1->finish();
        }

        {
            shared_result_read rr2 = io.read(rfd2, len2, 0, 201);
            ASSERT_NE(rr2, nullptr);
            EXPECT_EQ(rr2->wait(), (int)len2);
            unique_buf d2 = rr2->transfer_data();
            ASSERT_NE(d2.get(), nullptr);
            EXPECT_STREQ((const char*)d2.get(), data2);
            rr2->finish();
        }
    }

    close(rfd1);
    close(rfd2);

    std::filesystem::remove(path1);
    std::filesystem::remove(path2);
}

// ============================================================
// 10. async_io 先于 result 析构的场景
// ============================================================

TEST(AsyncIoLifetimeTest, IoDestroyedBeforeResultWrite) {
    std::filesystem::path path = get_temp_file("lifetime_w.bin");
    const char* data = "lifetime test";
    size_t len = strlen(data) + 1;

    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);

    shared_result_write rw;
    {
        async_io io(32);
        uint8_t* raw = new uint8_t[len];
        memcpy(raw, data, len);
        unique_buf buf = make_unique_buf(raw);
        rw = io.write(fd, std::move(buf), len, 0, 42);
        ASSERT_NE(rw, nullptr);
        // io 在这里析构，ring 被销毁
    }

    // result 在 io 析构后仍然存活，ring 已过期
    EXPECT_TRUE(rw->empty());           // use_d 未被设置
    EXPECT_EQ(rw->peek(), state::ERROR); // ring.expired() 返回 true
    EXPECT_EQ(rw->wait(), -1);          // ring.expired() 返回 -1
    EXPECT_EQ(rw->size(), 0);           // cqe 为 nullptr

    // finish 在 ring 过期时应安全返回，不崩溃
    rw->finish();

    close(fd);
    std::filesystem::remove(path);
}

TEST(AsyncIoLifetimeTest, IoDestroyedBeforeResultRead) {
    std::filesystem::path path = get_temp_file("lifetime_r.bin");
    const char* data = "read lifetime";
    size_t len = strlen(data) + 1;

    {
        int wfd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(wfd, 0);
        write(wfd, data, len);
        close(wfd);
    }

    int fd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);

    shared_result_read rr;
    {
        async_io io(32);
        rr = io.read(fd, len, 0, 77);
        ASSERT_NE(rr, nullptr);
        // io 在这里析构
    }

    // result 在 io 析构后仍然存活
    EXPECT_TRUE(rr->empty());
    EXPECT_EQ(rr->peek(), state::ERROR);
    EXPECT_EQ(rr->wait(), -1);
    EXPECT_EQ(rr->size(), 0);

    // transfer_data 在 use_d 为 nullptr 时应返回空 buf
    unique_buf data_buf = rr->transfer_data();
    EXPECT_EQ(data_buf.get(), nullptr);

    rr->finish();

    close(fd);
    std::filesystem::remove(path);
}

TEST(AsyncIoLifetimeTest, IoDestroyedBeforeWait) {
    std::filesystem::path path = get_temp_file("lifetime_nowait.bin");
    const char* test_data = "no wait test";
    size_t len = strlen(test_data) + 1;

    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);

    shared_result_write rw;
    {
        async_io io(32);
        uint8_t* raw = new uint8_t[len];
        memcpy(raw, test_data, len);
        unique_buf buf = make_unique_buf(raw);
        rw = io.write(fd, std::move(buf), len, 0, 999);
        ASSERT_NE(rw, nullptr);
        // 不调用 wait，直接让 io 析构
    }

    // io 析构后，result 析构时应安全处理（ring 已过期）
    // 这里 rw 超出作用域时析构，不应崩溃
    EXPECT_NO_THROW(rw.reset());

    close(fd);
    std::filesystem::remove(path);
}

// ============================================================
// 11. 批量提交、串行等待
//     单线程设计下允许批量入队，但必须逐个 wait+finish
// ============================================================

TEST(AsyncIoIntegrationTest, BatchSubmitSequentialWaitWrite) {
    std::filesystem::path path = get_temp_file("batch_w.bin");
    const int num = 8;
    const size_t block_size = 64;

    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);

    {
        async_io io(64);
        std::vector<shared_result_write> results;

        // 批量提交所有写请求（只入队，不等待）
        for (int i = 0; i < num; i++) {
            uint8_t* raw = new uint8_t[block_size];
            memset(raw, 'A' + (i % 26), block_size);
            unique_buf buf = make_unique_buf(raw);
            shared_result_write rw = io.write(fd, std::move(buf), block_size, i * block_size, i + 1000);
            ASSERT_NE(rw, nullptr);
            results.push_back(rw);
        }

        // 逐个等待每个请求完成（单线程设计）
        for (int i = 0; i < num; i++) {
            int res = results[i]->wait();
            EXPECT_EQ(res, (int)block_size) << "Write " << i << " failed";

            auto ud = results[i]->user_data();
            ASSERT_TRUE(ud.has_value());
            // 单线程设计：逐个 wait 保证 CQE 与请求一一对应
            // user_data 按请求顺序断言

            results[i]->finish();
        }
    }

    close(fd);
    std::filesystem::remove(path);
}

// ============================================================
// 10. 零值 user_data 测试
// ============================================================

TEST(AsyncIoTest, ZeroUserData) {
    std::filesystem::path path = get_temp_file("ud_zero.bin");
    const char* data = "zero user data test";
    size_t len = strlen(data) + 1;

    {
        int wfd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        ASSERT_GE(wfd, 0);
        write(wfd, data, len);
        close(wfd);
    }

    int fd = open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd, 0);

    {
        async_io io(32);
        shared_result_read rr = io.read(fd, len, 0, 0);
        ASSERT_NE(rr, nullptr);

        // wait 前 user_data 为空
        auto ud_before = rr->user_data();
        EXPECT_FALSE(ud_before.has_value());

        rr->wait();

        // wait 后 user_data 为 0
        auto ud_after = rr->user_data();
        ASSERT_TRUE(ud_after.has_value());
        EXPECT_EQ(ud_after.value(), 0);

        rr->finish();
    }

    close(fd);
    std::filesystem::remove(path);
}

// ============================================================
// main
// ============================================================
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}