# imgpipe —— 图片处理流水线 SDK

把「图片处理步骤 + 依赖关系」交给一个 DAG 执行器：业务方声明步骤和依赖、注册进度回调，SDK 负责拓扑顺序、并发执行、失败跳过与取消。

- **不实现图片算法**：解码、缩放、水印、编码仍由业务方的 `Step` 完成。
- **提供的能力**：DAG 建图与校验、同步/异步执行、步骤状态通知、失败传播、取消。

## 目录结构

```
20-01-picture/
├── include/
│   ├── image_pipeline.h        # 对外 C++ 接口
│   └── image_pipeline_c.h      # 对外 C API
├── src/
│   ├── pipeline_impl.h         # 只剩 struct Impl 前向声明
│   ├── pipeline_impl.cpp       # 建图、同步/异步调度、线程池（全部实现细节）
│   └── c_api.cpp               # C API：不透明句柄 + 错误码
├── tests/                      # GTest 用例
├── example/
│   ├── basic.cpp               # 链式六步完整示例
│   ├── cancel.cpp              # 回调内取消 + 失败传播
│   └── python_demo.py          # Python ctypes 示例
├── doc/
│   ├── 使用指南.md
│   ├── 接口异步化调研.md
│   ├── design.md
│   └── 训练大纲.md
└── CMakeLists.txt
```

## 构建

依赖 Boost.Asio（vcpkg 的 `boost-asio` / `boost-system`，header-only）与 GTest（测试）。

```powershell
cmake --preset msvc-debug
cmake --build build\msvc-debug --target 20-01-picture imgpipe_c imgpipe_tests `
  imgpipe-example-basic imgpipe-example-cancel
```

Python ctypes 示例：

```powershell
python src/20-sdk/20-01-picture/example/python_demo.py `
  build/msvc-debug/src/20-sdk/20-01-picture/bin/imgpipe.dll
```

> MinGW 预设可用 `mingw-debug`；Boost 组件需已通过 vcpkg 安装。

## 快速开始

```cpp
#include "image_pipeline.h"

int main() {
    using namespace imgpipe;
    Config cfg;
    cfg.maxConcurrency = 2;
    auto pipeline = Pipeline::Create(cfg);

    StepId a = pipeline->AddStep("decode", [] { /* ... */ });
    StepId b = pipeline->AddStep("encode", [] { /* ... */ });
    pipeline->AddDependency(b, a);

    pipeline->SetListener([](StepId id, StepState st, const Error& err) {
        // 只做观察；RunAsync 下回调在 worker 线程
    });

    pipeline->RunAsync();   // 立即返回
    pipeline->Wait();       // 等本轮收敛
}
```

同步场景直接调用 `pipeline->Run()`（阻塞，回调在调用线程）。

## 测试

```powershell
ctest --test-dir build\msvc-debug --output-on-failure -R "Smoke|Topology|Error|Lifecycle|Async"
```

## 文档

- [使用指南](doc/使用指南.md)：接口、线程模型、同步 vs 异步、常见问题
- [接口异步化调研](doc/接口异步化调研.md)：业界异步实现范式与选型
- [设计决策](doc/design.md)：关键取舍记录
- [训练大纲](doc/训练大纲.md)：分阶段目标与验收标准

## 状态

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| 1 | 建图、拓扑排序、同步 `Run`、环检测 | 完成 |
| 2 | 线程池、异步 `RunAsync`、`Wait` | 完成 |
| 3 | 取消、生命周期、并发压测 | 完成（ASan 全量通过；TSan 需 Clang，当前环境不可用） |
| 4 | Pimpl 拆分、`CreateForTest` / Mock | 完成 |
| 5 | C API、Python ctypes demo | 完成 |
