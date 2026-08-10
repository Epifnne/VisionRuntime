# VisonRuntime

面向工业视觉模型部署的 C++20 SDK。

## 当前基础能力

- 后端无关的 `Status`、`Result<T>`、`Tensor`、shape、stride 和设备描述。
- `TensorBuffer` 与固定容量 `TensorBufferPool`，支持共享 lease 和自动回池。
- move-only `Frame`，避免流水线阶段间隐式复制图像。
- 独立的相机 `FrameBufferPool` 与业务 `BusinessFramePool`。
- 相机图像准备完成后释放相机 Buffer，业务图像保留到热力图等后处理完成。

## 图像所有权

当前定时拍照 Pipeline 采用两段缓冲生命周期：

```text
Camera Frame -> crop/copy -> release camera buffer
Business Frame -> infer -> postprocess/heatmap -> release business buffer
```

`PipelinePacket` 只能移动，不能复制。默认配置在裁剪或复制完成后归还相机槽位，并将业务 Frame 的同一内存地址继续移交给推理和后处理。释放阶段可通过 `PipelineOwnershipOptions` 调整。

零拷贝裁剪视图会继续持有相机 Buffer；若需要尽早归还相机槽位，应从 `BusinessFramePool` 获取目标 Frame，并在前处理阶段直接写入。

## 执行模型

运行时同时提供同步 `run()` 和异步 `submit()`。首版异步 Executor 采用单通道分阶段流水线：并发调用的 `submit()` 进入线程安全的有界提交队列；前处理、推理和后处理各由一个专属线程执行，阶段间通过固定容量、FIFO 的有界 SPSC 队列连接，结果由独立 `CompletionDispatcher` 完成 future 并触发业务回调。

任务按提交顺序交付。入口满载时 `submit()` 返回 `QueueFull`，内部队列通过阻塞上游阶段传播背压，已经接受的任务不会静默丢失。首版不实现多 Worker 通道、结果重排或完成即交付；这些能力只在 benchmark 证明单通道吞吐不足后扩展。详细职责与停止、取消语义见 [Docs/architecture.md](Docs/architecture.md#37-executor)。

## 构建环境

- Windows
- Qt MinGW-w64 13.1 (`D:/Qt/Tools/mingw1310_64`)
- CMake 3.25 或更高版本
- Ninja

项目统一使用 MinGW，不使用 MSVC 或 vcpkg。第三方依赖直接放入 `Thirdparty/<package>/<version>`，具体规则见 [Thirdparty/README.md](Thirdparty/README.md)。

## 构建与测试

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

所有构建产物写入 `Build`。

当前共有 29 个单元测试，覆盖基础结果类型、Tensor 视图、缓冲池、池耗尽、非法布局和跨 Pipeline 阶段的图像生命周期。
