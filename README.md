# VisonRuntime

面向工业视觉模型部署的 C++20 SDK。

## 当前基础能力

- 后端无关的 `Status`、`Result<T>`、`Tensor`、shape、stride 和设备描述。
- `TensorBuffer` 与固定容量 `TensorBufferPool`，支持共享 lease 和自动回池。
- move-only `Frame`，避免流水线阶段间隐式复制图像。
- 独立的相机 `FrameBufferPool` 与业务 `BusinessFramePool`。
- 相机图像准备完成后释放相机 Buffer，业务图像保留到热力图等后处理完成。
- 异步目录 `FileSource`，按顺序解码文件夹图像并以 move-only `Frame` 回调交付。
- 类型状态化可组合前处理链，编译期校验节点顺序和唯一物化边界。
- 池化 Float32 NCHW 张量直写与原地归一化，避免临时 Tensor 和处理后的整块复制。

## 文件夹图像源

`FileSource` 在创建时扫描目录，在启动后由工作线程依次解码图像。内部使用 OpenCV，但公共接口只暴露 `Frame`、`Result` 和标准库类型。

```cpp
#include "camera/fileSource.hpp"

#include <chrono>
#include <memory>
#include <utility>

using namespace visonRuntime;

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

当前前处理链由一个图像物化节点和后续 Tensor 节点组成：

```cpp
#include "preprocess/fusedImageToTensorNode.hpp"
#include "preprocess/preprocessChain.hpp"
#include "preprocess/tensorNormalizeNode.hpp"

using namespace visonRuntime;

preprocess::FusedImageToTensorOptions imageOptions;
imageOptions.inputName = "image";
imageOptions.width = 640;
imageOptions.height = 640;
imageOptions.bufferCount = 2;
auto imageToTensor = preprocess::FusedImageToTensorNode::create(imageOptions);

preprocess::TensorNormalizeOptions normalizeOptions;
normalizeOptions.inputName = "image";
normalizeOptions.scale = 1.0F / 255.0F;
auto normalize = preprocess::TensorNormalizeNode::create(normalizeOptions);

auto preprocessor = preprocess::CameraFramePreprocessBuilder::start()
	.then(std::move(imageToTensor).value())
	.then(std::move(normalize).value())
	.build();
```

`FusedImageToTensorNode` 支持 Gray8、Bgr8 和 Bgra8 输入，执行双线性 resize、RGB 通道转换和 Float32 NCHW 排布，直接写入 `TensorBufferPool` 槽位。写入完成后释放相机 Frame；`TensorNormalizeNode` 随后在同一 Tensor 上原地归一化。Builder 在编译期要求物化后才能构建，并拒绝同一链中的第二个物化节点。

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

项目统一使用 MinGW，不使用 MSVC 或 vcpkg。第三方依赖直接放入 `Thirdparty/<package>/<version>`，具体规则见 [Thirdparty/README.md](Thirdparty/README.md)。

目录图像源从 `Thirdparty/opencv/4.12.0` 源码最小构建 `core`、`imgproc` 和 `imgcodecs`。首次构建会编译这些模块，耗时会明显高于后续增量构建。

## 构建与测试

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

所有构建产物写入 `Build`。

当前共有 47 个单元测试，覆盖基础结果类型、Tensor 视图、缓冲池、池耗尽、非法布局、Pipeline 与 Executor 行为、跨阶段图像生命周期、目录图像解码、前处理数值、张量池 lease 和节点组合约束。
