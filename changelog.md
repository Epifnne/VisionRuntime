# Changelog

## Unreleased - 2026-09-16（批推理计时与基准）

### Changed

- `IStagedVisionPipeline` 新增 `inferBatch()`：批推理收编进管线接口，`Pipeline::inferBatch` 接管 batch 维拼接/单次推理/切片（共享 helpers 移到新头文件 `pipeline/batchTensors.hpp`）；`BatchPipelineExecutor` 只保留凑批与超时 flush 调度，不再直调后端；`backend()` 访问器随之从接口、`Pipeline`、`TimedPipeline` 移除（原系批量执行器的旁路开口）。
- `TimedPipeline::inferBatch` 将整批推理 wall time 全额记入每个成员的 `inferenceMilliseconds`（latency 口径），batch 模式下 stage p50/p95/p99 自此覆盖推理阶段；同时补上 batch 路径 preprocess 失败的记账（此前 `finishExecution` 不被调用，`timings_` 泄漏且失败不计数，`finishExecution` 按 executionId 幂等，`run()` 路径不受影响）。

### Added

- 运行中实时统计：`VisionService` 在 session.start 成功后拉起专用 metrics 线程，每 500ms 调用 `MetricsAggregator::publishLive()` 刷新 `session.summary`（经 `SessionController::liveCounters()` 从运行中的 `MultiCameraSession::currentSummary()` 直读）与 `metrics.performance`（经 `TimedPipeline::livePerformanceSnapshot()` 当前批快照）；批次结束时 `publishSummary()`/`batchObserver` 覆盖为最终值。此前两个端点运行中恒为零，GUI 的总 FPS/数量控件无数据。
- `SessionController::completionCallback` 由 `VisionService` 接线：有限源自然结束时先停 metrics 线程再发布最终 summary/state，GUI 在目录源跑完后也能看到最终计数。
- `MetricsAggregator` 的 payload 字符串（live/lastBatch）与 `VisionService::metricsThread_` 分别由新增 `payloadMutex_`/`metricsThreadMutex_` 保护（写：pipeline/控制/metrics 线程；读：任意线程的端点快照调用）。

### Fixed

- `VisionServiceDirectorySessionTest.RunsDirectorySessionEndToEnd`：订阅回调对同一 payload 多次发布不幂等（latch 重复 count_down 越过初始计数导致 try_wait 永假），改为原子标志只 count_down 一次。
- 产品包 `products/rubberRingBatch4`：4 路目录源（共享 rubberRing 图像、不限流）+ `maxBatchSize: 4` 吞吐基准，UI 为 4 路 imageView + 统计数字；配套 `Tools/createBatchBenchModel.py` 生成原生动态 batch 维的基准 IR（`[-1,3,224,224] → [N,1]`）。
- 基准结论（本机 MinGW Debug + OpenVINO CPU）：4 源不限流时 batch=4 与 batch=1 均 ~300 fps（预处理单线程饱和），batch 吞吐收益 <3% 而单帧 stage p50 从 5.7ms 升至 12.7ms——CPU 上单次推理调用开销占比极小，批量的收益场景在 GPU/TensorRT 等高 per-call 开销后端。
- 模型注意：rubberRing 的 PatchCore IR 内部 Reshape pattern 写死 batch=1，插件 `dynamicBatch` 只 reshape 输入端口，推理时形状冲突失败；动态 batch 模型需在导出时生成（已记入 `Docs/architecture.md` 与 `Docs/backendPlugins.md`）。

## Unreleased - 2026-09-16

### Added

- 新增 `Shell/visionShell`（GUI Shell 计划阶段 C，本仓库唯一链接 Qt 的 target）：`ServiceClient` 绑定层（端点注册表 → QML 属性/方法，订阅回调经队列连接回 GUI 线程）与 `FrameImageProvider`（`QQuickImageProvider`，`Frame`→`QImage` 零拷贝包装，自定义 deleter 持有共享 buffer）。
- 通用控件集 `Shell/controls/`：`NodeView`（按 profile `ui` 节递归实例化控件树）、`ImageView`（流画面 + 实测 FPS）、`StateCard`（状态灯卡）、`CommandButton`、`ParameterForm`（按端点类型生成编辑器）、`StatNumber`、`ResultTable`；全部只绑定端点，不含领域语义。
- `main.qml` 改为纯 profile 驱动：TabBar + StackLayout 按 `ui.pages` 生成页面，无任何产品硬编码。
- `visionShell` 增加 `--smoke <seconds>` 验收模式：自动 start、按流统计帧数、stop 后等待 idle 输出 summary/performance 对照。
- 新增 `publishShell` target：windeployqt 组装 exe + Qt/MinGW 运行时 + `plugins/` + 产品包为自包含发布根。
- 产品包：`products/rubberRing`（单路目录源监控页）与 `products/rubberRingCompact`（纯 profile JSON 派生的第二布局，闭环验收用）。
- 新增 `VISION_BUILD_SHELL` 选项（默认 OFF，依赖 `VISION_BUILD_SERVICE`）。
- MSVC 构建验证通过：`Build/MSVC-2026` 树以 `VISION_BUILD_SHELL=ON` + `CMAKE_PREFIX_PATH=D:/Qt-OpenSource/6.10.1/msvc2022_64`（Qt 6.10.1 开源套件）配置，visionShell 与 publishShell 均构建通过，smoke EXIT=0、28/28 帧。

### Fixed

- `BatchPipelineExecutor::wait()` 收尾时补调 `pipeline_->finishBatch()`，与 serial/parallel 执行器一致；此前 batch 模式下 `TimedPipeline` 的 `BatchPerformanceObserver` 从不触发，`metrics.performance` 端点恒为零值。
- `Shell/src/main.cpp` 的 image provider 原以栈对象传给 `QQmlEngine::addImageProvider`（该 API 取得 provider 所有权），引擎析构时 delete 栈地址导致堆损坏断言（MSVC debug 堆检出，MinGW 无检测此前静默存在）；改为堆分配交由引擎管理。
- 全工程 MSVC 统一动态 CRT：根工程与 4 个 Samples 工程统一 `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL`，OpenCV 子工程 `BUILD_WITH_STATIC_CRT=OFF`，gtest `gtest_force_shared_crt=ON`；消除静态 CRT（/MTd）exe 与 Qt（/MDd）双 CRT 堆导致的 ucrt debug_heap 断言弹窗（终端无输出、退出码 0x80000003）。

## Unreleased - 2026-09-15

### Added

- 新增 `Service/visionService` 静态库（GUI Shell 计划阶段 B，纯 C++20、不依赖 Qt）：端点注册表（Parameter/Command/State/Stream）、专用分发线程串行投递订阅回调、Stream 端点只保留最新帧（旧帧计 dropped，不回压 pipeline）。
- 新增 `SessionController`：会话生命周期状态机（Idle→Configuring→Running→Stopping），控制线程独占 `MultiCameraSession` 所有权并是唯一调用 `wait()` 的线程；完成监视线程在有限源自然结束时触发优雅收尾。
- 新增 `ProfileLoader` 解析产品 profile JSON（`schemaVersion`、`sources[directory|camera]`、`model`、`pipeline`、`endpoints`、`ui` 透传）。
- 新增 `SessionAssembler`：profile → 目录/相机源 + preprocess 链 + 插件 backend + 阈值后处理 + `TimedPipeline` + batch executor，并经 `MultiCameraSession` 的 `frameObserver` 把帧零拷贝分流到对应 Stream 端点。
- 新增 `CameraService`（枚举包装、曝光/增益/软触发端点）与 `manifestExporter`（注册表序列化为 Designer manifest JSON）。
- `MultiCameraSession` 增加可选 `frameObserver` 与 `sources()` 访问器（供 Service 帧分流，不改核心默认行为）。
- 新增 `VISION_BUILD_SERVICE` 选项（默认 OFF）；Service gtest 套件（端点注册表、profile 解析、会话状态机、目录源端到端经 fake backend 插件）。

## Unreleased - 2026-09-11

### Added

- `ICameraDevice` 增加运行时调参接口 `setExposureMicroseconds()`/`setGain()`：可在启动前或采集中调用，默认实现返回 `Unsupported`；`HikrobotMvsCameraDevice` 先关闭对应 Auto 模式再写 MVS 浮点节点，非法取值返回 `InvalidArgument`（GUI Shell 计划阶段 A）。
- 增加 `CameraDeviceContractTest` 覆盖无相机 SDK 构建下的调参不支持路径；`hikMvsCaptureSmoke` 扩展负值拒绝契约检查与可选曝光/增益实机调参参数。
- 增加 `BatchPipelineExecutor`：聚合多源预处理后帧为 batch tensor 一次推理，支持凑满 `maxBatchSize` 触发与 `flushTimeout` 超时强制 flush（动态 batch），输出按 batch 维切片逐样本后处理，batch 成员与提交任务按 FIFO 一一对应。
- 增加 `MultiCameraSession`：统一管理多个 `IFrameSource`，按 source 下标烙印 `PipelinePacket::sourceId()`，提供 per-source 与全局运行统计，任一 source 启动失败回滚已启动 source。
- `PipelinePacket` 增加可选 `sourceId`；`BoundedBlockingQueue` 增加定时等待 `popFor`；`IStagedVisionPipeline` 增加 `backend()` 访问器供批量执行器直接调用后端。
- `RuntimeFactory` 增加 `createBatchExecutor()` 便捷入口。
- anomaly preset manifest 校验放开 batch 维：接受 `[N,1,224,224]` 任意 N≥1。
- OpenVINO 插件增加 `dynamicBatch` 选项：编译前将输入 batch 维 reshape 为动态，一个编译模型服务任意 N。
- TensorRT 插件增加 `maxBatchSize` 选项：create 时校验 engine optimization profile 的 max batch 覆盖配置值。
- 新增 `BatchPipelineExecutorTest`（凑满/超时/连续批次/平滑排空）与 `MultiCameraSessionTest`（多源归属统计/启动回滚）；计划见 `Docs/multiCameraBatchPlan.md`。

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

- Windows 构建新增 MSVC 支持；MSVC 目标和源码构建的静态 OpenCV 统一使用 `/MTd` 或 `/MT`，OpenCV 4.12 对 MSVC 19.51 显式沿用 `vc17` ABI 标识。
- Windows HIK_MVS 业务目标部署仓库内匹配版本的 MVS 4.8.1 完整 runtime，避免优先加载系统中其他版本；OpenVINO 继续只部署平台公共库、所选设备插件和模型 frontend。
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

- 使用 MinGW 验证 MVS 连续采集，以及使用 MSVC/MinGW 验证软件触发、停止延迟和多帧在途 lease。
- 增加可注入 MVS C API 后的无硬件单元测试，覆盖错误映射、帧号回绕和 `Free -> Close -> Destroy` 顺序。
- 增加基于 Factory 重建设备实例的自动重连策略，并完善结构化终止错误分类。

### Validation

- MSVC 19.51 x64 Debug 独立构建 `anomalyHikMvsSample` 成功；GigE 相机 `169.254.239.231` 完成采集、灰度前处理、OpenVINO CPU 推理和异常后处理，首帧输出 `score=5.45422, threshold=2, decision=NG`。
- MinGW Debug 默认 `NONE` Profile 下 `visionRuntime`、`buildProfileTest` 和 `frameBufferPoolTest` 目标构建通过；相关 7 项定向 CTest 全部通过。
- MinGW Debug `pipelineExecutorTest` 构建及 RuntimeFactory、FrameExecutor、串行/并行 Executor 相关测试通过。
- Source 与 Executor 生命周期相关 21 项定向测试全部通过，包含 Block 提交停止唤醒与回调线程请求停止场景。
- MinGW Debug 全量 76 项测试全部通过。
- 独立消费者 Release `anomalyDirectorySample` 使用新 `RuntimeSession` 生命周期 API 构建并运行通过；当前目录中的 80 张图（27 NG、53 OK）全部提交并在有限 Source 结束后平滑排空。
- Factory 测试 3 项、相机 Source 相关测试 17 项通过；默认 `NONE` Profile 的 `<visionruntime>` 消费者可编译链接，并对相机配置返回 `Unsupported`。
- `HIK_MVS/NONE` smoke 目标与独立 `hikMvsCaptureSample` 成功链接；显式厂商扩展头不泄漏到默认聚合 API。

## Unreleased - 2026-09-01 ~ 2026-09-15

### Added

- 增加 `Embedding` 模型输出布局和 PatchCore `[1,N,D]` 模型清单 preset。
- `AnomalyPostprocessor` 支持从原始 Float32 memory bank 构建 FAISS `IndexFlatL2` 索引，对每个 patch embedding 执行最近邻检索，并以最大平方 L2 距离作为图像级异常分数。
- 固定引入 FAISS 1.12.0 与 OpenBLAS 0.3.30 源码依赖；OpenBLAS 使用单精度、单线程、无 LAPACKE 配置构建。
- 增加 memory bank embedding 后处理单元测试，覆盖索引加载、最近邻检索和图像级最大距离聚合。

### Changed

- `AnomalyPreset` 同时接受标量 `[1]` 与 embedding `[1,N,D]` 输出；embedding 模式通过模型选项传入 memory bank 路径和维度。
- 目录与海康异常检测 sample 统一通过 `RuntimeFactory::createFromPreset<AnomalyPreset>()` 创建会话，移除专用 `AnomalyRuntimeFactory` 包装。
- `bootstrapDependencies` 增加 FAISS 和 OpenBLAS 的固定提交下载与核验。

### Pending Validation

- 使用 MinGW Debug 构建并运行新增 FAISS memory bank 后处理测试。
- 使用真实 PatchCore embedding 模型与 memory bank 完成端到端分数对比。

### Validation