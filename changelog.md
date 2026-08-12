# Changelog

## Unreleased - 2026-08-11

### Added

- 实现 `Status`、`Result<T>`、Tensor 数据类型、设备、shape 和规格约束。
- 实现 `TensorBuffer`、`TensorBufferPool` 与支持 stride、offset、subview 的 `Tensor` 视图。
- 实现 move-only `Frame`、`FrameMetadata` 和固定容量相机 `FrameBufferPool`。
- 实现 `BusinessFramePool`，为裁剪、复制和热力图绘制提供可复用业务图像缓冲区。
- 实现 move-only `PipelinePacket`，在前处理、推理和后处理阶段间显式移交图像所有权。
- 增加相机帧与业务帧的独立释放策略，默认在图像准备完成后释放相机帧，在后处理完成后释放业务帧。
- 增加 Tensor、缓冲池、Pipeline 图像生命周期、池耗尽和非法布局测试。
- 实现异步目录 `FileSource`，支持扩展名过滤、递归扫描、字典序/修改时间排序、循环播放和可配置帧间隔。
- 使用 OpenCV 解码 Gray8、Gray16、Float32Gray、BGR8 和 BGRA8 图像，通过共享 `cv::Mat` 生命周期的 `TensorBuffer` 零额外拷贝创建 `Frame`。
- 增加目录无匹配图像校验，以及真实 PPM 解码、像素格式、序号和 Frame 内存生命周期测试。
- 增加 `visonRuntime` 库 target，并从源码最小集成 OpenCV `core`、`imgproc` 和 `imgcodecs` 模块。
- 实现模板化 `PipelineExecutor`、`TaskHandle` 和任务状态机，支持固定容量并发提交、shared future、独立回调交付、FIFO 顺序、取消及平滑/立即停止。
- 增加 Executor 队列满载、顺序交付、异常隔离、排队取消和立即停止测试。
- 增加类型状态化 `PreprocessChainBuilder`，支持按顺序组合前处理节点，并在编译期禁止错误节点顺序和第二个物化节点。
- 增加 `FusedImageToTensorNode`，将 Gray8/Bgr8/Bgra8 图像双线性缩放并直接写入池化 Float32 NCHW Tensor，完成后释放相机 Frame。
- 增加 `TensorNormalizeNode`，在同一池化 Tensor 上原地执行逐通道 scale、mean 和 standard deviation 归一化。
- 增加前处理数值、相机 Frame 释放、TensorBufferPool lease 归还和节点组合约束测试。

### Changed

- 将 `preprocess` 和 `postprocess` 统一视为单词：命名空间与路径使用全小写，类型使用 `Preprocess`、`Postprocess` 词形。
- 将测试目标定义下放至 `Runtime/tests/CMakeLists.txt`，并按模块组织测试源码。
- 将通用固定槽位管理从相机模块抽取到 `core::TensorBufferPool`。
- `Frame` 改为不可复制、可移动类型，移动后源对象进入明确的空状态。
- 明确图像源的公共输出为 `Frame`：文件源内部管理解码存储，前处理阶段再将 Frame 转换为模型 Tensor。
- 明确当前 Executor 在统一 `IVisionPipeline::run()` 上调度；三阶段线程与 SPSC 队列待统一阶段化 Pipeline contract 落地后实现。

### Validation

- MinGW Debug 构建通过。
- 原有 29 个单元测试全部通过。
- `fileSourceTest` 目标使用 MinGW Debug 构建通过，新增 2 个 FileSource 单元测试全部通过；当前共 31 个测试。
- `pipelineExecutorTest` 目标使用 MinGW Debug 构建通过，新增 5 个 Executor 单元测试全部通过；CTest 当前发现 44 个测试。
- 可组合前处理、旧兼容前处理器和 Pipeline 相关目标使用 MinGW Debug 构建通过；CTest 当前发现并通过全部 47 个测试。
