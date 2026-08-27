# Changelog

## Unreleased - 2026-08-01 ~ 2026-08-15

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
- 增加 `visionRuntime` 库 target，并从源码最小集成 OpenCV `core`、`imgproc` 和 `imgcodecs` 模块。
- 实现模板化 `PipelineExecutor`、`TaskHandle` 和任务状态机，支持固定容量并发提交、shared future、独立回调交付、FIFO 顺序、取消及平滑/立即停止。
- 增加 Executor 队列满载、顺序交付、异常隔离、排队取消和立即停止测试。
- 增加类型状态化 `PreprocessChainBuilder`，支持按顺序组合前处理节点，并在编译期禁止错误节点顺序和第二个物化节点。
- 增加 `CvResizeNode`、`CvCenterCropNode`、`ToTensorNode` 和 `NormalizeNode`，支持短边缩放、中心裁剪、Float32 NCHW 物化及逐通道归一化。
- 增加前处理数值、相机 Frame 释放、TensorBufferPool lease 归还和节点组合约束测试。
- 增加 `vision_add_runtime` CMake 入口及 `VISION_CAMERA_SDK`、`VISION_INFERENCE_PLATFORM` 缓存选项，校验 `NONE`/`HIK_MVS` 与 `NONE`/`OPENVINO_INTEL` 选择。
- 由 CMake 生成公共 `config/buildProfile.hpp`，暴露 `SelectedCamera`、`SelectedPlatform`、稳定枚举、名称和相机/CPU/GPU/NPU 能力。
- 增加 Build Profile 编译期映射和运行时名称测试，同一测试覆盖默认 `NONE/NONE` 与 `HIK_MVS/OPENVINO_INTEL` 组合。
- 增加 OpenVINO、TensorRT、ONNX Runtime 和海康 MVS 隔离 imported target；仅在选择对应厂商能力时解析 SDK。
- 增加固定提交的依赖引导 target，覆盖 OpenCV、GoogleTest、nlohmann/json 和 spdlog。
- 增加 MinGW 严格警告及 Windows MinGW 基础 CI，并在配置期拒绝其他编译器。
- 增加示例与工具聚合 target，为后续可执行程序提供稳定工程入口。
- 增加 `AnomalyResult` 和标量 `AnomalyPostprocessor`，按指定 Float32 输出首元素及阈值生成 OK/NG 结果。
- 增加 OpenVINO C API 单输入/单输出 Float32 同步后端初版，仅在 `OPENVINO_INTEL` Profile 下编译。
- 增加 `anomalyDirectorySample`，串联目录 `FileSource`、NCHW/ImageNet 前处理、OpenVINO 推理和异常阈值后处理。
- 增加 `VISION_OPENVINO_DEVICE` 和 `VISION_MODEL_ARTIFACT_TYPE` 构建选项；`anomalyDirectorySample` 只部署选中设备插件与 ONNX/IR frontend，以及公共 OpenVINO、TBB 和 MinGW 运行库。
- 增加 target 级 `vision_target_runtime()`；业务 target 只声明平台、设备和模型制品，框架负责创建 Runtime 变体、链接、C++20 要求及运行库部署。
- 增加 `vision-modelc` Python CLI，使用 OpenVINO 官方转换 API 将 ONNX 转为 `.xml + .bin` IR，并生成包含输入输出信息、OpenVINO 版本和 SHA-256 的构建记录。
- 增加通用 `benchmark::TimedPipeline` 装饰器，通过 observer 上报 Pipeline 各阶段及总耗时。

### Changed

- 将 `preprocess` 和 `postprocess` 统一视为单词：命名空间与路径使用全小写，类型使用 `Preprocess`、`Postprocess` 词形。
- 将测试目标定义下放至 `Runtime/tests/CMakeLists.txt`，并按模块组织测试源码。
- 将通用固定槽位管理从相机模块抽取到 `core::TensorBufferPool`。
- `Frame` 改为不可复制、可移动类型，移动后源对象进入明确的空状态。
- 明确图像源的公共输出为 `Frame`：文件源内部管理解码存储，前处理阶段再将 Frame 转换为模型 Tensor。
- 明确当前 Executor 在统一 `IVisionPipeline::run()` 上调度；三阶段线程与 SPSC 队列待统一阶段化 Pipeline contract 落地后实现。
- 使用标准 CMake `CTest` 模块生成测试元数据，消除 CMake Tools 缺少 `DartConfiguration.tcl` 的警告。
- 文档确立由 CMake 在配置期唯一选择相机 SDK 与推理平台族，并生成业务代码使用的构建 Profile；首个组合为海康 MVS 与 OpenVINO Intel。
- 文档将 Intel NPU 纳入首个 OpenVINO 平台族，并明确具体相机实例和 CPU/GPU/NPU 设备仍由运行时配置校验。
- 文档确立独立 Python `vision-modelc` 负责 ONNX 校验和 OpenVINO IR 转译但不进入 Runtime 发行包；目标机首次编译由 OpenVINO C++ Runtime 完成，首版不负责 INT8 校准。
- 将 `anomalyDirectorySample` 移到顶层 `Samples/anomalyDirectory` 独立消费者工程；框架作为子目录使用时不再构建自身测试和工具。
- 将 `anomalyDirectorySample` 的 PatchCore 手写前处理替换为 `CameraFramePreprocessBuilder` 公共节点链。
- 将 `anomalyDirectorySample` 的计时 Pipeline 实现移入 benchmark，sample 只保留异常结果 CSV 格式化；CSV 继续写标准输出。

### Validation

- MinGW Debug 构建通过。
- 原有 29 个单元测试全部通过。
- `fileSourceTest` 目标使用 MinGW Debug 构建通过，新增 2 个 FileSource 单元测试全部通过；当前共 31 个测试。
- `pipelineExecutorTest` 目标使用 MinGW Debug 构建通过，新增 5 个 Executor 单元测试全部通过；CTest 当前发现 44 个测试。
- 可组合前处理、旧兼容前处理器和 Pipeline 相关目标使用 MinGW Debug 构建通过；CTest 当前发现并通过全部 47 个测试。
- `buildProfileTest` 分别在 `NONE/NONE` 与 `HIK_MVS/OPENVINO_INTEL` Profile 下使用 MinGW Debug 构建并通过；CTest 当前发现 48 个测试。
- 默认 `NONE/NONE` Profile 下全量 48 个测试通过。
- M0 工程基础设施变更后，MinGW Debug 全量构建通过，默认 `NONE/NONE` Profile 下 48 个测试全部通过。
- `anomalyPostprocessorTest` 使用 MinGW Debug 构建通过，新增 2 个标量异常分数测试全部通过。
- `anomalyDirectorySample` 使用 MinGW Debug、OpenVINO C API 和 CPU 构建运行通过；参考 NG 图输出 `2.91676521`，Python 参考值为 `2.90172958`，相对误差约 0.52%，阈值 2.0 下判定一致。
- 从 `Samples/anomalyDirectory` 顶层独立配置并通过 `VISION_RUNTIME_ROOT` 引入框架源码构建成功；消费者无需设置 C++ 标准，框架自动传播 C++20，且框架测试/工具未进入业务构建图。
- 短边缩放与中心裁剪新增 1 个前处理测试并通过；独立消费者 `anomalyDirectorySample` 重建及真实模型目录运行通过。
- `vision-modelc` 使用 OpenVINO 2026.3 将样例 ONNX 成功转换为 FP32 IR，端口校验及构建记录生成通过；IR Profile 下 MinGW Release `-O3` 构建和 6 张样例图运行通过，分数与 ONNX 一致。
- `TimedPipelineTest` 的成功与后端失败 observer 用例通过；独立消费者 `anomalyDirectorySample` 使用 MinGW Release 增量构建通过。

## Unreleased - 2026-08-15 ~ 2026-08-31

### Added

- 增加 `RuntimeSession<ResultType>`，统一整次帧源运行的启动、等待、停止和汇总生命周期。
- 增加 `RuntimeFactory::createRuntime()`，按 `DeploymentConfig` 选择串行或阶段并行执行器，并与帧源组装为运行会话。
- 增加工厂创建可运行会话的 Executor 集成测试。
- 增加 `IPipelineExecutor`、`SerialPipelineExecutor` 和三阶段 `ParallelPipelineExecutor`，支持配置选择串行或阶段重叠执行。
- 增加 `IStagedVisionPipeline`，统一 preprocess、inference、postprocess 阶段 contract。
- 增加 `common::BoundedBlockingQueue<T>`：固定容量 SPSC 无锁环形数据路径，使用 `atomic::wait/notify` 阻塞，head/tail 独占 64 字节缓存行。
- 增加 `core::CompletionDispatcher<ResultType>`，通过有界 SPSC 队列异步 FIFO 交付 future 与 callback。
- 增加 `executor::ExecutorTask<ResultType>`，统一任务 ID、状态、取消、future 和 callback 生命周期。
- 增加 `ExecutorConfig`、部署 JSON 解析和 `RuntimeFactory` 执行策略组装，支持入口满载策略、入口容量及阶段容量。
- 增加 SPSC 顺序、阻塞唤醒、关闭唤醒、零容量、阶段重叠、立即取消和工厂策略测试。
- 增加通用 `CameraDeviceInfo`、`CameraDeviceOptions`、`CameraCapabilities` 和 `ICameraDevice` 工业相机契约。
- 增加 `HikrobotMvsCameraDevice`，支持 MVS GigE/USB 枚举、序列号选择、连续采集和软件触发。
- 增加 MVS SDK Buffer 的只读零拷贝 Frame lease，最后一个视图释放时归还 SDK Buffer，并延长设备句柄生命周期至所有在途帧释放。
- 增加 HIK Profile 专用 `hikMvsCaptureSmoke` 实机检查工具。
- 增加 `ContinuousCameraSource` 和 `TimedTriggerSource`，将设备取流与连续/定时输入调度分离。
- 增加互斥的 `FrameSourceConfig` variant 与 `FrameSourceFactory`，统一组装目录、连续相机和定时软件触发输入。
- 增加无硬件 Fake Device 测试，覆盖连续委托、单 trigger 在途、输入帧间隔、响应超时、触发/设备错误和停止唤醒。

### Changed

- Runtime、FrameSource、Pipeline Executor 和 CompletionDispatcher 将非阻塞停止请求与同步线程回收拆分为 `requestStop()` 和 `wait()`；回调线程不再执行 join。
- `FrameExecutor` 先请求 Source 与 Executor 停止以解除阻塞提交，再由外部 `wait()` 按所有权顺序回收线程，避免 Block 队列停止时形成等待环。
- `FileSource` 使用独立 stop source，并允许停止请求唤醒帧间隔等待；框架不提供回收超时或进程终止语义。
- `FrameExecutor` 仅依赖注入的 `IPipelineExecutor`，不再接收 Pipeline、构造串行执行器或持有执行器队列配置。
- `anomalyDirectorySample` 改为只持有 `RuntimeFactory` 返回的 `RuntimeSession`，不再手工组装 `FrameExecutor` 与 Pipeline Executor。
- 串行与并行执行器复用 `ExecutorTask` 和 `CompletionDispatcher`，移除重复任务状态机、完成队列、完成线程及阶段 mutex/CV。
- 并行执行器的 preprocess→inference、inference→postprocess、postprocess→completion 均改为固定容量 SPSC 队列，慢阶段和慢 callback 会向入口传播背压。
- `TimedPipeline` 输出改为带名的 pre、infer、post、stage、wait、latency，并增加批次总耗时、完成/失败数和 FPS；文件输出使用对应 CSV 列。
- 删除旧 `PipelineExecutor` 名称和兼容头，串行实现直接使用 `SerialPipelineExecutor`。
- HIK Profile 现在条件编译并链接 MVS 适配器；`HIK_MVS_ROOT` 默认指向仓库 `Thirdparty/hik-mvs/4.8.1`。
- 相机 Build Profile 将 SDK Buffer lease 与用户注册 Buffer 分开描述；MVS 4.8.1 不再错误声明用户 Buffer 注册能力。
- `IFrameSource` 与 `ICameraDevice` 明确为单次启动对象；停止请求与线程回收分离，终止后重建设备需要创建新实例。
- 连续源的 `frameRate` 改为设备真实采集帧率；定时源的 `triggerInterval` 从上一成功输入帧到达时刻计算，回调耗时计入间隔且不允许 trigger 重叠。
- 定时源在响应等待、回调后和 interval 期间都转发设备终止错误，不再因成功响应去重状态而丢失错误。
- `<camera>` 聚合头只导出配置、Factory、值类型和 `IFrameSource`；具体 Source、`ICameraDevice`、Buffer Pool 与海康 API 改为显式高级扩展头。顶层聚合头不再默认导出 benchmark 和业务 Preset。

### Pending Validation

- 在已安装 MVS Runtime 和驱动的目标机运行 `hikMvsCaptureSmoke`，验证连续采集、软件触发、停止延迟和多帧在途 lease。
- 增加可注入 MVS C API 后的无硬件单元测试，覆盖错误映射、帧号回绕和 `Free -> Close -> Destroy` 顺序。
- 增加基于 Factory 重建设备实例的自动重连策略，并完善结构化终止错误分类。

### Validation

- MinGW Debug 默认 `NONE` Profile 下 `visionRuntime`、`buildProfileTest` 和 `frameBufferPoolTest` 目标构建通过；相关 7 项定向 CTest 全部通过。
- MinGW Debug `pipelineExecutorTest` 构建及 RuntimeFactory、FrameExecutor、串行/并行 Executor 相关测试通过。
- Source 与 Executor 生命周期相关 21 项定向测试全部通过，包含 Block 提交停止唤醒与回调线程请求停止场景。
- MinGW Debug 全量 76 项测试全部通过。
- 独立消费者 Release `anomalyDirectorySample` 使用新 `RuntimeSession` 生命周期 API 构建并运行通过；当前目录中的 80 张图（27 NG、53 OK）全部提交并在有限 Source 结束后平滑排空。
- Factory 测试 3 项、相机 Source 相关测试 17 项通过；默认 `NONE` Profile 的 `<visionruntime>` 消费者可编译链接，并对相机配置返回 `Unsupported`。
- `HIK_MVS/NONE` smoke 目标与独立 `hikMvsCaptureSample` 成功链接；显式厂商扩展头不泄漏到默认聚合 API。
