# VisonRuntime 项目规划

详细模块边界见 [Docs/architecture.md](Docs/architecture.md)。每个阶段应形成可编译、可测试的纵向增量，不在同一阶段同时扩展多个未知维度。

## M0：工程骨架

- [ ] 确定项目命名、C++ namespace 和公共头文件 include 路径。
- [ ] 编写根 CMake 和 Runtime 子项目 CMake，建立静态库、测试、示例和工具 target。
- [ ] 将 OpenCV、JSON、日志和测试框架下载到 `Thirdparty`，记录版本、来源和校验值。
- [ ] 为 OpenVINO、TensorRT、ONNX Runtime、海康 MVS 定义独立 CMake 开关和 imported targets。
- [ ] 建立 MinGW 警告级别、格式化、静态分析和基础 CI。
- [ ] 将 `preProcess`、`postProcess` 等目录名统一为最终命名风格，避免公开 include 路径后再改名。

验收：无任何推理 SDK 时 core target 和单元测试可以独立配置、编译并运行。

## M1：Core 与 Vision 基础类型

- [x] 实现稳定错误码、`Status` 和 `Result<T>`。
- [x] 实现 `DataType`、`Device`、`TensorShape`、`TensorBuffer` 和 `Tensor` 视图。
- [x] 明确 Tensor 的 shape、stride、offset、连续性、设备位置和所有权规则。
- [x] 实现 move-only `Frame`、`FrameMetadata` 和图像缓冲生命周期约定。
- [x] 实现通用 `TensorBufferPool`、相机 `FrameBufferPool` 和业务 `BusinessFramePool`。
- [x] 实现相机帧在图像准备后释放、业务帧在后处理后释放的可配置所有权策略。
- [ ] 实现 `TransformContext` 及坐标、尺寸和热力图的反向映射。
- [ ] 定义分类、检测、异常、OCR、关键点强类型结果。
- [ ] 为 Result、Tensor、Frame、缓冲池、变换映射和结果类型编写单元测试（Result、Tensor、Frame 与缓冲池已完成）。

验收：core 不依赖 OpenCV 和推理后端；vision 只依赖 core 与 OpenCV。

## M2：首个纵向功能——OpenVINO 异常检测

- [ ] 定义 `IPreprocessor`、`IInferenceBackend`、`IPostprocessor` 和线性 Pipeline 接口。
- [ ] 实现 resize、颜色转换、归一化和 NCHW 前处理节点。
- [ ] 实现 OpenVINO ONNX 加载、CPU 编译和同步推理。
- [ ] 实现异常分数、热力图还原、阈值判断和缺陷区域提取。
- [ ] 实现 `AnomalyResult` 示例程序：读取本地图像并输出结果。
- [ ] 使用同一输入与 Python 参考实现对比数值误差。
- [ ] 记录前处理、推理、后处理和总耗时。

验收：固定 NCHW、batch 1 的异常模型可从 ONNX 完成端到端推理，结果在约定容差内与 Python 一致。

## M3：模型包与配置

- [ ] 定义 `manifest.json` 与 `deployment.json` 的版本化 JSON Schema。
- [ ] 实现 `ModelManifest`、`DeploymentConfig` 和严格配置校验。
- [ ] 实现模型包相对路径解析、资源文件加载和错误上下文。
- [ ] 实现 `RuntimeFactory`，按配置组装前处理、后端、后处理和 Runtime。
- [ ] 实现 ONNX SHA-256、设备、精度、后端版本和构建参数组成的缓存键。
- [ ] 支持 `BuildIfMissing` 与 `CacheOnly` 两种模型准备策略。
- [ ] 编写模型包检查工具和错误配置测试。

验收：训练人员只需提供模型包和节点组合代码，部署配置可以在不修改模型包的情况下切换设备策略。

## M4：异步 Executor

- [ ] 实现支持并发 `submit()` 的固定容量有界提交队列。
- [ ] 实现有界 SPSC 队列，连接前处理、推理、后处理和结果交付阶段。
- [ ] 实现 `TaskHandle`、future、回调和任务状态机。
- [ ] 实现单通道 `PipelineRunner`：前处理、推理和后处理各使用一个专属线程，并配置独立 `CompletionDispatcher`。
- [ ] 入口队列满时返回 `QueueFull`；内部队列满时阻塞上游阶段并传播背压，保证任务不静默丢失。
- [ ] 保证各阶段 FIFO 和按提交顺序交付；多通道及完成即交付留作后续扩展。
- [ ] 定义平滑停止、立即停止和取消语义；未开始任务可取消，执行中任务只记录取消请求并停止交付结果。
- [ ] 保证业务回调异常不终止工作线程。
- [ ] 增加阶段并行、队列满载、背压、停止、取消、结果顺序、回调异常和图像生命周期测试。

验收：单通道流水线持续异步提交时没有悬空图像、任务泄漏和未捕获异常，任务严格有序，结果可通过 task ID 与工件稳定关联。

## M5：ONNX Runtime 后端

- [ ] 实现 ORT CPU Session、输入输出绑定和同步推理。
- [ ] 复用 OpenVINO 后端 contract tests。
- [ ] 使用 ORT 作为 ONNX 参考执行后端比较 OpenVINO 输出。
- [ ] 增加 provider、线程数和图优化级别配置。
- [ ] 明确不支持算子、动态 shape 和输出不匹配时的错误信息。

验收：同一模型包仅修改部署配置即可在 OpenVINO 和 ORT 间切换。

## M6：TensorRT 后端与缓存工具

- [ ] 实现 ONNX Parser、Builder、Engine 序列化与加载。
- [ ] 支持 FP32 和 FP16，暂不加入 INT8 校准。
- [ ] 将 GPU 型号、CUDA/TensorRT 版本、精度和 profile 纳入缓存键。
- [ ] 实现 CUDA buffer、stream、host/device 拷贝和 RAII 生命周期。
- [ ] 实现离线模型预编译工具，并与运行时共用 prepare 逻辑。
- [ ] 生产 `CacheOnly` 模式下禁止隐式构建 Engine。
- [ ] 复用后端 contract tests，并与 ORT 输出比较。

验收：同一 ONNX 可在目标 NVIDIA GPU 上构建、缓存并重复加载，缓存失效条件可预测。

## M7：标准任务扩展

- [ ] 增加分类前后处理与 `ClassificationResult` 示例。
- [ ] 增加检测解码、NMS、坐标还原与 `DetectionResult` 示例。
- [ ] 增加 OCR 字典解码和关键点坐标还原的基础接口。
- [ ] 为每种标准任务建立 Python 参考数据和黄金测试。
- [ ] 提供自定义前后处理节点示例，验证无需修改框架核心。

验收：新增任务只增加节点和结果实现，不修改 Executor 与具体 Backend。

## M8：图像源与海康 MVS

- [x] 定义 `IFrameSource` 生命周期和 move-only 帧回调接口。
- [x] 实现固定容量相机缓冲池及异步 Frame/Tensor lease 生命周期。
- [ ] 实现目录及视频图像源，供测试和离线回放使用。
- [ ] 封装海康 MVS 枚举、打开、触发、采集和关闭流程。
- [ ] 管理厂商缓冲区释放及 `cv::Mat` 所有权转换。
- [ ] 实现断线检测、重连策略和采集错误报告。
- [ ] 建立无相机硬件可运行的模拟源测试。

验收：相机 SDK 类型不出现在 camera 模块之外，相机回调返回后异步任务仍可安全访问图像。

## M9：性能、稳定性和交付

- [ ] 建立 benchmark 工具，输出排队及各阶段 P50/P95/P99、FPS、峰值内存。
- [ ] 分析并减少不必要的 `cv::Mat`、Tensor 和 host/device 拷贝（已建立相机/业务双池和阶段间零拷贝所有权基础）。
- [ ] 建立坏模型、错误 shape、队列满载、回调异常和后端失败测试。
- [ ] 进行长时间运行和资源泄漏测试。
- [ ] 补全 API 文档、模型接入指南、部署指南和常见错误说明。
- [ ] 固化示例模型、参考输出、依赖版本和发布包结构。

验收：可扩展性、正确性和性能数据均有自动化验证；发布包能被独立示例工程消费。

## 后续候选能力

- [ ] 动态 batch 与自动合批。
- [ ] 动态高宽和 TensorRT optimization profile 管理。
- [ ] CUDA/OpenVINO 设备前后处理及零拷贝。
- [ ] 多模型 DAG、并行分支和条件节点。
- [ ] 稳定 DLL 插件 ABI 或 Python 扩展。
- [ ] INT8 校准和量化模型验证。
- [ ] 可视化流水线编辑与调试 GUI。

这些项目必须由真实部署需求和 benchmark 数据驱动，不提前进入首版范围。
