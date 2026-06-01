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

## 注意

### async_io
- `read()/write()`调用时，库认为期望执行对应动作，因此若一切顺利会直接提交，不对`prep_count()`进行计数，若提交失败则会`prep_count()`加1,并设置`submit_error`表示提交失败，而已准备的`sqe`可能随着任意一次提交从而提交。
- `result_count()`表示应拿多少个结果对象，如果强行获取更多的结果对象，则在调用结果对象的`wait()`时可能导致无法中止的阻塞，因此不建议通过`get_result(true)`强制获取。
- 在`sqe`准备后，若没有提交在大于当前`cqe`数量的结果对象调用`wait()`时可能导致无法中止的阻塞，而提交是否成功则在调用`read()/write()/submit()`后通过`submit_err()`检查。(新的api使得此条已失效，在未提交前无法通过`get_result()`获取结果对象)
- `queue_size()`和`result_count()`语义相似但不同，前者表示提交和准备的数据有几条，使得他减少只有结果对象通过`peek()/wait()`消费后才会减去消费条目，后者表示还有几个结果对象应拿，只有通过`get_result()`才会减少计数。
- `prep_count()`返回已准备未提交的任务数量，当调用`read()/write()/submit()`提交后会增加`result_count()`的计数。

### async_result
- 在未通过`peek()/wait()`前其绝大多数方法无效，只会返回默认值/空值。
- 读和写的情况下api语义不同需要注意。
- `is_valid`在初始化时会检查`map`和`ring`是否有效，并以此设置`is_valid`。
- 在调用`transfer_data()`后`is_valid`会被设置为`false`，`us_d`内的`buf`会被设置为`nullptr`，这会使得大多数api失效，但不影响`user_data()`,`size()`和`rw()`。
- 若创建该对象的`async_io`失效，则所有由这个`async_io`创建的结果对象全部失效。

## 许可证

MIT
