# Changelog

## Unreleased - 2026-08-10

### Added

- 实现 `Status`、`Result<T>`、Tensor 数据类型、设备、shape 和规格约束。
- 实现 `TensorBuffer`、`TensorBufferPool` 与支持 stride、offset、subview 的 `Tensor` 视图。
- 实现 move-only `Frame`、`FrameMetadata` 和固定容量相机 `FrameBufferPool`。
- 实现 `BusinessFramePool`，为裁剪、复制和热力图绘制提供可复用业务图像缓冲区。
- 实现 move-only `PipelinePacket`，在前处理、推理和后处理阶段间显式移交图像所有权。
- 增加相机帧与业务帧的独立释放策略，默认在图像准备完成后释放相机帧，在后处理完成后释放业务帧。
- 增加 Tensor、缓冲池、Pipeline 图像生命周期、池耗尽和非法布局测试。

### Changed

- 将测试目标定义下放至 `Runtime/tests/CMakeLists.txt`，并按模块组织测试源码。
- 将通用固定槽位管理从相机模块抽取到 `core::TensorBufferPool`。
- `Frame` 改为不可复制、可移动类型，移动后源对象进入明确的空状态。

### Validation

- MinGW Debug 构建通过。
- 29 个单元测试全部通过。
