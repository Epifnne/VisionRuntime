# VisionRuntime

面向工业视觉模型部署的 C++20 SDK。

## 目标使用方式

VisionRuntime 计划在 CMake 配置期显式选择相机 SDK 和推理平台族，由构建系统只编译、链接对应适配器，并生成供业务代码使用的构建 Profile。首个组合为海康机器人 MVS 与 OpenVINO Intel 平台，推理设备覆盖 CPU、GPU 和 NPU；具体相机型号、序列号、曝光参数及模型路径仍在运行时配置和校验。

```cmake
vision_add_runtime(inspectionRuntime
	CAMERA HIK_MVS
	PLATFORM OPENVINO_INTEL
)
```

业务代码通过生成的 `SelectedRuntime` 组装强类型前后处理节点和模型，不包含 MVS 或 OpenVINO 的厂商类型。CMake 是硬件后端选择的唯一事实源，避免业务代码与构建脚本重复声明。

当前仓库已实现构建 Profile 的第一阶段。配置根项目时通过缓存参数显式选择：

```powershell
cmake --preset mingw-debug `
	-DVISON_CAMERA_SDK=HIK_MVS `
	-DVISON_INFERENCE_PLATFORM=OPENVINO_INTEL
```

CMake 会生成 `config/buildProfile.hpp`，其中包含 `SelectedCamera`、`SelectedPlatform`、稳定枚举、名称及硬件能力。默认值为 `NONE/NONE`，用于不安装厂商 SDK 的核心开发和测试。海康 MVS、OpenVINO、TensorRT 和 ONNX Runtime 均通过隔离的 imported target 接入，只有选中对应 Profile 或后端时才解析 SDK。具体适配器和 `SelectedRuntime` 仍属于后续实现，当前 Profile 选择不会虚构厂商对象。

模型转译由独立 Python CLI `vision-modelc` 完成。当前基础版本在开发或发布环境中转换 ONNX、校验可选端口名，并生成 OpenVINO IR 和可复现的构建记录，但不进入目标机运行时发行包；manifest 校验仍属于后续增量。目标机使用 OpenVINO C++ Runtime 将 IR 首次编译到实际的 Intel CPU、GPU 或 NPU。首版不执行 INT8 校准，只接受训练侧提供的量化 ONNX。

```powershell
python -m pip install openvino
python Runtime/tools/vision-modelc.py model.onnx `
	-o Build/ModelArtifacts/model.xml `
	--input-name images --output-name score
```

命令同时生成 `model.bin` 和 `model.build.json`。默认保留 FP32 权重；只有确认精度可接受时才使用 `--compress-to-fp16`。IR 可跳过 ONNX frontend 转换，但设备相关图编译仍由目标机 OpenVINO CPU/GPU/NPU 插件完成，因此主要稳定并缩短模型加载阶段，单帧推理提升需要在目标设备上实测。

## 当前基础能力

- 后端无关的 `Status`、`Result<T>`、`Tensor`、shape、stride 和设备描述。
- `TensorBuffer` 与固定容量 `TensorBufferPool`，支持共享 lease 和自动回池。
- move-only `Frame`，避免流水线阶段间隐式复制图像。
- 独立的相机 `FrameBufferPool` 与业务 `BusinessFramePool`。
- 相机图像准备完成后释放相机 Buffer，业务图像保留到热力图等后处理完成。
- 异步目录 `FileSource`，按顺序解码文件夹图像并以 move-only `Frame` 回调交付。
- 类型状态化可组合前处理链，编译期校验节点顺序和唯一物化边界。
- CMake 配置期相机 SDK/推理平台选择，以及生成的强类型 `BuildProfile` 和能力描述。
- 池化 Float32 NCHW 张量直写与原地归一化，避免临时 Tensor 和处理后的整块复制。
- OpenVINO 单输入/单输出 Float32 同步后端，以及标量异常分数阈值后处理。
- `vision-modelc` ONNX 到 OpenVINO IR 转译工具，输出制品哈希和端口信息构建记录。
- `anomalyDirectorySample` 文件夹异常检测示例，逐图输出 score 和 OK/NG。

## 文件夹图像源

`FileSource` 在创建时扫描目录，在启动后由工作线程依次解码图像。内部使用 OpenCV，但公共接口只暴露 `Frame`、`Result` 和标准库类型。

```cpp
#include "camera/fileSource.hpp"

#include <chrono>
#include <memory>
#include <utility>

using namespace visionRuntime;

core::Result<std::unique_ptr<camera::FileSource>> startImageDirectory() {
	camera::FileSourceOptions options;
	options.directory = "images";
	options.recursive = true;
	options.loop = false;
	options.frameInterval = std::chrono::milliseconds(100);

	auto sourceResult = camera::FileSource::create(std::move(options));
	if (!sourceResult) {
		return sourceResult;
	}

	auto source = std::move(sourceResult).value();
	auto started = source->start([](core::Result<vision::Frame> frame) {
		if (!frame) {
			return;
		}
		// 将 std::move(frame).value() 提交给 Pipeline。
	});
	if (!started) {
		return core::Result<std::unique_ptr<camera::FileSource>>::failure(
			started.status());
	}
	return core::Result<std::unique_ptr<camera::FileSource>>::success(
		std::move(source));
}

// 持有返回的 FileSource；退出前调用 stop() 并检查结果。
// 析构函数也会等待工作线程结束。
```

默认扩展名为 `.bmp`、`.jpeg`、`.jpg`、`.png`、`.tif` 和 `.tiff`，匹配不区分大小写。可配置递归扫描、循环播放、帧间隔，以及字典序或最后修改时间排序。当前支持 Gray8、Gray16、Float32Gray、BGR8 和 BGRA8 解码结果；无法解码或不支持的文件通过 callback 返回失败 `Result<Frame>`，不会阻止后续文件处理。

每个 `Frame` 的 `TensorBuffer` 共享持有 OpenCV 解码内存，因此 callback 返回后像素仍然有效，直到最后一个 Frame/Buffer 视图释放。文件源输出 `Frame`，不是裸 `TensorBuffer`；模型输入 `Tensor` 由前处理阶段生成。

## 可组合前处理

`preprocess` 和 `postprocess` 在项目命名中各自视为一个单词：目录与命名空间使用全小写，类型使用 `Preprocess`、`Postprocess` 词形。

当前前处理链由独立 Frame 节点、Frame 到 Tensor 的物化节点和后续 Tensor 节点组成：

```cpp
#include "preprocess/frameNodes/centerCropNode.hpp"
#include "preprocess/frameNodes/resizeNode.hpp"
#include "preprocess/frameNodes/toTensorNode.hpp"
#include "preprocess/preprocessChain.hpp"
#include "preprocess/tensorNodes/normalizeNode.hpp"

using namespace visionRuntime;

preprocess::ToTensorOptions imageOptions;
imageOptions.tensorName = "image";
imageOptions.bufferCount = 2;

preprocess::NormalizeOptions normalizeOptions;
normalizeOptions.scale = 1.0F / 255.0F;

auto preprocessor = preprocess::PreprocessBuilder::start<vision::Frame>()
	.then(preprocess::Resize::shortSide(720))
	.then(preprocess::CenterCrop({640, 640}))
	.then(preprocess::ToTensor(std::move(imageOptions)))
	.then(preprocess::Normalize(std::move(normalizeOptions)))
	.build();
```

`Resize` 将短边缩放到目标尺寸，并把保持原像素格式的 8-bit Frame 写入节点自己的 BufferPool；`CenterCrop` 在该 Frame 上建立零拷贝中心裁剪视图；`ToTensor` 只负责将当前 Gray8、Bgr8 或 Bgra8 Frame 转换为 RGB、Float32 NCHW Tensor。写入完成后释放工作 Frame；`Normalize` 随后自动读取当前 Tensor 并原地归一化。所有操作节点遵循统一的 `PreprocessNode` 协议，Builder 只提供一个泛型 `then()`；参数校验和 BufferPool 创建错误由 `build()` 统一返回。

## 文件夹异常检测示例

[`Samples/anomalyDirectory`](Samples/anomalyDirectory) 是独立业务工程，不属于框架内部构建。它模拟框架使用者将完整 VisionRuntime 源码包放在业务工程 `Thirdparty/VisionRuntime` 下，再通过 target 级 API 声明部署需求：

```text
MyInspection/
├─ CMakeLists.txt
├─ main.cpp
└─ Thirdparty/
   └─ VisonRuntime/
```

业务 `CMakeLists.txt` 的核心只有：

```cmake
add_subdirectory(Thirdparty/VisionRuntime)

vison_target_runtime(myInspection
	PLATFORM OPENVINO_INTEL
	DEVICE CPU
	ARTIFACT ONNX
)
```

当目标尚未创建时，`vision_target_runtime` 会自动使用同目录下的
`myInspection.cpp` 创建 executable；已有 target 也可直接绑定。业务源码只需引用
聚合头，不需要了解后端、Pipeline 或前后处理节点的头文件位置：

```cpp
#include <visionruntime>
```

框架自动链接所需 Runtime、传播 C++20、解析 OpenVINO，并精准部署 CPU 与 ONNX 对应的运行库。初版约定模型只有一个 Float32 输入和一个 Float32 输出，输出首元素为图像级异常分数，`score >= threshold` 判定为 NG。

```powershell
cmake -S Samples/anomalyDirectory -B Build/SampleConsumer -G Ninja `
	-DVISON_RUNTIME_ROOT=<path-to-VisonRuntime> `
	-DCMAKE_PREFIX_PATH=<openvino-package>
cmake --build Build/SampleConsumer --target anomalyDirectorySample

Build/SampleConsumer/bin/anomalyDirectorySample.exe `
	<model.onnx> <image-directory> 2.0
```

`VISON_RUNTIME_ROOT` 只在当前仓库内验证 sample 时用于指向框架位置；复制成真实业务工程后，默认位置就是 `Thirdparty/VisonRuntime`。构建 sample 后，CMake 只复制公共 OpenVINO Runtime、所选设备插件、ONNX/IR frontend、TBB 和 MinGW 运行库。缺失必需 DLL 会在配置期报错；设备和模型类型不能通过命令行切换到未打包能力。

示例通过 `PreprocessBuilder` 组合 `Resize`、`CenterCrop`、`ToTensor` 和 `Normalize`，实现 PatchCore 所需的短边缩放到 256、中心裁剪 224、RGB、Float32 NCHW 和 ImageNet mean/std 前处理。`benchmark::TimedPipeline` 测量各阶段耗时，sample 将 sequence、score、判定和耗时 CSV 写到标准输出，不会自动创建文件；需要保存时可执行 `anomalyDirectorySample.exe ... > result.csv`。使用参考工程已知 NG 图验证时，sample 输出 `2.91676521`，Python 参考结果为 `2.90172958`，相对误差约 0.52%，阈值 2.0 下均判定为 NG。剩余误差主要来自 Pillow 与 C++ resize 的像素取整差异。

## 图像所有权

当前定时拍照 Pipeline 采用两段缓冲生命周期：

```text
Camera Frame -> crop/copy -> release camera buffer
Business Frame -> infer -> postprocess/heatmap -> release business buffer
```

`PipelinePacket` 只能移动，不能复制。默认配置在裁剪或复制完成后归还相机槽位，并将业务 Frame 的同一内存地址继续移交给推理和后处理。释放阶段可通过 `PipelineOwnershipOptions` 调整。

零拷贝裁剪视图会继续持有相机 Buffer；若需要尽早归还相机槽位，应从 `BusinessFramePool` 获取目标 Frame，并在前处理阶段直接写入。

## 执行模型

运行时同时提供同步 `run()` 和异步 `submit()`。当前 `PipelineExecutor<ResultType>` 在 `IVisionPipeline<ResultType>` 之上提供单执行通道：并发调用的 `submit()` 进入线程安全的固定容量队列，执行线程按 FIFO 调用 `run()`，独立完成线程设置 shared future 并触发业务回调。入口满载时返回 `QueueFull`，Pipeline 或回调异常不会逃出工作线程。

`TaskHandle` 提供 task ID、状态、shared future 和取消请求。任务按提交顺序交付；平滑停止会排空已接受任务，立即停止会按顺序取消正在运行及排队任务，并拒绝新提交。执行中的后端调用不被抢占，其结果会在安全边界替换为 `Cancelled`。

当前公共 Pipeline 接口只暴露整体 `run()`，因此尚未实现前处理、推理、后处理的三线程重叠执行及阶段间 SPSC 队列。该阶段化能力需要先增加统一的可分阶段 Pipeline contract，且不能让 Executor 判断具体 Pipeline 类型。详细边界见 [Docs/architecture.md](Docs/architecture.md#37-executor)。

## 构建环境

- Windows
- Qt MinGW-w64 13.1 (`D:/Qt/Tools/mingw1310_64`)
- CMake 3.25 或更高版本
- Ninja

项目当前仅支持 MinGW，不使用 vcpkg。第三方依赖直接放入 `Thirdparty/<package>/<version>`，具体规则见 [Thirdparty/README.md](Thirdparty/README.md)。

首次配置前可构建 `bootstrapDependencies` target，按固定提交下载或核验 OpenCV、GoogleTest、nlohmann/json 和 spdlog。项目源码启用 `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`。

目录图像源从 `Thirdparty/opencv/4.12.0` 源码最小构建 `core`、`imgproc` 和 `imgcodecs`。首次构建会编译这些模块，耗时会明显高于后续增量构建。

## 构建与测试

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

所有构建产物写入 `Build`。

当前共有 50 个单元测试，覆盖构建 Profile、基础结果类型、Tensor 视图、缓冲池、池耗尽、非法布局、Pipeline 与 Executor 行为、跨阶段图像生命周期、目录图像解码、前处理数值、异常分数后处理、张量池 lease 和节点组合约束。
