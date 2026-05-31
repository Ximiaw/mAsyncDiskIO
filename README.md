# mAsyncDiskIO

一个基于 io_uring 的单线程异步磁盘 I/O 库。

## 依赖

- Linux 内核（≥ 5.1，使用 `IORING_SETUP_SQPOLL` 时需更高版本）
- liburing
- CMake ≥ 3.10
- C++20 编译器(仅测试了GCC13)

## 构建

```bash
mkdir build && cd build
cmake ..
make
```

## 核心接口

### async_io —— 入口类

| 方法 | 说明 |
|------|------|
| `read(fd, size, user_data, offset)` | 准备并提交读请求，返回 `unique_result` |
| `write(fd, buf, size, user_data, offset)` | 准备并提交写请求，返回 `unique_result` |
| `prep_read(...)` / `prep_write(...)` | 仅准备 SQE，需手动调用 `submit()` |
| `get_result()` | 获取一个结果对象，配合 `prep_*` 使用 |
| `submit()` | 提交队列中已准备的请求 |
| `queue_size()` / `result_count()` | 当前队列 / 待取结果数量 |

### async_result —— 结果对象

| 方法 | 说明 |
|------|------|
| `peek()` | 非阻塞查询完成状态 |
| `wait()` | 阻塞等待直到完成 |
| `transfer_data()` | 转移缓冲区所有权（仅读操作有效） |
| `user_data()` | 取回用户自定义标识 |
| `size()` | 实际读写字节数（负值为错误码） |

## 注意事项

- 仅在 Linux 平台可用。
- **每个结果对象必须消费**：`async_result` 必须通过 `peek()` 或 `wait()` 完成 CQE 回收，否则造成资源泄漏。
- `async_io` 实例须在全部 `async_result` 消费前保持存活。
- `transfer_data()` 仅可调用一次，调用后结果对象失效。
- 传入 `user_data` 的裸指针需自行保证生命周期。
- `read`/`write` 提交失败返回 `nullptr`，可检查 `submit_err()`。
- `prep_*` 模式需自行管理 `submit()` 与 `get_result()` 的配对。

## 许可证

MIT
