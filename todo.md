# VisionRuntime 项目规划

详细模块边界见 [Docs/architecture.md](Docs/architecture.md)。每个阶段应形成可编译、可测试的纵向增量，不在同一阶段同时扩展多个未知维度。

## M0：工程骨架

- [x] 确定项目命名、C++ namespace 和公共头文件 include 路径。
- [x] 编写根 CMake 和 Runtime 子项目 CMake，建立静态库、测试、示例和工具 target。
- [x] 将 OpenCV、JSON、日志、测试框架、FAISS 和 OpenBLAS 下载到 `Thirdparty`，记录版本、来源和固定提交。
- [x] 实现 `vision_add_runtime(... CAMERA HIK_MVS PLATFORM OPENVINO_INTEL)`，在 CMake 配置期选择唯一相机 SDK 与推理平台族，并保留 `NONE/NONE` 核心测试 Profile。
- [x] 为 OpenVINO、TensorRT、ONNX Runtime、海康 MVS 定义隔离的 imported targets，未选择的厂商实现不参与编译和链接。
- [x] 由 CMake 生成 `config/buildProfile.hpp`，提供 `SelectedCamera`、`SelectedPlatform` 及编译期能力描述。
- [ ] 在具体相机与后端适配器可构造后生成 `SelectedRuntime`，并按 Profile 选择厂商源文件和链接目标。
- [x] 对未知选项、缺失 SDK 和不支持的相机/平台组合在 CMake 配置期给出明确错误。
- [ ] 建立 MinGW 警告级别、格式化、静态分析和基础 CI（警告级别与基础 CI 已完成）。
- [x] 将 `preprocess`、`postprocess` 作为单词统一目录、命名空间和公开 include 路径。

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

- [x] 定义 `IPreprocessor`、`IInferenceBackend`、`IPostprocessor` 和线性 Pipeline 接口。
- [x] 实现 resize、颜色转换、归一化和直接写入池化 NCHW 张量的前处理。
- [x] 实现类型状态化 `PreprocessBuilder`，编译期校验节点顺序且每条链最多包含一个物化节点。
- [x] 实现 letterbox、裁剪、灰度化和二值化等可选前处理节点。
- [x] 完成 OpenVINO 后端（已增加单输入/单输出 Float32 CPU 同步推理初版，待端到端验证及模型准备契约）。
- [x] 将 OpenVINO 后端建模为 Intel 平台族，在同一契约下支持 CPU、GPU 和 NPU 设备选择且禁止静默回退。
- [x] 在 CPU 纵向功能通过后，使用 OpenVINO C++ Runtime 打通 Intel NPU 的 IR 加载、首次编译、缓存和同步推理。
- [x] 完成标量异常分数、阈值判断和 `AnomalyResult` 后处理。
- [x] 完成 PatchCore embedding memory bank 加载、FAISS L2 最近邻检索和图像级最大距离聚合。
- [ ] 完成 patch 距离图、热力图还原、坐标反向映射与缺陷区域提取。
- [x] 实现 `AnomalyResult` 示例程序：文件夹读取、OpenVINO CPU 推理及逐图 score/OK/NG 输出，并与参考 NG 图完成数值对比。
- [x] 使用同一输入与 Python 参考实现对比数值误差（已知 NG 图相对误差约 0.52%，判定一致）。
- [x] 记录 pre、infer、post、stage、wait、端到端 latency、批次总耗时和 FPS。

验收：固定 NCHW、batch 1 的异常模型可从 ONNX 经 `vision-modelc` 转译为 IR，并完成端到端推理，结果在约定容差内与 Python 一致。

## M3：模型包与配置

- [ ] 定义 `manifest.json` 与 `deployment.json` 的版本化 JSON Schema。
- [ ] 实现 `ModelManifest`、`DeploymentConfig` 和严格配置校验。
- [ ] 实现模型包相对路径解析、资源文件加载和错误上下文。
- [ ] 完成 `RuntimeFactory` 的模型包级组装和跨平台族校验（已完成按部署配置选择 Executor，并将 source、pipeline 组装为 `RuntimeSession`）。
- [ ] 实现模型制品 SHA-256、设备、驱动、精度、后端版本和构建参数组成的缓存键。
- [ ] 支持 `BuildIfMissing` 与 `CacheOnly` 两种模型准备策略。
- [ ] 完善独立 Python CLI `vision-modelc`（ONNX 到 IR、端口名校验和构建记录已实现；待增加 manifest 校验）。
- [ ] 生成包含源模型哈希、工具版本、目标平台和制品哈希的 `build-lock.json`。
- [ ] 明确 Runtime 发行包不包含 Python、`vision-modelc`、ONNX 转换器和 INT8 校准依赖。
- [ ] 编写模型包检查、构建 Profile 不匹配和错误配置测试。

验收：训练人员只需提供模型包和节点组合代码；部署配置可切换 CPU、GPU、NPU 和缓存策略，但不能切换到未编入程序的后端。

## M4：异步 Executor

- [x] 实现支持并发 `submit()` 的固定容量有界提交队列。
- [x] 以 `IPipelineExecutor` 解耦帧采集控制与串行/阶段并行任务调度。
- [x] 增加 `RuntimeSession`，由 `RuntimeFactory` 返回统一的 `start()`、非阻塞 `requestStop()` 和同步 `wait()` 全局生命周期入口。
- [x] 实现固定容量 SPSC 环形队列，连接前处理、推理、后处理和结果交付阶段；head/tail 独占缓存行并使用原子等待。
- [x] 实现 `TaskHandle`、future、回调和任务状态机。
- [x] 实现单通道 `ParallelPipelineExecutor`：前处理、推理和后处理各使用一个专属线程，并配置独立 `CompletionDispatcher`。
- [x] 入口队列满时返回 `QueueFull`；内部队列满时阻塞上游阶段并传播背压，保证任务不静默丢失。
- [x] 保证各阶段 FIFO 和按提交顺序交付；多通道及完成即交付留作后续扩展。
- [x] 定义基础执行器的平滑停止、立即停止和取消语义；执行中的 Pipeline 调用不抢占，在安全边界转换结果。
- [x] 将 Source、Executor 和 CompletionDispatcher 的停止请求与线程回收拆分，先关闭并唤醒阻塞入口，再由外部 `wait()` 按所有权顺序回收线程，消除 Block 队列停止等待环。
- [x] 明确框架只提供协作式停止；不设置回收超时、不强杀线程，永久阻塞时由应用或操作系统负责进程级退出。
- [x] 保证 Pipeline 与业务回调异常不终止工作线程。
- [x] 增加阶段并行、队列满载、背压、停止、取消、结果顺序、回调异常和图像生命周期测试。

验收：单通道流水线持续异步提交时没有悬空图像、任务泄漏、停止死锁和未捕获异常，任务严格有序，结果可通过 task ID 与工件稳定关联。当前全量 76 项测试通过，MinGW Release 独立消费者 sample 使用 80 张图完成端到端运行。

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
- [x] 实现目录图像源，供测试和离线回放使用。
- [ ] 实现视频图像源。
- [x] 封装海康 MVS GigE/USB 枚举、按序列号打开、连续/软件触发采集和关闭流程。
- [x] 定义通用 `ICameraDevice`、设备信息、采集配置和运行时能力接口，公共 API 不暴露厂商类型。
- [x] 实现 `ContinuousCameraSource` 与 `TimedTriggerSource`，分离设备取流和连续/定时输入语义。
- [x] 使用互斥 `FrameSourceConfig` 和 `FrameSourceFactory` 统一组装目录、连续相机和定时触发输入。
- [x] 收紧 `<camera>`/`<visionruntime>` 聚合头，具体 Source、设备接口和厂商适配器仅通过显式高级扩展头使用。
- [x] 按 HIK Profile 条件编译并链接 MVS 适配器，默认 `NONE` Profile 不解析相机 SDK。
- [x] 使用 `TensorBuffer` 共享 lease 零拷贝包装 SDK Buffer，最后一个视图释放时调用 `MV_CC_FreeImageBuffer`，并保证设备句柄最后释放。
- [x] 区分 SDK Buffer lease 与用户注册 Buffer 能力；MVS 4.8.1 当前不声明用户 Buffer 注册支持。
- [ ] 完成 MVS fake API 单元测试，覆盖错误映射、像素格式、帧号回绕、调用顺序和 Source 先析构时的 Frame lease。
- [ ] 在安装 MVS Runtime 的目标机使用 `hikMvsCaptureSmoke` 验证 GigE/USB 连续采集、软件触发和多帧在途。
- [ ] 查询并校准具体设备 timestamp tick frequency，再启用 `FrameMetadata::hardwareTimestamp`。
- [ ] 增加硬件触发模式及触发线、触发沿配置。
- [ ] 实现断线检测、重连策略和采集错误报告。
- [x] 建立无相机硬件可运行的 Fake Device 测试，覆盖连续委托、单 trigger 在途、输入帧间隔、超时、设备错误和停止唤醒。
- [ ] 增加可重建设备实例的自动重连 Source 策略，并定义结构化终止错误来源与可重试性。

验收：业务只依赖 `FrameSourceConfig`、`FrameSourceFactory` 和 `IFrameSource`；相机回调返回后异步任务仍可安全访问图像。

## M9：性能、稳定性和交付

- [ ] 建立 benchmark 工具，输出排队及各阶段 P50/P95/P99、FPS、峰值内存（逐任务阶段/等待/延迟、批次总耗时和 FPS 已实现）。
- [ ] 分析并减少不必要的 `cv::Mat`、Tensor 和 host/device 拷贝（已建立相机/业务双池和阶段间零拷贝所有权基础）。
- [ ] 建立坏模型、错误 shape、队列满载、回调异常和后端失败测试。
- [ ] 进行长时间运行和资源泄漏测试。
- [ ] 补全 API 文档、模型接入指南、部署指南和常见错误说明。
- [ ] 固化示例模型、参考输出、依赖版本和发布包结构。

验收：可扩展性、正确性和性能数据均有自动化验证；发布包能被独立示例工程消费。

## 后续候选能力

- [ ] 多个 `RuntimeSession` 共享工作线程池和设备级配额的进程级调度器。
- [ ] 动态 batch 与自动合批。
- [ ] 动态高宽和 TensorRT optimization profile 管理。
- [ ] CUDA/OpenVINO 设备前后处理及零拷贝。
- [ ] 多模型 DAG、并行分支和条件节点。
- [ ] 稳定 DLL 插件 ABI 或 Python 扩展。
- [ ] INT8 校准和量化模型验证。
- [ ] 可视化流水线编辑与调试 GUI。

这些项目必须由真实部署需求和 benchmark 数据驱动，不提前进入首版范围。
