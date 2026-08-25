# VisionRuntime 架构设计

## 1. 项目目标

VisionRuntime 是一个面向工业视觉模型部署的 C++20 SDK。模型训练人员提供 ONNX 模型、模型元数据，并使用框架内置或自定义的前后处理节点组合推理流水线；业务软件通过同步或异步 API 执行分类、检测、异常检测、OCR 和关键点等任务。

首要目标：

- 在 CMake 配置期显式绑定相机 SDK 和推理平台族，只编译、链接所选厂商适配器，并生成业务代码使用的构建 Profile。
- 使用一套后端无关 API 支持 OpenVINO、TensorRT、ONNX Runtime 及 NPU 后端；首个平台族为 OpenVINO Intel，覆盖 CPU、GPU 和 NPU。
- 首个相机 SDK 为海康机器人 MVS；具体相机型号、序列号、曝光和触发参数仍在运行时配置并按编译期能力约束校验。
- 前后处理可组合、可替换，并允许业务工程通过 C++ 实现自定义节点。
- 在线运行采用单通道分阶段异步流水线，具备有界队列、异步回调、有序交付、错误传播和分阶段性能统计。
- 公共接口保持清晰、强类型；当前提供 CPU 图像池化和阶段间零拷贝移交，并为后续动态批处理、设备内存及跨设备零拷贝保留扩展空间。

首版约束：Windows、MinGW-w64、C++20、CMake、静态库优先、固定 NCHW、batch 1、CPU 前后处理。首个纵向功能先完成 OpenVINO CPU 异常检测，再沿用同一 OpenVINO 平台族打通 Intel NPU，模型输出异常分数和热力图。

## 2. 总体数据流

```text
FileSource / Camera Adapter
	|
	v  Frame（图像语义）
	|
	v
RuntimeSession / FrameExecutor -- 全局生命周期、停止条件、采集统计
	|
	v  PipelinePacket
	|
	v
IPipelineExecutor -- 可替换提交边界
	|
	v
Serial / Parallel PipelineExecutor -- 有界队列、任务 ID、future、回调
	|
	v
Preprocess Thread  -- resize、颜色转换、归一化、layout 转换
	|
	v  有界 SPSC 队列
Inference Thread  -- ORT / OpenVINO / TensorRT
	|
	v  有界 SPSC 队列
Postprocess Thread -- 阈值、坐标还原、热力图、区域提取
	|
	v  有界 SPSC 队列
CompletionDispatcher -- future 就绪、业务回调
	|
	v
VisionResult  -- Classification / Detection / Anomaly / OCR / Keypoint
```

框架同时提供同步 `run()` 和异步 `submit()`。同步执行定义基础语义；异步执行器只负责调度，不理解模型和算法。首版只有一个执行通道，各阶段各有一个专属线程，阶段间使用 FIFO 的有界 SPSC 队列，因此任务天然按提交顺序交付。

## 3. 目录和模块职责

```text
Runtime/
├─ benchmark/        # Pipeline 分阶段计时装饰器和指标结构
├─ include/
│  ├─ common/        # 无业务语义的并发容器等底层设施
│  ├─ core/          # 无视觉业务语义的基础类型
│  ├─ vision/        # 图像、几何、变换上下文和标准结果
│  ├─ preprocess/    # 前处理节点接口与内置节点
│  ├─ backends/      # 统一后端接口及各后端声明
│  ├─ postprocess/   # 后处理节点接口与标准任务实现
│  ├─ pipeline/      # 线性流水线、构建器和运行时门面
│  ├─ executor/      # 任务调度、线程和执行器接口
│  ├─ config/        # 模型清单、部署配置、校验和对象构造
│  ├─ runtime/       # RuntimeFactory 与 RuntimeSession 全局生命周期
│  ├─ camera/        # 文件夹图像源、图像源抽象及海康 MVS 适配器
│  ├─ logs/          # 日志接口和运行指标
│  └─ gui/           # 预留，不属于首版核心 SDK
├─ src/              # 对应模块的非公开实现
├─ tests/            # 单元、契约、集成和稳定性测试
└─ tools/            # 模型检查、缓存预编译和 benchmark

Samples/
└─ anomalyDirectory/ # 独立消费者工程，模拟 Thirdparty/VisionRuntime 接入
```

框架作为顶层项目配置时构建自身测试和工具；作为业务工程的 `Thirdparty/VisionRuntime` 子目录引入时只提供 Runtime targets 与 `vision_target_runtime()`，不会把框架测试、工具或示例加入消费者构建图。Runtime 的公开 C++20 要求通过 target usage requirements 自动传播，业务工程无需重复设置语言标准。

### 3.1 core

`core` 位于 `common` 之上，只提供通用基础能力：

- `Status`、错误码和 `Result<T>`。
- `TensorBuffer` 描述底层存储的地址、容量、设备、内存类型、可写性和共享生命周期。
- `TensorBufferPool` 提供线程安全的固定容量可复用缓冲区；最后一个视图释放后槽位自动归还。
- `Tensor` 是 `TensorBuffer` 上的类型化多维视图，支持 shape、byte stride、offset、连续性和 subview。
- CPU/CUDA 等 `Device` 描述。
- `CompletionDispatcher<ResultType>` 使用固定容量 SPSC 队列和独立消费线程，按 FIFO 设置任务结果并触发回调。
- 公共导出宏和基础类型。

`common` 位于 `core` 下方，当前提供 `BoundedBlockingQueue<T>`：固定容量 SPSC 环形队列，数据路径使用 acquire/release 原子操作，阻塞等待使用 C++20 `atomic::wait/notify`。生产者写入的 tail 与消费者写入的 head 分别占用 64 字节缓存行，避免伪共享。该队列只允许一个生产者和一个消费者，不能用于多业务线程并发提交的入口队列。

`core` 只依赖 `common`，不依赖 OpenCV、推理 SDK、配置解析器或执行器。

### 3.2 vision

`vision` 表达视觉领域数据：

- `Frame`：`TensorBuffer` 上的 move-only 图像视图，包含宽高、像素格式、行步长和采集元数据。
- `FrameMetadata`：序列号、采集时间和可选硬件时间戳。
- `TransformContext`：原图尺寸、网络尺寸、缩放、裁剪和填充信息。
- 强类型任务结果：分类、检测、异常、OCR 和关键点。
- `VisionResult`：标准任务结果的 `std::variant` 容器。

`TransformContext` 随前处理结果传递，后处理依靠它把检测框、关键点和热力图映射回原图。

### 3.3 preprocess

前处理模块把 `Frame` 转换为后端可消费的 `TensorMap`。`PreprocessBuilder::start<Frame>()` 使用类型状态组合配置描述，构建完成后仍以 `IPreprocessor` 接入 Pipeline；节点在同一前处理线程中顺序运行，不为每个节点创建线程。每次执行产生 `PreparedInput`，其中同时包含张量和 `TransformContext`。配置描述在 `then()` 中保持链式表达，参数校验和资源创建错误由 `build()` 统一返回。

当前已实现的节点链为：

```text
Camera Frame
	-> Resize
	   short-side resize，写入池化 8-bit Frame
	-> CenterCrop
	   在 Resize Frame 上建立零拷贝视图
	-> ToTensor
	   Gray/BGR/BGRA to RGB + Float32 NCHW
	   直接写入 TensorBufferPool 槽位并释放工作 Frame
	-> Normalize
	   在同一 Tensor 上原地执行 scale / mean / standard deviation
```

`Resize` 为中间 Frame 使用独立 BufferPool，默认限制输出长边不超过 4096；`CenterCrop` 共享 Resize Buffer，不再次复制图像。`ToTensor` 是 Frame 到 Tensor 的唯一物化边界：它申请最终张量 Buffer，并直接写入目标 NCHW 平面，不执行 resize 或 crop。`Normalize` 自动继承当前张量名，只修改已有 Tensor，不改变其 Buffer lease。所有构建期操作实现统一的 `PreprocessNode` 协议，Builder 的泛型 `then()` 不依赖具体节点类型，因此新增节点无需修改 Builder。

节点通过 `CameraFrame`、`Tensor` 类型状态声明输入输出，并通过 `materializes` 标记物化边界。Builder 在编译期保证：

- 归一化节点不能出现在图像物化之前。
- 一条链必须经过物化后才能 `build()`。
- 一条链最多包含一个物化节点，第二个物化节点无法通过 `then()` 约束。

当前 Frame 节点支持保持宽高比的短边缩放和中心裁剪；缩小时使用抗锯齿双线性采样并输出 8-bit Frame，裁剪通过共享 Buffer 视图完成。`ToTensor` 支持 Gray8/Bgr8/Bgra8 到 RGB、Float32 NCHW，随后可执行逐通道归一化。letterbox、任意区域裁剪、灰度化与二值化节点尚未实现。

自定义前处理通过实现 `IPreprocessor` 随业务程序一起编译，不在首版提供运行时 DLL ABI。

### 3.4 backends

`IInferenceBackend` 只抽象模型执行引擎，统一以下生命周期：

```text
prepare(ModelArtifact, options) -> PreparedModel
infer(PreparedModel, TensorMap) -> TensorMap
```

`ModelArtifact` 是经过模型包校验后交给已编译后端的目标制品，不要求所有后端直接消费 ONNX：OpenVINO 首版接收 IR 或兼容的预编译制品，TensorRT 后续可接收 ONNX 或 Engine。

- ONNX Runtime 直接创建 Session。
- OpenVINO 从模型包中的 IR 读取模型，并通过 C++ Runtime 编译到所选 Intel CPU、GPU 或 NPU；也可导入兼容环境预编译的制品。
- TensorRT 解析 ONNX 并构建或加载 Engine。

当前 OpenVINO 纵向增量使用 C API 隔离 ABI，只在 `OPENVINO_INTEL` Profile 下编译。它接收单个连续 host Float32 Tensor，通过一个同步 `InferRequest` 执行单输入、单输出模型，并将 Float32 输出复制到框架 `TensorMap`。当前 sample 产物由 `VISION_OPENVINO_DEVICE` 固定打包 CPU、GPU 或 NPU 中的一个设备插件，由 `VISION_MODEL_ARTIFACT_TYPE` 固定打包 ONNX 或 IR frontend；运行时不能切换到未打包能力。多输入、多输出、非 Float32、请求池、缓存及模型包准备生命周期仍属于后续增量。

后端专用对象只能出现在各自实现中，不进入 `core`、`vision` 或公共 Pipeline API。推理平台族由 CMake 在配置期唯一确定，部署配置不得切换到未编入程序的后端，也不进行静默回退。平台 Profile 声明支持的设备类型、精度、动态 shape 和内存能力，Pipeline 与模型清单据此尽早拒绝不兼容组合。

纯 OpenCV 算法不实现 `IInferenceBackend`，避免把图像和强类型结果伪装成模型张量。它通过 `IOpenCvAlgorithm<ResultType>` 接入独立的 `OpenCvPipeline<ResultType>`。

模型准备支持两种模式：

- `BuildIfMissing`：目标机使用已发行的后端 C++ Runtime 首次编译 IR 并写入缓存；不依赖 Python 或 `vision-modelc`。
- `CacheOnly`：生产环境只加载预构建缓存，缓存缺失即报错。

缓存键至少包含模型制品 SHA-256、后端及版本、驱动、设备信息、精度和构建参数。预编译制品只能在 manifest 与目标环境兼容时导入。

### 3.5 postprocess

后处理模块把原始 `TensorMap` 转换为标准强类型结果。当前 `AnomalyPostprocessor` 读取指定 Float32 输出的首元素作为图像级 score，并按可配置阈值生成 `AnomalyResult`。热力图缩放和缺陷区域提取尚未实现，随后再增加分类和检测。

自定义任务可以实现 `IPostprocessor<ResultType>`，也可以直接取得原始 `TensorMap`。

### 3.6 pipeline

`IVisionPipeline<ResultType>` 是同步视觉任务的统一入口：

```text
IVisionPipeline<ResultType>
	|-- ModelPipeline<ResultType>
	|     IPreprocessor -> IInferenceBackend -> IPostprocessor<ResultType>
	|
	`-- OpenCvPipeline<ResultType>
				IOpenCvAlgorithm<ResultType>
```

`ModelPipelineBuilder` 和 `OpenCvPipelineBuilder` 负责类型正确的组装。Executor 和 Runtime 只依赖 `IVisionPipeline<ResultType>`，无需判断任务由推理引擎还是 OpenCV 实现。模型路径使用 `TensorMap` 作为前处理、推理和后处理之间的数据契约；OpenCV 路径直接读取 `PipelinePacket` 并返回强类型结果。

首版不实现 DAG、多模型串并联和运行时插件系统，避免过早引入图调度及稳定 ABI 问题。

Pipeline 使用 move-only `PipelinePacket` 在线性阶段间移交图像，不允许隐式复制 `Frame`。图像缓冲采用两段生命周期：

```text
Camera Frame
	-> crop/copy 完成
	-> 释放相机池槽位

Business Frame
	-> inference / postprocess / heatmap render
	-> 释放业务池槽位
```

默认所有权策略为：

- 相机 Frame 在 `AfterImagePreparation` 释放。
- 裁剪图、复制图或业务显示图在 `AfterPostprocess` 释放。

在当前节点链中，`ToTensorNode` 写完最终池化 Tensor 后调用 `completeImagePreparation()` 并释放工作 Frame，因此后续 `NormalizeNode` 不再持有中间图像 Buffer。Tensor 自身持有张量池 lease，直到推理阶段不再引用该 Tensor 时自动归还槽位。

`BusinessFramePool` 按固定宽高、像素格式和 row stride 预分配业务图像。前处理直接写入取得的业务 Frame，随后同一地址通过 move 传递到推理和后处理，避免阶段间再次复制。

如果业务图只是相机 Buffer 上的零拷贝裁剪视图，它仍持有相机 lease，相机槽位会延长到业务图释放；需要在图像准备后立即归还相机槽位时，必须将裁剪结果写入独立业务池。

### 3.7 executor

Executor 在 Pipeline 之上提供在线调度：

当前调度层：

- `RuntimeSession<ResultType>` 是 `RuntimeFactory` 返回的整次运行门面，统一提供 `start()`、非阻塞 `requestStop()` 和同步 `wait()`，业务代码不需要持有具体 Executor。
- `FrameExecutor<ResultType>` 只拥有 `IFrameSource` 和注入的 `IPipelineExecutor<ResultType>`，负责帧数/时长停止条件、源错误策略与运行统计，不包含 Pipeline 构造和队列配置。
- `IPipelineExecutor<ResultType>` 统一 `submit()`、`requestStop()` 与 `wait()`；`SerialPipelineExecutor` 使用单执行线程按 FIFO 调用整体 `run()`。
- `ParallelPipelineExecutor` 依赖 `IStagedVisionPipeline<ResultType>`，preprocess、inference 和 postprocess 各由一个专属线程执行，不同任务可以在不同阶段重叠。
- `ExecutorTask<ResultType>` 集中管理 task ID、PipelinePacket、取消状态、future 和 callback；串行与并行执行器不重复实现任务状态机。
- `submit()` 支持多业务线程并发调用；固定容量入口队列满时返回 `QueueFull`。
- `TaskHandle` 提供 task ID、任务状态、shared future 和取消请求。
- 独立完成线程设置 future 并触发回调；Pipeline 与回调异常均不会逃出线程入口。
- 平滑停止排空已接受任务；立即停止标记正在运行和排队任务为取消，并保持提交顺序完成。执行中的 Pipeline 调用不被抢占。
- `submit()` 可以由多个业务线程并发调用，因此入口使用线程安全的固定容量有界提交队列，不假定单生产者。
- 并行执行器的 preprocess→inference、inference→postprocess、postprocess→completion 三条边均为固定容量 FIFO SPSC 环形队列；队列满时阻塞生产阶段，使慢推理或慢回调的背压逐级传到入口。
- 只有 `submit()` 在入口容量不足时返回 `QueueFull`；已经接受的任务不得因中间队列满载而丢失。
- `TaskHandle` 持有 future、任务状态和 task ID。
- `CompletionDispatcher` 在独立线程完成 future 并触发业务回调，业务回调不占用流水线阶段线程。
- 单通道各阶段保持 FIFO，结果严格按提交顺序交付；首版不提供完成即交付模式。
- `TimedPipeline` 当前记录 pre、infer、post、stage、wait 和端到端 latency，并在批次结束时输出 wall-clock 总耗时及 FPS；P50/P95/P99 聚合尚未实现。
- 平滑停止拒绝新任务并排空已接受任务；立即停止取消所有未开始任务，正在执行的阶段允许完成后停止向下游交付。
- 未开始任务可以取消；执行中的任务只记录取消请求，不抢占后端调用，并在安全边界停止后续阶段或结果交付。
- 阶段函数和业务回调产生的异常必须转换为失败状态，不能逃出线程入口。
- 所有工作线程和用户 callback 只能请求停止；只有外部控制线程调用 `wait()` 并按 Source、Executor 内部阶段、CompletionDispatcher、Timer 的所有权层级回收线程。
- 框架不提供 shutdown timeout、线程强杀或进程终止。不可取消的第三方调用或永久阻塞 callback 会使 `wait()` 与析构持续阻塞，最终强制退出由应用或操作系统负责。

`IVisionPipeline` 保留整体 `run()` contract；可阶段化的模型 Pipeline 额外实现 `IStagedVisionPipeline`。`RuntimeFactory` 在选择并行策略时验证该 contract，不识别具体 Pipeline 类型；OpenCV 单阶段 Pipeline 配置并行策略时返回配置错误。工厂通过 `createRuntime()` 组合 source、pipeline、部署策略和帧执行选项，通过 `createExecutor()` 保留只构造调度层的高级入口。

提交异步任务时通过 move-only `Frame` 明确移交图像。底层 `TensorBuffer` 使用 lease 保证异步阶段访问期间内存有效；最后一个 Frame/Tensor 视图释放时自动归还所属池。`PipelineOwnershipOptions` 可分别配置相机帧和业务帧的释放阶段。

该设计以稳定、可预测的资源占用为首版目标。若性能数据证明单通道吞吐不足，后续可以复制完整的 `PipelineRunner` 形成多通道；每个通道仍保持 SPSC 和单后端实例所有权，由 Executor 在通道间分发任务。多通道引入任务越序后，再增加有序重排和完成即交付策略。

### 3.8 config

配置模块只负责加载、校验并生成强类型配置，不参与单帧推理：

- `ModelManifest` 描述模型输入输出语义、前后处理参数和资源文件。
- `DeploymentConfig` 描述编译期平台族内的设备实例、精度、缓存、线程、队列及交付顺序，不能改变相机 SDK 或推理平台族。
- `ExecutorConfig` 描述串行或阶段并行性能策略、入口满载策略、入口容量和阶段队列容量。
- JSON Schema 在创建 Runtime 前完成字段、类型、范围和版本校验。
- `config::ConfigLoader` 解析 JSON；`runtime::RuntimeFactory` 根据强类型配置选择具体 `IPipelineExecutor`，并将帧源与执行器组装为 `RuntimeSession`。

模型语义与机器部署策略分离。模型包可以携带多个目标制品，但运行时只选择与编译期 Profile 和实际设备兼容的制品。

### 3.9 camera

`IFrameSource` 抽象开始、停止和 move-only 帧回调。图像源内部操作 `TensorBuffer`，但公共边界始终交付包含宽高、像素格式、row stride 和采集元数据的 `Frame`；前处理再把 `Frame` 转换为模型所需的 `Tensor`。

`FileSource` 是基于文件夹的异步图像序列源：

- `create()` 校验目录并一次性枚举文件；扩展名匹配不区分大小写，支持普通或递归扫描。
- 默认识别 BMP、JPEG、PNG 和 TIFF，也允许通过 `FileSourceOptions::extensions` 指定其他 OpenCV 可解码格式。
- 文件按字典序或最后修改时间排序，可配置循环播放与帧间隔。
- `start()` 创建工作线程，依次解码文件并通过 `FrameCallback` 移交 `Result<Frame>`；单个文件解码失败时回调错误，后续文件继续处理。
- 解码结果支持 Gray8、Gray16、Float32Gray、BGR8 和 BGRA8。`TensorBuffer` 共享持有 `cv::Mat` 的生命周期，创建 `Frame` 时不再复制像素数据。
- 文件帧的 `sequenceNumber` 从零递增，`capturedAt` 记录实际解码交付时间。`requestStop()` 只请求停止并唤醒帧间隔等待，`wait()` 由外部控制线程调用并回收工作线程。

OpenCV 仅存在于 `FileSource` 的 `.cpp` 实现和私有链接依赖中，不泄漏到 `IFrameSource`、`Frame`、`TensorBuffer` 等公共 API。当前从源码最小构建 `core`、`imgproc` 和 `imgcodecs` 模块，并关闭 FileSource 不需要的 ADE、FFmpeg、GStreamer 和 IPP。

`FrameBufferPool` 为实时相机采集提供固定容量槽位，并复用 `core::TensorBufferPool` 的 lease 归还机制。海康机器人 MVS 适配器后续负责断线重连、触发模式和厂商缓冲区释放；相机 SDK 只存在于适配器实现，不传入核心 API。视频文件源尚未实现，应与目录 `FileSource` 分开建模，避免混合有限图像序列和连续媒体流语义。

相机 SDK 由 CMake 配置期选择，首个值为 `HIK_MVS`。生成的相机 Profile 暴露硬件触发、外部 Buffer、像素格式等编译期能力，供 Runtime 与 Pipeline 约束组合；具体型号和序列号保持运行时可配置，启动时验证设备能力是否满足 Profile 和业务要求。

### 3.10 logs

日志模块定义轻量日志门面和指标结构。框架记录后端选择、模型缓存命中、错误上下文以及阶段耗时，但不记录图像数据。具体日志库作为实现细节，避免泄漏到公共 API。

`benchmark::TimedPipeline<ResultType>` 装饰具体 `pipeline::Pipeline<ResultType>`，测量 preprocess、inference、postprocess 的纯执行时间。`stage` 为三阶段执行时间之和，`wait` 为阶段队列等待时间，`latency` 为任务从 preprocess 开始到结果完成的端到端延迟。批次指标记录 wall-clock 总耗时、完成/失败数和 FPS。标准输出使用带阶段名的可读文本，文件输出保持 CSV。

## 4. 依赖规则

```text
common
	^
core
  ^
  ├── vision
  ├── logs
  └── backends
	^
vision ─┼── preprocess
	├── postprocess
	└── camera

core + vision + preprocess + backends + postprocess
		      ^
		   pipeline
		      ^
	     executor / config
```

约束：

1. `core` 不依赖其他业务模块。
2. `executor` 不理解视觉算法和具体后端。
3. `config` 不参与运行时逐帧处理。
4. `vision` 不管理线程，也不依赖具体推理 SDK。
5. `pipeline` 不依赖相机，相机只是 Frame 的来源之一。
6. GUI 只能依赖公共 Runtime API，核心模块不得依赖 GUI。

## 5. 公共接口草图

```cpp
class IInferenceBackend {
public:
    virtual ~IInferenceBackend() = default;
    virtual Result<void> prepare(const ModelSource&, const PrepareOptions&) = 0;
    virtual Result<TensorMap> infer(const TensorMap&) = 0;
};

class IPipelineExecutor {
public:
	virtual Result<TaskHandle> submit(
		PipelinePacket packet, CompletionCallback callback = {}) = 0;
	virtual void stop(StopMode mode) noexcept = 0;
};
```

所有可能失败的 API 返回 `Result<T>`，异常不跨 SDK 边界传播。错误需要包含稳定错误码、可读消息和可选底层后端上下文。

## 6. 构建 Profile、模型包与转译

### 6.1 编译期硬件选择

CMake 是相机 SDK 与推理平台族的唯一选择入口：

```cmake
vision_add_runtime(inspectionRuntime
	CAMERA HIK_MVS
	PLATFORM OPENVINO_INTEL
)
```

配置过程负责校验组合、选择源文件和 imported targets，并生成 `buildProfile.hpp`。生成头文件提供 `SelectedCamera`、`SelectedPlatform` 和 `SelectedRuntime` 等类型别名；业务代码不重复声明厂商，也不直接包含厂商 SDK。`OPENVINO_INTEL` 是平台族；当前 sample 在构建时进一步固定一个 CPU、GPU 或 NPU 部署设备并只携带对应插件。未来通用发布包若需要运行时设备切换，必须显式打包允许的插件集合并按 Profile 校验，不进行静默回退。

当前第一阶段已实现 `VISION_CAMERA_SDK`、`VISION_INFERENCE_PLATFORM` 缓存入口、`vision_add_runtime` 参数校验，以及 `config/buildProfile.hpp` 中的 `SelectedCamera`、`SelectedPlatform` 和能力描述。`NONE/NONE` 是无厂商 SDK 的核心测试 Profile。当前 `NONE`/`HIK_MVS` 与 `NONE`/`OPENVINO_INTEL` 的四种组合均受支持；选择厂商项时配置过程要求对应 SDK 完整可用。OpenVINO、TensorRT、ONNX Runtime 和海康 MVS 通过独立 imported target 隔离，未选择的厂商依赖不参与编译和链接。`SelectedRuntime` 和按 Profile 选择具体适配器源文件要在对应适配器可构造后接入，不能提前生成空壳厂商对象。

新增相机或推理平台时必须提供独立适配器、CMake 依赖目标、能力 Profile 和契约测试，不使用预处理宏把多家 SDK 分支散布到公共代码。

### 6.2 模型包与运行时缓存

```text
model-package/
├─ manifest.json
├─ model.xml
├─ model.bin
├─ build-lock.json
└─ assets/
   ├─ labels.json
   └─ dictionary.txt
```

`manifest.json` 定义模型语义、输入输出契约和兼容目标；`build-lock.json` 记录源 ONNX 哈希、转译工具及厂商工具版本。源 ONNX 可选择归档，但不是目标机运行的必需文件。

运行时编译产物不回写模型包，而写入机器本地缓存：

```text
cache/
├─ openvino/<cache-key>/
└─ tensorrt/<cache-key>/model.engine
```

`manifest.json` 跟随模型发布，定义模型语义；`deployment.json` 位于部署环境，定义运行策略。二者均携带 schema version，未知主版本必须拒绝加载。

### 6.3 `vision-modelc`

`vision-modelc` 是独立的 Python 开发工具，不进入 Runtime 安装包。当前基础版本调用 OpenVINO 官方转换 API 生成 `.xml + .bin` IR，校验可选输入输出名，并记录源模型、制品哈希、OpenVINO 版本和端口信息。完整模型包 manifest 校验仍属于后续增量。职责为：

- 校验 ONNX 的输入输出、布局、数据类型、静态 shape 及 manifest 一致性。
- 调用 OpenVINO 官方转换能力生成 IR，而不自行实现模型格式转换。
- 生成 `manifest.json`、`build-lock.json` 和制品哈希，保证转译过程可追溯。
- 可选使用 ONNX Runtime 与目标后端比较基准输入输出。
- 接受训练侧提供的量化 ONNX，但不负责 INT8 校准或量化。

目标机的 `BuildIfMissing` 由 OpenVINO C++ Runtime 执行 `read_model()` 与 `compile_model()`，因此只需携带 OpenVINO Runtime 和所选设备插件，不携带 Python、ONNX 转换器或 `vision-modelc`。`CacheOnly` 使用 `import_model()` 加载兼容环境预编译的制品，缺失或环境不匹配时明确失败。

## 7. 工程和验证策略

- 代码和文件命名遵循 [codingConventions.md](codingConventions.md)：C++ 类型使用 UpperCamelCase，函数、变量和项目文件名使用 lowerCamelCase。
- C++20、CMake、MinGW-w64，静态库优先；项目 preset 固定编译器和构建目录。
- CMake 配置期选择唯一相机 SDK 与推理平台族，并生成构建 Profile；运行时配置不能绕过该边界。
- 不使用 vcpkg；OpenCV、JSON、日志和测试框架等依赖直接下载到 `Thirdparty/<package>/<version>`。
- OpenVINO、TensorRT、ONNX Runtime 和海康 MVS 通过 CMake imported target 隔离厂商 SDK。
- 单元测试覆盖纯逻辑；所有后端运行同一套 contract tests。
- 生命周期测试覆盖 Block 提交唤醒、回调线程请求停止、平滑排空、立即取消和按所有权顺序 join；`wait()` 返回后汇总数据不再变化。
- 使用 Python 参考结果验证数值正确性，并为浮点误差设定明确容差。
- benchmark 输出各阶段 P50/P95/P99、吞吐、峰值内存和缓存命中情况。
- 稳定性测试覆盖队列满载、坏模型、错误 shape、回调异常、相机断线和长时间运行。

当前 MinGW Debug 全量 76 项测试通过；MinGW Release 独立消费者 `anomalyDirectorySample` 使用 80 张目录图像（27 NG、53 OK）完成端到端运行，并在有限 Source 结束后平滑回收 Source、Executor stages 和 CompletionDispatcher 线程。

## 8. 暂不纳入首版

- 动态 batch、动态高宽和自动合批。
- GPU 前后处理、跨后端零拷贝和 CUDA stream 编排。
- 多通道 PipelineRunner、结果重排和完成即交付。
- DAG、多模型串并联和条件分支。
- 运行时 DLL/Python 插件及稳定插件 ABI。
- 在 Runtime 发行包中携带 Python、模型转换器或 INT8 校准环境。
- 自动后端回退和可视化流程编辑器。

这些能力应在首版接口和性能数据稳定后，根据真实模型接入需求逐项引入。
