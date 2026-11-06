# 设计决策

记录实现过程中的关键取舍。每条包含：选项、当前选择、理由与代价、状态。

## 1. `AddStep` 的 fn 签名

- 选项：`std::function<void()>` / `std::function<Result()>` / 模板
- 选择：`std::function<void()>`，失败通过**抛异常**表达
- 理由：接口最简、与 `std::function` 兼容、业务可直接用 lambda 捕获上下文
- 代价：失败信息只能靠 `Error.message`；若将来需要结构化返回，需要改契约
- 状态：已定

## 2. 回调在哪个线程触发

- 选项：worker 内直接调 / 专用通知线程
- 选择：`Run()` 在调用线程；`RunAsync()` 在 worker 线程直接调
- 理由：延迟低、无需额外队列；符合“回调短小非阻塞”的使用约定
- 代价：慢回调会拖住 worker；业务回调必须线程安全；GUI 需自行转发到界面线程
- 状态：已定（阶段 2）

## 3. 同一 Step 的通知保序

- 选项：锁内投递 / 序号 + 单通知线程 / 单任务内顺序发
- 选择：同一个 `runStep` 内依次发 `Pending → Running → 终态`；上游 `Succeeded` 在提交下游前发出
- 理由：无需额外序号与通知线程，顺序天然成立
- 代价：通知分散在各 worker 中，调试时链路不如单线程直观
- 状态：已定（阶段 2）

## 4. `Run` / `RunAsync` 的可重入性

- 选项：重入抛异常 / 返回 false / 排队
- 选择：`running_` 在互斥量保护下抢占；已有执行未收敛时返回 `false`
- 理由：不阻塞、不抛异常，调用方容易处理
- 代价：调用方必须检查返回值，否则会静默不执行
- 状态：已定

## 5. `Cancel` 对 Running 的语义

- 选项：强制中断 / 允许完成
- 选择：已 `Running` 的步骤允许完成；未开始的按依赖状态分类
  - 就绪（上游全部成功）→ `Cancelled`
  - 被运行中/失败/取消的上游挡住 → `Skipped`
- 理由：步骤函数是用户代码，强行中断只能靠协作式检查或线程终止，不安全；区分就绪与阻塞，符合“上游未成功 → 下游跳过”的语义
- 代价：需要在锁内遍历节点并判断入度，取消路径稍复杂
- 状态：同步与异步均已实现（阶段 3）

## 6. 回调抛异常

- 选项：转 `Failed` / 吞掉 / 终止进程
- 选择：捕获后把该 Step 转为 `Failed`（`Error.code = 3`），异常不穿透 worker
- 理由：训练文档要求“回调异常 → Step Failed”；吞掉会让调用方误以为成功
- 代价：如果 `Succeeded` 通知本身抛异常，会再补一个 `Failed`，调用方可能看到两个终态事件
- 状态：已实现（阶段 3）

## 7. `SetListener(nullptr)` 与正在执行的回调

- 选项：`shared_ptr<Listener>` 快照 / 标记 + 引用计数
- 选择：`std::shared_ptr<const Listener>`；`notify` 在锁内取一份快照，锁外调用
- 理由：注销时即使回调正在执行，函数对象也不会被销毁；`SetListener` 返回后不再接受新的通知
- 代价：每次通知多一次 `shared_ptr` 拷贝与一把短锁
- 状态：已实现（阶段 3）

## 8. 析构与回调的生命周期

- 选择：析构函数执行 `Cancel() + Wait()`；线程池声明为最后一个成员，随后析构并 `join`
- 理由：保证析构返回前不会再触发新回调，也没有 worker 访问已销毁的成员
- 代价：析构会阻塞到运行中的步骤结束
- 限制：在回调里析构 `Pipeline` 会因等待自身而卡住
- 状态：已实现（阶段 3）

## 9. 抽象接口 + Pimpl

- 选择：对外只暴露抽象类 `Pipeline`；`PipelineImpl` 私有头里只有 `struct Impl; std::unique_ptr<Impl>`
- 实现：节点、容器、锁、条件变量、Boost 线程池全部定义在 `pipeline_impl.cpp` 的 `struct PipelineImpl::Impl` 中
- 理由：抽象接口解决多实现/契约隔离；Pimpl 解决内部演进不污染头文件
- 代价：多一层间接与一次堆分配；调试时多看一层
- 状态：已实现（阶段 4）

## 10. 执行期禁止改图

- 选择：`building` 标志；`Run`/`RunAsync` 开始后 `AddStep`/`AddDependency` 抛 `std::logic_error`
- 理由：执行期间修改 `nodes` 会导致迭代器失效与数据竞争；运行时检测比文档约定可靠
- 代价：需要在构图与执行之间明确分界
- 状态：已定（阶段 1）

## 11. `CreateForTest()` / Mock

- 选项：独立 `MockPipeline` / 复用 `PipelineImpl` + 测试模式
- 选择：复用同一实现，`testMode` 下 `RunAsync()` 直接调用同步 `Run()`，不创建线程池
- 理由：建图、调度、错误传播、取消只有一套代码，Mock 与真实实现不会行为分叉
- 代价：`Impl` 内多一个模式判断
- 状态：已实现（阶段 4）

## 12. C API 的边界

- 选择：`extern "C"` + 不透明句柄 `imgpipe_handle*`；C 层不抛异常，失败用返回码/回调错误表达
- 实现：`c_api.cpp` 内部包住 C++ `Pipeline`，`AddStep` 把 C 函数指针 + `user_data` 包成 `std::function`
- 理由：跨语言/跨编译器稳定；异常不能跨越 C ABI；句柄让 C 侧完全看不到实现
- 代价：需要手动管理句柄生命周期；错误信息只能通过回调即时读取
- 导出：Windows 下用 `__declspec(dllexport)`，只导出 `imgpipe_*`
- 状态：已实现（阶段 5）

## 附：线程池选型

- 选择：Boost.Asio `boost::asio::thread_pool`（vcpkg，header-only），懒创建，大小取 `Config::maxConcurrency`
- 理由：无需自维护线程池；header-only，无额外运行库
- 代价：`join()` 之后池不可复用，`Wait`/`Cancel` 仍需自己用计数与条件变量实现；阶段 3 若取消语义受限，可改为自写线程池
