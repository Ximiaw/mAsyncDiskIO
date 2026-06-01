# mAsyncDiskIO

一个基于 io_uring 的单线程异步磁盘 I/O 薄封装。

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

## 许可证

MIT
