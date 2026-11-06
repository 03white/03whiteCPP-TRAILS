# imgpipe Qt Demo

用 Qt Widgets 调用 `20-01-picture` 的图片流水线：选图 → 加载 → 转换 → 缩放 → 水印 → 编码 → 保存。

## 运行

本机 Qt 只有 **release** 二进制（`D:/Qt/6.5.3-msvc-x64`），所以用 `msvc-release` 构建：

```powershell
cmake --preset msvc-release -DCMAKE_PREFIX_PATH=D:/vcpkg/installed/x64-windows
cmake --build build\msvc-release --target picture_qt_demo
build\msvc-release\src\20-sdk\20-02-picture-qt\picture_qt_demo.exe
```

`windeployqt` 会在构建后把 Qt 运行库和插件（`platforms/qwindows.dll`、`imageformats/*`）拷到 exe 旁边，可以直接双击运行。

## 界面

- **选择图片**：选一张本地图片，右侧显示预览
- **开始**：跑 6 步链式流水线
- **取消**：运行中可点；运行中的步骤允许跑完，没开始的会转 `Cancelled` / `Skipped`
- **步骤表**：实时显示 `等待 / 执行中 / 成功 / 失败 / 跳过 / 已取消`
- **进度条**：按到达终态的步骤数推进
- **日志**：每个步骤的状态变化和错误信息
- 成功后右侧显示处理结果，输出文件在输入图片同目录：

```
<input>_imgpipe.png
```

## 线程模型

SDK 的回调（`RunAsync`）在库的 worker 线程触发，Qt 控件只能在 GUI 线程访问，所以：

```cpp
pipeline_->SetListener([this, run](StepId id, StepState state, const Error& err) {
    const int code = err.code;
    const QString message = QString::fromUtf8(err.message.c_str());  // 先拷贝
    QMetaObject::invokeMethod(this, [=] {
        onStep(run, id, state, code, message);                       // 回到 GUI 线程
    }, Qt::QueuedConnection);
});
```

要点：

- `err.message` 只在回调期间有效，先转成 `QString`
- `invokeMethod` 带 `this` 作为 context，窗口销毁后排队事件会自动丢弃
- `runId` 用来忽略上一轮残留的通知

## 扩展

- **加步骤**：在 `startRun()` 里继续 `addStep(name, fn)`，再用 `AddDependency(下游, 上游)` 连边
- **并行分支**：例如“缩略图”和“水印”可以并行；注意两条分支不要同时写同一个 `Job::image`，需要各自拷贝或加锁
- **更多格式**：`QImage` 支持 png/jpg/bmp/webp/tiff，Qt 插件已部署
- **耗时统计**：在 `onStep` 里记录 `Running → 终态` 的时间差即可

## 已知限制

- 关闭窗口时，如果步骤还在跑，析构会 `Cancel + Wait`，窗口会稍等片刻
- 目前是单链，没有用并行分支；`maxConcurrency` 默认 2
- 界面只做演示，没有做错误弹窗、历史记录等产品化细节
