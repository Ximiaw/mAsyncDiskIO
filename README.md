# mAsyncDiskIO

一个基于 io_uring 的单线程异步磁盘 I/O 库。

## 特性

- 基于 Linux io_uring，使用 `IORING_SETUP_SQPOLL` 内核轮询模式
- 单线程设计
- 不保证 I/O 执行次序
- RAII 管理资源（缓冲区、io_uring 队列等）
- C++20

## 依赖

- Linux 内核 5.1+（支持 io_uring）
- [liburing](https://github.com/axboe/liburing)
- CMake 3.10+
- C++20 编译器(仅测试了GCC13)

## 使用

### 基本读写

#### CMakeLists.txt

```CMake
cmake_minimum_required(VERSION 3.10)
project(Main)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(mAsyncDiskIO)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE mAsyncDiskIO)
```
#### main.cpp
```cpp
#include "async_io.h"
#include <fcntl.h>
#include <iostream>
#include <memory.h>

using namespace mAsyncDiskIO;

int main() {
    async_io io(64);// 队列深度 64，启用 SQPOLL
    int fd = open("/tmp/test.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);

    // 异步写：user_data=1 作为自定义标识
    uint8_t* buf = new uint8_t[32];
    memcpy(buf, "hello io_uring", 15);
    auto w = io.write(fd, make_unique_buf(buf), 15, 0, 1);
    // peek 轮询，非阻塞检查完成状态
    while (w->peek() == state::UNFINISHED) {}
    w->finish();                         // 标记 CQE 已消费

    // 异步读：user_data=2
    auto r = io.read(fd, 15, 0, 2);
    while (r->peek() == state::UNFINISHED) {}
    auto data = r->transfer_data();      // 转移 buffer 所有权
    std::cout << (char*)data.get() << "\n"; // "hello io_uring"
    r->finish();

    close(fd);
    return 0;
}
```

### 非阻塞轮询

```cpp
shared_result_read rr = io.read(fd, 1024, 0, 42);

while (rr->peek() == state::UNFINISHED) {
    // 做其他事...
}

auto ud = rr->user_data();  // 获取用户数据（optional<uint64_t>）
auto sz = rr->size();       // 实际读/写的字节数
rr->finish();
```

## API

### data_struct

| 方法 | 说明 |
|------|------|
| `make_unique_buf(ptr)` | 工厂函数，从裸指针构造 `unique_buf`。传 `nullptr` 构造空缓冲区 |

- `unique_buf` 不可拷贝，只可移动。通过 `make_unique_buf` 创建，析构时自动 `delete[]` 底层数组。

### async_io

| 方法 | 说明 |
|------|------|
| `async_io(size_t deep)` | 构造。初始化 io_uring 队列，深度为 `deep` |
| `read(fd, size, offset, user_data)` | 提交异步读请求，返回 `shared_result_read`。队列满时返回 `nullptr` |
| `write(fd, buf, size, offset, user_data)` | 提交异步写请求，转移缓冲区所有权，返回 `shared_result_write`。队列满时返回 `nullptr` |

- `async_io` 不可拷贝、不可移动。
- 强调"单线程调用"（不要多线程并发提交），但允许队列深度级别的多请求 inflight。

### async_result_read / async_result_write

| 方法 | 说明 |
|------|------|
| `empty()` | 请求尚未完成时为 `true` |
| `peek()` | 非阻塞检查状态。返回 `FINISH` 时自动释放 CQE；返回 `UNFINISHED` / `ERROR` 时不释放 |
| `wait()` | 阻塞等待完成，返回 io_uring 结果（字节数或负的错误码），完成后自动释放 CQE |
| `user_data()` | 返回提交时传入的用户数据（`optional<uint64_t>`） |
| `size()` | 返回实际读/写的字节数（`cqe->res`） |
| `transfer_data()` | **仅 read。** 转移缓冲区所有权给调用方 |

- 结果对象不可拷贝，可移动。
- `peek()` / `wait()` 在检测到完成后会自动调用 `io_uring_cqe_seen` 释放 CQE，不需要手动清理。
- 如果 `async_io` 实例先于结果对象析构，`peek()` 返回 `ERROR`，`wait()` 返回 `-1`，行为安全。
- `wait()`会触发系统调用

## 注意事项
1. **运行权限**：可能需要 **root 权限** 或 `CAP_SYS_ADMIN` capability（`IORING_SETUP_SQPOLL` 要求）。
2. **不保证执行次序**：多个 I/O 请求的完成顺序取决于磁盘调度，与提交顺序无关。
3. **单线程**：`async_io` 为单线程设计。不要从多个线程并发调用同一个实例的 `read` / `write`。
4. **队列深度**：提交队列满时，`read` / `write` 返回空指针。
5. **写操作缓冲区**：`write` 通过 `unique_buf&&` 转移缓冲区所有权，调用后不应再访问原缓冲区。
6. **读操作缓冲区**：通过 `transfer_data()` 获取结果数据，只能调用一次。
7. **user_data 生命周期**：如果 `user_data` 传入的是指针，其指向对象的生命周期由调用方保证。
8. **串行使用**：每次仅允许一个`async_result_read/async_result_write`获取处理，在持用`cqe`的结果对象调用前`finish()`前，其它结果对象`peek()/wait()`会获取正在处理的cqe，导致未定义行为。

## 测试

```bash
mkdir build
cd build
cmake .. -DBUILD_TESTS=ON
cmake --build .
ctest --output-on-failure
```

测试覆盖：基础类型、构造析构、读写操作、偏移读写、多请求、错误处理、大文件（1MB）、生命周期安全。

## 文件结构

```
├── include/
│   ├── async_io.h/.cpp      # 核心类
│   ├── result_read.h/.cpp   # 读结果
│   ├── result_write.h/.cpp  # 写结果
│   ├── result_base.h        # 结果基类
│   └── data_struct.h        # 类型定义
├── test_async_io.cpp        # 单元测试
├── CMakeLists.txt           # 顶层
└── src/CMakeLists.txt       # 库
```
