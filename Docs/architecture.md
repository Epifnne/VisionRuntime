# VisonRuntime 架构设计

## 1. 项目目标

VisonRuntime 是一个面向工业视觉模型部署的 C++20 SDK。模型训练人员提供 ONNX 模型、模型元数据，并使用框架内置或自定义的前后处理节点组合推理流水线；业务软件通过同步或异步 API 执行分类、检测、异常检测、OCR 和关键点等任务。

首要目标：

- 使用一套后端无关 API 支持 OpenVINO、TensorRT 和 ONNX Runtime。
- OpenVINO 主要面向 Intel CPU，TensorRT 面向 NVIDIA GPU，ONNX Runtime 用作通用执行后端和正确性基准。
- 前后处理可组合、可替换，并允许业务工程通过 C++ 实现自定义节点。
- 在线运行采用单通道分阶段异步流水线，具备有界队列、异步回调、有序交付、错误传播和分阶段性能统计。
- 公共接口保持清晰、强类型；当前提供 CPU 图像池化和阶段间零拷贝移交，并为后续动态批处理、设备内存及跨设备零拷贝保留扩展空间。

首版约束：Windows、MinGW-w64、C++20、CMake、静态库优先、固定 NCHW、batch 1、CPU 前后处理。首个纵向功能为 OpenVINO CPU 异常检测，模型输出异常分数和热力图。

## 2. 总体数据流

```text
FrameSource / cv::Mat
	|
	v
PipelineExecutor  -- 多生产者有界提交队列、任务 ID、future、回调
	|
	v
PreProcess Thread  -- resize、颜色转换、归一化、layout 转换
	|
	v  有界 SPSC 队列
Inference Thread  -- ORT / OpenVINO / TensorRT
	|
	v  有界 SPSC 队列
PostProcess Thread -- 阈值、坐标还原、热力图、区域提取
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
├─ include/
│  ├─ core/          # 无视觉业务语义的基础类型
│  ├─ vision/        # 图像、几何、变换上下文和标准结果
│  ├─ preProcess/    # 前处理节点接口与内置节点
│  ├─ backends/      # 统一后端接口及各后端声明
│  ├─ postProcess/   # 后处理节点接口与标准任务实现
│  ├─ pipeline/      # 线性流水线、构建器和运行时门面
│  ├─ executor/      # 队列、任务、线程和结果交付
│  ├─ config/        # 模型清单、部署配置、校验和对象构造
│  ├─ camera/        # 图像源抽象及海康 MVS 适配器
│  ├─ logs/          # 日志接口和运行指标
│  └─ gui/           # 预留，不属于首版核心 SDK
├─ src/              # 对应模块的非公开实现
├─ tests/            # 单元、契约、集成和稳定性测试
├─ samples/          # 模型接入及相机调用示例
└─ tools/            # 模型检查、缓存预编译和 benchmark
```

### 3.1 core

`core` 是最低层模块，只提供通用能力：

- `Status`、错误码和 `Result<T>`。
- `TensorBuffer` 描述底层存储的地址、容量、设备、内存类型、可写性和共享生命周期。
- `TensorBufferPool` 提供线程安全的固定容量可复用缓冲区；最后一个视图释放后槽位自动归还。
- `Tensor` 是 `TensorBuffer` 上的类型化多维视图，支持 shape、byte stride、offset、连续性和 subview。
- CPU/CUDA 等 `Device` 描述。
- 公共导出宏和基础类型。

`core` 不依赖 OpenCV、推理 SDK、配置解析器或执行器。

### 3.2 vision

`vision` 表达视觉领域数据：

- `Frame`：`TensorBuffer` 上的 move-only 图像视图，包含宽高、像素格式、行步长和采集元数据。
- `FrameMetadata`：序列号、采集时间和可选硬件时间戳。
- `TransformContext`：原图尺寸、网络尺寸、缩放、裁剪和填充信息。
- 强类型任务结果：分类、检测、异常、OCR 和关键点。
- `VisionResult`：标准任务结果的 `std::variant` 容器。

`TransformContext` 随前处理结果传递，后处理依靠它把检测框、关键点和热力图映射回原图。

### 3.3 preProcess

前处理模块把 `Frame` 转换为后端可消费的 `TensorMap`。节点采用线性组合，首版内置 resize、letterbox、颜色转换、归一化和 HWC/NCHW 转换。每次执行产生 `PreparedInput`，其中同时包含张量和 `TransformContext`。

自定义前处理通过实现 `IPreprocessor` 随业务程序一起编译，不在首版提供运行时 DLL ABI。

### 3.4 backends

`IInferenceBackend` 统一以下生命周期：

```text
prepare(ONNX, options) -> PreparedModel
infer(PreparedModel, TensorMap) -> TensorMap
```

- ONNX Runtime 直接创建 Session。
- OpenVINO 从 ONNX 读取模型并编译到指定设备。
- TensorRT 解析 ONNX 并构建或加载 Engine。

后端专用对象只能出现在各自实现中，不进入 `core`、`vision` 或公共 Pipeline API。部署配置显式选择后端，不进行静默回退。

模型准备支持两种模式：

- `BuildIfMissing`：开发环境允许首次编译并写入缓存。
- `CacheOnly`：生产环境只加载预构建缓存，缓存缺失即报错。

缓存键至少包含 ONNX SHA-256、后端及版本、设备信息、精度和构建参数。离线预编译工具与运行时复用同一套模型准备逻辑。

### 3.5 postProcess

后处理模块把原始 `TensorMap` 转换为标准强类型结果。首版实现异常分数、热力图缩放、阈值判断和缺陷区域提取，随后增加分类和检测。

自定义任务可以实现 `IPostprocessor<ResultType>`，也可以直接取得原始 `TensorMap`。

### 3.6 pipeline

Pipeline 只描述线性的执行顺序：

```text
IPreprocessor -> IInferenceBackend -> IPostprocessor
```

`PipelineBuilder` 负责类型正确的组装，`Runtime` 是面向 SDK 使用者的门面。首版不实现 DAG、多模型串并联和运行时插件系统，避免过早引入图调度及稳定 ABI 问题。

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

`BusinessFramePool` 按固定宽高、像素格式和 row stride 预分配业务图像。前处理直接写入取得的业务 Frame，随后同一地址通过 move 传递到推理和后处理，避免阶段间再次复制。

如果业务图只是相机 Buffer 上的零拷贝裁剪视图，它仍持有相机 lease，相机槽位会延长到业务图释放；需要在图像准备后立即归还相机槽位时，必须将裁剪结果写入独立业务池。

### 3.7 executor

Executor 在 Pipeline 之上提供在线调度：

- `PipelineRunner` 是单通道的三阶段流水线，前处理、推理和后处理各由一个专属线程串行执行本阶段任务。
- `submit()` 可以由多个业务线程并发调用，因此入口使用线程安全的固定容量有界提交队列，不假定单生产者。
- `PipelineRunner` 内相邻阶段通过固定容量、FIFO 的有界 SPSC 队列连接；内部队列满时阻塞生产阶段，使背压逐级传到入口。
- 只有 `submit()` 在入口容量不足时返回 `QueueFull`；已经接受的任务不得因中间队列满载而丢失。
- `TaskHandle` 持有 future、任务状态和 task ID。
- `CompletionDispatcher` 在独立线程完成 future 并触发业务回调，业务回调不占用流水线阶段线程。
- 单通道各阶段保持 FIFO，结果严格按提交顺序交付；首版不提供完成即交付模式。
- 记录排队、前处理、推理、后处理和总耗时的 P50/P95/P99。
- 平滑停止拒绝新任务并排空已接受任务；立即停止取消所有未开始任务，正在执行的阶段允许完成后停止向下游交付。
- 未开始任务可以取消；执行中的任务只记录取消请求，不抢占后端调用，并在安全边界停止后续阶段或结果交付。
- 阶段函数和业务回调产生的异常必须转换为失败状态，不能逃出线程入口。

提交异步任务时通过 move-only `Frame` 明确移交图像。底层 `TensorBuffer` 使用 lease 保证异步阶段访问期间内存有效；最后一个 Frame/Tensor 视图释放时自动归还所属池。`PipelineOwnershipOptions` 可分别配置相机帧和业务帧的释放阶段。

该设计以稳定、可预测的资源占用为首版目标。若性能数据证明单通道吞吐不足，后续可以复制完整的 `PipelineRunner` 形成多通道；每个通道仍保持 SPSC 和单后端实例所有权，由 Executor 在通道间分发任务。多通道引入任务越序后，再增加有序重排和完成即交付策略。

### 3.8 config

配置模块只负责加载、校验并生成强类型配置，不参与单帧推理：

- `ModelManifest` 描述模型输入输出语义、前后处理参数和资源文件。
- `DeploymentConfig` 描述后端、设备、精度、缓存、线程、队列及交付顺序。
- JSON Schema 在创建 Runtime 前完成字段、类型、范围和版本校验。
- `ConfigLoader` 解析 JSON，`RuntimeFactory` 根据强类型配置组装运行时对象。

模型语义与机器部署策略分离，模型包可在不同硬件上复用。

### 3.9 camera

`IFrameSource` 抽象开始、停止和 move-only 帧回调。`FrameBufferPool` 为采集提供固定容量槽位，并复用 `core::TensorBufferPool` 的 lease 归还机制。首版提供目录/视频源以及海康机器人 MVS 适配器。相机 SDK 只存在于适配器实现，不传入核心 API；断线重连、触发模式和厂商缓冲区释放由适配器管理。

### 3.10 logs

日志模块定义轻量日志门面和指标结构。框架记录后端选择、模型缓存命中、错误上下文以及阶段耗时，但不记录图像数据。具体日志库作为实现细节，避免泄漏到公共 API。

## 4. 依赖规则

```text
core
  ^
  ├── vision
  ├── logs
  └── backends
	^
vision ─┼── preProcess
	├── postProcess
	└── camera

core + vision + preProcess + backends + postProcess
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

class PipelineExecutor {
public:
    Result<TaskHandle> submit(Frame frame);
    Result<TaskHandle> submit(Frame frame, CompletionCallback callback);
    Result<VisionResult> run(Frame frame);
    void stop(StopMode mode);
};
```

所有可能失败的 API 返回 `Result<T>`，异常不跨 SDK 边界传播。错误需要包含稳定错误码、可读消息和可选底层后端上下文。

## 6. 模型包与部署配置

```text
model-package/
├─ manifest.json
├─ model.onnx
└─ assets/
   ├─ labels.json
   └─ dictionary.txt
```

运行时编译产物不写入模型包，而写入机器本地缓存：

```text
cache/
├─ openvino/<cache-key>/
└─ tensorrt/<cache-key>/model.engine
```

`manifest.json` 跟随模型发布，定义模型语义；`deployment.json` 位于部署环境，定义运行策略。二者均携带 schema version，未知主版本必须拒绝加载。

## 7. 工程和验证策略

- 代码和文件命名遵循 [codingConventions.md](codingConventions.md)：C++ 类型使用 UpperCamelCase，函数、变量和项目文件名使用 lowerCamelCase。
- C++20、CMake、MinGW-w64，静态库优先；项目 preset 固定编译器和构建目录。
- 不使用 vcpkg；OpenCV、JSON、日志和测试框架等依赖直接下载到 `Thirdparty/<package>/<version>`。
- OpenVINO、TensorRT、ONNX Runtime 和海康 MVS 通过 CMake imported target 隔离厂商 SDK。
- 单元测试覆盖纯逻辑；所有后端运行同一套 contract tests。
- 使用 Python 参考结果验证数值正确性，并为浮点误差设定明确容差。
- benchmark 输出各阶段 P50/P95/P99、吞吐、峰值内存和缓存命中情况。
- 稳定性测试覆盖队列满载、坏模型、错误 shape、回调异常、相机断线和长时间运行。

## 8. 暂不纳入首版

- 动态 batch、动态高宽和自动合批。
- GPU 前后处理、跨后端零拷贝和 CUDA stream 编排。
- 多通道 PipelineRunner、结果重排和完成即交付。
- DAG、多模型串并联和条件分支。
- 运行时 DLL/Python 插件及稳定插件 ABI。
- 自动后端回退和可视化流程编辑器。

这些能力应在首版接口和性能数据稳定后，根据真实模型接入需求逐项引入。
