# 回调函数

## 同步回调

同步回调指函数在执行过程中直接调用传入的回调函数，回调执行完成后，原函数才继续往下执行。

示例文件：`sync_callback.cpp`

构建后运行：

```bash
./sync-callback
```
## 异步回调

异步回调指函数发起任务后先返回，任务在后台执行，完成后再调用传入的回调函数。

示例文件：`async_callback.cpp`

构建后运行：

```bash
./async-callback
```
