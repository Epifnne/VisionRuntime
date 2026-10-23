# VisionRuntime

A C++20 SDK for deploying industrial vision models.

## Intended Usage

VisionRuntime selects the camera SDK explicitly during CMake configuration and generates a build profile for application code. Inference backends are separate C ABI plugins: the core runtime stays vendor-neutral, while deployment configuration selects the plugin directory, backend ID, and device at runtime without fallback.

```cmake
vision_target_runtime(inspectionApp
	CAMERA HIK_MVS
	BACKEND_PLUGINS OpenVino
)
```

Application code uses the aggregate header and presets without exposing vendor-specific MVS, OpenVINO, or TensorRT types. Camera capability selection remains compile-time through CMake; model artifacts and backend devices are runtime data supplied by the model package and deployment configuration.

Camera profile support is implemented through `VISION_CAMERA_SDK`. Backend plugins are independent build options and can be enabled only when their SDK is available:

```powershell
cmake --preset mingw-debug `
	-DVISION_CAMERA_SDK=HIK_MVS `
	-DVISION_BUILD_OPENVINO_PLUGIN=ON
```

CMake generates `config/buildProfile.hpp` for the selected camera only. The default camera is `NONE` for core development and testing without vendor SDKs. Hikrobot MVS, OpenVINO, and TensorRT are integrated through isolated imported targets. OpenVINO and TensorRT are available as runtime plugins; ONNX Runtime remains planned work.

Model translation is handled by the standalone Python CLI `vision-modelc`. The current base version converts ONNX in development or release environments, validates optional port names, and generates OpenVINO IR plus a reproducible build record. It is not included in the target runtime distribution; manifest validation remains planned work. On the target machine, the OpenVINO C++ Runtime compiles the IR for the actual Intel CPU, GPU, or NPU on first use. The initial version does not perform INT8 calibration and accepts only quantized ONNX supplied by training.

```powershell
python -m pip install openvino
python Runtime/tools/vision-modelc.py model.onnx `
	-o Build/ModelArtifacts/model.xml `
	--input-name images --output-name score
```

The command also generates `model.bin` and `model.build.json`. FP32 weights are preserved by default; use `--compress-to-fp16` only after confirming acceptable accuracy. IR skips ONNX frontend conversion, but device-specific graph compilation is still performed by the target machine's OpenVINO CPU/GPU/NPU plugin. It therefore mainly stabilizes and shortens model loading; single-frame inference improvements must be measured on the target device.

## Current Core Capabilities

- Backend-independent `Status`, `Result<T>`, `Tensor`, shape, stride, and device descriptions.
- `TensorBuffer` and a fixed-capacity `TensorBufferPool` with shared leases and automatic return to the pool.
- Move-only `Frame` to prevent implicit image copies between pipeline stages.
- Separate camera `FrameBufferPool` and application `BusinessFramePool`.
- Camera buffers are released after image preparation, while application images remain alive until postprocessing such as heatmap generation completes.
- Asynchronous directory `FileSource` that decodes folder images in order and delivers them through move-only `Frame` callbacks.
- Hikrobot MVS GigE/USB camera adapter with continuous acquisition, software triggering, and read-only zero-copy leases from SDK buffers to `Frame`.
- `FrameSourceConfig` and `FrameSourceFactory` provide a unified way to assemble directory, continuous-camera, and timed software-trigger inputs while upper layers hold only `IFrameSource`.
- Composable typestate preprocessing chains with compile-time validation of node order and a unique materialization boundary.
- Camera SDK selection at CMake configuration time, with a generated strongly typed camera `BuildProfile` and capability descriptions.
- Direct pooled writes to Float32 NCHW tensors and in-place normalization, avoiding temporary tensors and full processed-buffer copies.
- Versioned C ABI backend plugins loaded from an explicit deployment plugin directory.
- OpenVINO single-input/single-output Float32 synchronous plugin for IR or ONNX artifacts.
- TensorRT 10 single-input/single-output Float32 synchronous plugin with prebuilt engine loading, optimization profile selection, and dynamic shapes.
- `RuntimeFactory` assembles the frame source, pipeline, and execution policy and returns a unified-lifecycle `RuntimeSession`.
- `vision-modelc` translates ONNX to OpenVINO IR and emits artifact hashes and port information in its build record.
- `anomalyDirectorySample` folder anomaly detection example with per-image score and OK/NG output.

## Mock YOLO Routing Sample

[yoloRouting](Samples/yoloRouting/README.md) demonstrates a custom operator that
routes simulated detection boxes to object classification callbacks, defect
annotations, and cropped text ROIs with an explicit OCR placeholder. It runs
without models or inference plugins and returns a combined, owned per-frame
result through the existing asynchronous runtime. Enable
`VISION_BUILD_SAMPLES=ON` or build the sample as a standalone consumer.

## Declarative Business Applications

[businessExports](Samples/businessExports/README.md) demonstrates a model-free
OpenCV product whose CLI and product Shell link the same business library.
The project declares parameters, commands, typed result fields and image streams
through a pure export contract; builds automatically generate a manifest without
initializing business resources, and Service binds and validates the same
declarations at runtime. `Samples\buildShell.cmd businessExports` produces a
ready-to-design kit with automatic manifest discovery. Use `VISION_PRODUCT_APPLICATION` to select the
product library for the Shell. Existing sample/runtime factories remain supported.

## Event-driven Multi-view Inspection

[eventInspection](Samples/eventInspection/README.md) demonstrates infrared-style
business events, delayed captures at different conveyor positions, heterogeneous
OpenCV pipeline results, and workpiece-level OK/NG/FAULT aggregation.
`EventTriggerSource` accepts bounded, correlated software-trigger requests;
`EventInspectionSession` routes views and rejects late/duplicate results.
Sensor listening, debounce, conveyor timing and PLC transport stay in business
code. `Samples\buildShell.cmd eventInspection` builds a shared CLI/Shell product
and publishes its Designer kit to root `Publish/release`. The event adapter
connects lifecycle, image streams, outcomes, faults and statistics to Service.
Recovery starts from empty; raw GUI triggers are rejected for correlated cameras.
This does not add automatic event scheduling to the default profile assembler.

## TensorRT Plugin

OpenVINO and TensorRT are implemented as independent C ABI plugins and loaded through the model package plus deployment configuration.
See [Backend Plugins](Docs/backendPlugins.md) for contracts, build options and tests.

The Windows TensorRT plugin uses NVIDIA TensorRT 10.16.1.11 GA SDK and CUDA Toolkit and requires MSVC. The GitHub `NVIDIA/TensorRT` repository contains only OSS components and cannot replace the GA SDK, which includes `nvinfer.lib` and runtime DLLs. The default SDK layout is:

```text
Thirdparty/tensorrt/10.16.1.11/windows-x86_64/TensorRT-10.16.1.11/
├─ include/NvInfer.h
├─ lib/nvinfer_10.lib
└─ bin/nvinfer_10.dll
```

Example configuration:

```powershell
cmake -S . -B Build/MSVC-TensorRT -G Ninja `
	-DVISION_BUILD_TENSORRT_PLUGIN=ON `
	-DCUDAToolkit_ROOT=D:/cuda
```

The plugin directly loads a `.engine` or `.plan` compatible with the target GPU and TensorRT/CUDA versions; it does not parse ONNX at runtime. Inputs and outputs must be contiguous host Float32 tensors. The deployment `device` selects the CUDA device index. Inference uses a CUDA stream for H2D, `enqueueV3`, and D2H, then returns outputs as host `TensorMap` storage owned by the core.

## Directory Image Source

The directory input scans its directory when created and decodes images sequentially on a worker thread after startup. It uses OpenCV internally, while the public interface exposes only configuration, `IFrameSource`, `Frame`, `Result`, and standard library types.

```cpp
#include <visionruntime>

#include <chrono>
#include <memory>

using namespace visionRuntime;

core::Result<std::unique_ptr<camera::IFrameSource>> startImageDirectory() {
	camera::FrameSourceConfig config = camera::FileFrameSourceConfig{
		.source = {
			.directory = "images",
			.frameInterval = std::chrono::milliseconds(100),
			.recursive = true,
			.loop = false,
		},
	};
	auto sourceResult = camera::FrameSourceFactory::create(config);
	if (!sourceResult) {
		return sourceResult;
	}

	auto source = std::move(sourceResult).value();
	auto started = source->start([](core::Result<vision::Frame> frame) {
		if (!frame) {
			return;
		}
		// Submit std::move(frame).value() to the Pipeline.
	});
	if (!started) {
		return core::Result<std::unique_ptr<camera::IFrameSource>>::failure(
			started.status());
	}
	return core::Result<std::unique_ptr<camera::IFrameSource>>::success(
		std::move(source));
}

// Call requestStop() before wait() during shutdown.
```

The default extensions are `.bmp`, `.jpeg`, `.jpg`, `.png`, `.tif`, and `.tiff`, matched case-insensitively. Recursive scanning, looping, frame interval, and sorting by lexicographic order or last modification time are configurable. Gray8, Gray16, Float32Gray, BGR8, and BGRA8 decoded results are currently supported. Files that cannot be decoded or are unsupported return a failed `Result<Frame>` through the callback without preventing subsequent files from being processed.

Each `Frame` owns the OpenCV decoding memory through its shared `TensorBuffer`, so pixels remain valid after the callback returns until the last Frame/Buffer view is released. The file source outputs `Frame`, not a raw `TensorBuffer`; the preprocessing stage generates the model input `Tensor`.

## Hikrobot MVS Camera Source

When the `HIK_MVS` profile is selected, Runtime conditionally compiles `HikrobotMvsCameraDevice`. By default, the SDK package is located at `Thirdparty/hik-mvs/4.8.1`, with platform files separated into `windows-x86_64` and `linux-x86_64`. `HIK_MVS_ROOT` can point to another location. The local Thirdparty package includes runtime libraries, but the target machine must still have matching MVS drivers and system services installed.

```powershell
cmake --preset mingw-debug -B Build/HikMvsDebug `
	-DVISION_CAMERA_SDK=HIK_MVS
cmake --build Build/HikMvsDebug --target hikMvsCaptureSmoke
```

Application code selects continuous acquisition through the factory. Omitting the serial number is valid only when exactly one camera is currently present:

```cpp
#include <visionruntime>

using namespace visionRuntime;

camera::FrameSourceConfig config = camera::ContinuousCameraSourceConfig{
	.device = {
		.serialNumber = "camera-serial",
		.pixelFormat = vision::PixelFormat::Bgr8,
		.maxFramesInFlight = 3,
	},
	.source = {.frameRate = 30.0},
};
auto source = camera::FrameSourceFactory::create(config).value();
source->start([](core::Result<vision::Frame> frame) {
	if (frame) {
		// Submit std::move(frame).value() to the Pipeline.
	}
}).value();
```

Timed software triggering uses `TimedCameraSourceConfig`. `triggerInterval` is the minimum arrival interval between consecutive successful input frames: callback time counts toward the interval, but the next trigger still waits for the current callback to return. At most one trigger is always in flight, with no backlog or catch-up triggers. `responseTimeout` limits the device response after a trigger. In continuous mode, `frameRate` is the actual acquisition rate sent to the camera, not callback throttling.

Both `IFrameSource` and `ICameraDevice` are single-start objects and cannot be restarted in place after the first successful start. `requestStop()` is non-blocking and idempotent; `wait()` joins the thread and guarantees that no callback begins after it returns. Response timeouts, trigger failures, and device errors are delivered as terminal errors. Reconnection should destroy the old Source/Device and create a new instance through the factory.

Acquisition uses `MV_CC_GetImageBuffer`, and `Frame` shares the SDK buffer through a read-only `TensorBuffer`. `MV_CC_FreeImageBuffer` is called automatically after the final Frame/Buffer view is released. Therefore, `maxFramesInFlight` must cover the number of camera frames the Pipeline may hold concurrently. Concrete Source types, `ICameraDevice`, and `HikrobotMvsCameraDevice` are not exported by the `<visionruntime>` or `<camera>` aggregate headers; diagnostic tools must include the corresponding advanced extension header explicitly.

The initial version directly supports Gray8, Gray16, RGB8, BGR8, RGBA8, and BGRA8. Bayer, YUV, and packed 10/12-bit formats are not converted implicitly. Hardware triggering, device timestamp calibration, and reconnection remain planned work. The hardware smoke test tool is used as `hikMvsCaptureSmoke [serial] [trigger]`.

## Composable Preprocessing

In project naming, `preprocess` and `postprocess` are each treated as one word: directories and namespaces use lowercase, while types use the `Preprocess` and `Postprocess` forms.

The current preprocessing chain consists of independent Frame nodes, a Frame-to-Tensor materialization node, and subsequent Tensor nodes:

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

`Resize` scales the short side to the target size and writes an 8-bit Frame in the original pixel format into the node's own BufferPool. `CenterCrop` creates a zero-copy center-crop view on that Frame. `ToTensor` only converts the current Gray8, Bgr8, or Bgra8 Frame to a Float32 NCHW Tensor. The working Frame is released after writing, and `Normalize` then automatically reads the current Tensor and normalizes it in place. All operation nodes follow the common `PreprocessNode` protocol. The builder exposes only one generic `then()`, while `build()` reports parameter validation and BufferPool creation errors.

## Directory Anomaly Detection Sample

[`Samples/anomalyDirectory`](Samples/anomalyDirectory) is a standalone application project and is not part of the framework's internal build. It simulates a framework consumer placing the complete VisionRuntime source package under `Thirdparty/VisionRuntime` in its application project, then declaring deployment requirements through a target-level API:

```text
MyInspection/
├─ CMakeLists.txt
├─ main.cpp
└─ Thirdparty/
	└─ VisionRuntime/
```

The core of the application's `CMakeLists.txt` is:

```cmake
add_subdirectory(Thirdparty/VisionRuntime)

vision_target_runtime(myInspection
	CAMERA NONE
	BACKEND_PLUGINS TensorRt
)
```

If the target does not yet exist, `vision_target_runtime` automatically creates an executable from `myInspection.cpp` in the same directory. It can also bind directly to an existing target. Application source code only needs the aggregate header and does not need to know the header locations of backends, the Pipeline, or preprocessing and postprocessing nodes:

```cpp
#include <visionruntime>
```

The framework automatically links the required Runtime, propagates C++20, resolves the selected backend plugin, and deploys the plugin directory declared by the target. The anomaly preset accepts one Float32 NCHW input and one Float32 scalar output. The model produces the image-level anomaly score directly; postprocessing only compares the score with the configured threshold. `score >= threshold` is classified as NG.

```powershell
cmake -S Samples/anomalyDirectory -B Build/SampleConsumer -G Ninja `
	-DVISION_RUNTIME_ROOT=<path-to-VisionRuntime> `
	-DVISION_BUILD_TENSORRT_PLUGIN=ON `
	-DCUDAToolkit_ROOT=<cuda-toolkit>
cmake --build Build/SampleConsumer --target anomalyDirectorySample

Build/SampleConsumer/bin/anomalyDirectorySample.exe `
	<benchmark.csv> <model-package> <deployment.json>
```

`cmake --build Build/SampleConsumer --target publish` assembles a self-contained release directory whose root doubles as the model package (override the location with `-DANOMALY_DIRECTORY_PUBLISH_DIR=<path>`):

```text
release/
├─ anomalyDirectorySample.exe
├─ plugins/tensorrt/   # plugin DLL plus nvinfer, plugin and CUDA runtime DLLs
├─ manifest.json       # model package manifest
├─ artifacts/          # model-fp32.engine
├─ image/              # sample input images
└─ deployment.json
```

Run it from the release root, where the hardcoded `image/` directory, the model package (`.`), and the relative `pluginDirectory` resolve:

```powershell
cd Publish/release
./anomalyDirectorySample.exe benchmark.csv . deployment.json
```

A deployment selects one delivered backend explicitly. A relative
`pluginDirectory` is resolved against the deployment file, not the process
working directory:

```json
{
	"schemaVersion": {"major": 1, "minor": 0},
	"backend": {
		"pluginDirectory": "plugins",
		"id": "tensorrt",
		"device": "0"
	},
	"executor": {
		"performancePolicy": "serial",
		"queueFullPolicy": "block",
		"queueCapacity": 16,
		"stageQueueCapacity": 1
	}
}
```

`VISION_RUNTIME_ROOT` is used only when validating the sample within this repository to point to the framework location. After copying it into a real application project, the default location is `Thirdparty/VisionRuntime`. After building the sample, CMake copies the declared backend plugins and their vendor libraries into `plugins/<backend-id>`. HIK_MVS Windows targets currently copy the complete MVS 4.8.1 `bin` directory to keep the main DLL, transport layers, GenICam, image conversion, and vendor dependency versions consistent. Missing required SDKs or plugin files cause a configuration error. The deployment file selects only from plugin directories packaged with the application.

MSVC targets use the static CRT (`/MTd` for Debug and `/MT` for other configurations). Here, "static CRT" means only that this project and the source-built OpenCV do not depend on the general MSVC C/C++ runtime DLLs; it does not mean the entire application has no dynamic libraries. OpenVINO and Hikrobot MVS remain dynamic SDKs. Some components in the Hikrobot package were built with older MSVC versions and include dependencies such as their own `msvcr*.dll` and `msvcp*.dll`. `Build/MSVC-HikMvs/bin` currently contains 52 top-level DLLs: 5 from OpenVINO/TBB and 47 from the complete MVS runtime. The latter form a runnable deployment set and do not imply that the sample directly loads every DLL in this configuration.

The sample combines `Resize`, `CenterCrop`, `ToTensor`, and `Normalize` through `PreprocessBuilder` to implement the PatchCore preprocessing sequence: resize the short side to 256, center-crop to 224, convert to RGB Float32 NCHW, and apply ImageNet mean/std normalization. The sample writes per-frame benchmark data to the CSV specified on the command line, then appends total duration, completed/failed counts, FPS, and P50/P95/P99 total stage execution time after the batch. `stage = pre + infer + post` and `wait = latency - stage`, so waiting in a parallel queue is not incorrectly counted as stage execution time.

## Image Ownership

The current timed-capture Pipeline uses two buffer lifetimes:

```text
Camera Frame -> crop/copy -> release camera buffer
Business Frame -> infer -> postprocess/heatmap -> release business buffer
```

`PipelinePacket` is move-only. By default, the camera slot is returned after cropping or copying, and the application Frame's same memory address is passed on to inference and postprocessing. Release stages can be adjusted through `PipelineOwnershipOptions`.

A zero-copy crop view continues to hold the camera buffer. To return the camera slot early, obtain a destination Frame from `BusinessFramePool` and write directly into it during preprocessing.

## Execution Model

The runtime provides both synchronous `run()` and asynchronous `submit()`. The recommended API is `RuntimeFactory::createRuntime()`, which returns a `RuntimeSession<ResultType>` so application code manages the entire run only through `start()`, `requestStop()`, and `wait()`. `requestStop()` may be called from any thread and is non-blocking; it closes input and wakes waiters. `wait()` must be called only from an external control thread; it waits for and joins the Source, Executor, and completion thread. `RuntimeSession` owns the frame acquisition controller, while `FrameExecutor` handles only the frame source, stop conditions, failure policy, and runtime statistics.

```cpp
auto runtime = runtime::RuntimeFactory::createRuntime(
	std::move(source), std::move(pipeline), deployment, {
		.frameCount = frameCount,
	}).value();
runtime->start().value();
const auto summary = runtime->wait();
```

`IPipelineExecutor<ResultType>` provides a unified interface for asynchronous submission, stop requests, and waiting. `SerialPipelineExecutor` uses one execution thread to call the complete `run()` in FIFO order. `ParallelPipelineExecutor` overlaps preprocessing, inference, and postprocessing for different tasks on three dedicated threads. The preprocess-to-inference, inference-to-postprocess, and postprocess-to-completion paths each use a fixed-capacity SPSC ring queue. When an internal queue is full or a callback slows down, the upstream stage blocks and backpressure propagates through the stages. SPSC queues use atomic indices and `atomic::wait/notify`, with head and tail separated onto distinct 64-byte cache lines to prevent false sharing. The concurrent `submit()` entry point still uses a mutex-protected queue that supports multiple producers.

`TaskHandle` provides a task ID, status, shared future, and cancellation request. Both executors deliver results in submission order. Graceful stop drains accepted tasks; immediate stop cancels running and queued tasks in order and rejects new submissions. A stage call already in progress is not preempted; its result is replaced with `Cancelled` at a safe boundary.

The framework provides cooperative stopping only. It does not impose a join timeout, kill threads, or terminate the process. If a third-party call or user callback blocks forever, `wait()` and destruction also remain blocked; the application or operating system is responsible for final process-level forced termination. A user callback may call `requestStop()` but must not call `wait()`.

The standalone consumer `anomalyDirectorySample` has been validated end-to-end from its release directory with the TensorRT plugin on the 80 images in `Samples/anomalyDirectory/image` (28 NG and 52 OK; one borderline flip versus the earlier OpenVINO CPU reference of 27 NG and 53 OK). Runtime gracefully drains accepted tasks after the finite `FileSource` ends, and joins the Source, three-stage Executor, and result callback thread before `wait()` returns. All 122 automated tests pass on the MSVC build.

The model Pipeline exposes three stages through `IStagedVisionPipeline`; the OpenCV single-stage Pipeline still supports serial execution only. `RuntimeFactory` selects an executor from the deployment configuration's `performancePolicy`, `queueFullPolicy`, ingress capacity, and stage capacities, then assembles it with the frame source into a `RuntimeSession`. Advanced callers can still use `createExecutor()` to obtain the submission interface directly. See [Docs/architecture.md](Docs/architecture.md#37-executor) for detailed boundaries.

## Build Environment

- Windows: Qt MinGW-w64 13.1 (`D:/Qt/Tools/mingw1310_64`)
- Windows: MSVC 19.51 (`D:/Visual Studio`), x64 static CRT
- WSL Ubuntu: GCC for native Linux builds and `perf`
- CMake 3.25 or later
- Ninja

The project supports MSVC, MinGW, and x86_64 Linux without vcpkg. Third-party dependencies are placed directly under `Thirdparty/<package>/<version>`. OpenVINO is separated by platform; see [Thirdparty/README.md](Thirdparty/README.md) for details. WSL performance builds use the `linux-perf` preset in `Samples/anomalyDirectory/CMakePresets.json`.

Before the first configuration, the `bootstrapDependencies` target can download or verify OpenCV, GoogleTest, nlohmann/json, and spdlog at pinned commits. MinGW/GCC sources enable `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`, while MSVC sources enable `/W4 /permissive-`.

The directory image source builds the minimal `core`, `imgproc`, and `imgcodecs` modules from the `Thirdparty/opencv/4.12.0` source. The first build compiles these modules and takes noticeably longer than subsequent incremental builds.

## Build and Test

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

All build artifacts are written to `Build`.

The Hikrobot anomaly detection sample has been validated end-to-end with MSVC x64 Debug and camera `169.254.239.231`. First load the development environment in `cmd.exe`, then configure and build the standalone project:

```bat
call "D:\Visual Studio\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
"D:\Qt\Tools\CMake_64\bin\cmake.exe" -S "Samples\anomalyHikMvs" -B "Build\MSVC-HikMvs" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MAKE_PROGRAM="D:\Qt\Tools\Ninja\ninja.exe" -DVISION_RUNTIME_ROOT="E:\Work\VisionRuntime"
"D:\Qt\Tools\CMake_64\bin\cmake.exe" --build "Build\MSVC-HikMvs" --target anomalyHikMvsSample
```

The sample takes the model package directory and the deployment file as arguments; a relative `pluginDirectory` in the deployment resolves against the deployment file, not the process working directory:

```powershell
Build\MSVC-HikMvs\bin\anomalyHikMvsSample.exe <model-package> <deployment.json>
```

The MVS client must be closed first because Runtime opens the camera exclusively. The first-frame hardware validation output was `score=5.45422, threshold=2, decision=NG`.

CTest covers camera build profiles, core result types, Tensor views, buffer pools, SPSC queues, serial/parallel/batch executors, backpressure and cancellation, Pipeline lifetimes, directory image decoding, preprocessing, anomaly postprocessing, backend plugin loading, model package loading, deployment configuration, and v1 release contracts. Test counts depend on enabled service, GUI, and vendor targets; use `ctest -N` in the selected build rather than a historical count.

GitHub Actions enables the model-free samples in both Windows MSVC and Linux GNU
jobs. The Windows Qt matrix builds and tests the generic Shell, `businessExports`,
and `eventInspection`, including their desktop publication targets. TensorRT,
OpenVINO, camera SDKs, GPU engines, and local replay assets are not CI prerequisites;
validate those separately on machines with the matching SDKs and hardware.

## v1.0 Contract and Release Gates

The v1 product route is single-model, scalar-score anomaly detection with directory inputs or Hikrobot MVS cameras and explicitly deployed OpenVINO/TensorRT plugins. Classification, detection decoding, OCR, heatmaps, hardware trigger synchronization, automatic reconnect, and multi-model routing are not v1 product capabilities. Custom SDK consumers can implement business logic through the existing C++ extension interfaces.

### Fault and deadline configuration

Existing profiles retain their queue `block`/`drop` behavior. The following optional fields enable production fault policies:

```json
{
  "pipeline": {
    "faultPolicy": "reportAndContinue",
    "taskTimeoutMilliseconds": 1000,
    "queueFullPolicy": "block"
  }
}
```

- `faultPolicy`: `reportAndContinue` (default) or `stopSession`. Failed inspection results never become OK; a terminating camera fault always stops the session, regardless of this policy.
- `taskTimeoutMilliseconds`: integer 1..86400000, omitted to disable. SDK consumers use `DeploymentConfig.executor.taskTimeout` or `ExecutorOptions.taskTimeout`. The deadline starts at `FrameMetadata::capturedAt`, or submission when that timestamp is absent, and includes queue waiting, batching, processing, and completion dispatch.
- Timeout handling is cooperative: an expired task is rejected before admission or at a stage boundary, and late successful outputs become `DeadlineExceeded`. A running vendor inference or blocking user callback cannot be preempted; there is no guaranteed deadline-time callback or bounded shutdown for a hung vendor SDK. Use an external process supervisor if that failure mode must be recovered.
- Continuous camera sources accept optional `noFrameTimeoutMilliseconds` (same range); SDK consumers use `ContinuousCameraSourceOptions.noFrameTimeout`. This reports silence as a terminal fault. Software-trigger sources retain their separate `responseTimeoutMilliseconds`. Choose these values above the expected acquisition interval and worst normal jitter.
- `NaN` and positive/negative infinity in an anomaly score produce `DataLoss`, never an OK decision.

### Correlated delivery

`FrameExecutionOptions.resultObserver`, `MultiCameraExecutionOptions.resultObserver`, and `VisionService.resultObserver` receive outcome (`Completed`, `Dropped`, `Rejected`), `FrameIdentity`, optional accepted task ID, and `Result<T>`. Identity retains `executionId`, `sourceId`, frame sequence, and capture metadata after image buffers are released. IDs are process-local; persist a deployment/run identifier alongside them for cross-restart traceability. Business applications map these identities to their own workpiece/trigger IDs.

Observers run on source or completion threads and must remain non-blocking. Forward immutable result data into the application's bounded device/traceability queue; do not perform PLC communication, image saving, or disk I/O in these callbacks. An observer exception is logged and does not kill its worker, but cannot prove that a physical action was performed.

Acquisition sequence gaps are reported through `sourceFaultObserver` and `missingFrames`/`missingPerSource`, separately from executor `dropped`/`droppedPerSource`. Gap counts describe missing monotonically increasing sequence values, not missing physical workpieces; first frames and sequence resets do not establish a gap count. Third-party sources must override `IFrameSource::failureIsTerminal()` when a delivered error terminates acquisition.

Service monitoring endpoints:

- `inspection.result`: an immutable snapshot per published outcome; errors use `valid:false`, `decision:"UNKNOWN"`, `score:null`, and an explicit error code. `sourceId` maps to the profile's source array; rejected/dropped frames have no accepted task ID.
- `source.fault`: source index, terminal flag, error, and sequence-gap information.
- `session.summary`: also exposes acquisition gaps, rejected frames, and expired tasks.
- `metrics.performance`: adds capture-to-delivery P50/P95/P99 and sample counts. Stage and delivery percentiles use bounded windows of the latest 4096 measured outcomes; completed/failed counters still span the whole session. Outcomes without a valid software capture timestamp are not added to the delivery window.
- `session.recover`: while idle, reload the saved profile and recreate camera/model resources; detection must then be explicitly started. Sessions remain single-use. Recovery does not resume a production line automatically.

The GUI dispatcher has a bounded queue and reports saturation. Monitoring endpoints are not a durable industrial command bus: an overloaded UI subscription may miss a notification, while the latest snapshot remains readable. Use the Runtime/Service result observer for business handoff and implement device acknowledgement/idempotency in the business adapter.

### Product-specific Shells from samples

Build a separate Shell for each product instead of running a sample executable
alongside a generic Shell. `VisionService(ProductRuntimeFactory)` adopts the
sample's existing `RuntimeSession<AnomalyResult>` or
`MultiCameraSession<AnomalyResult>`. It attaches monitoring observers without
replacing business callbacks, pipelines, executors, or camera instances.
Recovery calls the same factory again.
The previous session is destroyed before rebuilding so its model/device
resources can be reopened. GUI snapshots own their pixels and do not pin SDK
buffer leases; detection itself retains the Runtime's existing ownership model.
Business code that retains raw leased frames must release them before recovery.

Windows: edit the machine configuration at the top of
[`Samples/buildShell.cmd`](Samples/buildShell.cmd): `QT_DIR` is a Qt6 **MSVC**
kit; `VCVARS64` is optional when Visual Studio discovery or an x64 developer
prompt is available. The script explicitly uses MSVC x64, Ninja, Release, and
four jobs; it does not use CMake presets. CMake 3.25+, Ninja, Qt6 Quick, and the
selected inference/camera SDK development files must be installed.

```bat
Samples\buildShell.cmd
Samples\buildShell.cmd anomalyHikMvs
Samples\buildShell.cmd anomalyHikMvsMulti
Samples\buildShell.cmd anomalyTensorRtMulti
```

Linux:

```sh
sh Samples/buildShell.sh anomalyDirectory -DCMAKE_PREFIX_PATH=/path/to/qt
```

Both scripts default to `anomalyDirectory` and forward remaining arguments to
CMake. Build output defaults to `Build/SampleShell/<sample>`; the design kit
is installed directly into `Publish/release`, replacing the previous kit:

```text
Publish/release/
  visionDesigner/                 # Designer, Qt runtime, Shell/controls
  runtimeTemplates/windows/       # or linux: dedicated Shell + selected backend
  profiles/<sample>.json          # starting profile, without model/image assets
  design.cmd                     # or design.sh
  sample-release.json
```

Only the platform and product built this time are included; stale templates for
other products/platforms are not retained. Run `design.cmd` (Windows) or
`./design.sh` (Linux), select the model and source resources, compose the GUI,
then publish the final `bin/` + `products/<id>/` + entry-script package in Designer.
Linux currently uses system Qt libraries. Environment overrides are
`VISION_SHELL_BUILD_DIR`, `VISION_SHELL_JOBS`, and on Windows
`VISION_SHELL_QT_DIR`/`VISION_SHELL_VCVARS64`.

Each sample's `shell.cmake` declares its default sources/backend and its original
sample source defines `visionSample::createSampleRuntime`. Put business
assembly/callback changes in that shared function, **not** inside CLI-only
`main()`. Rebuild the script after C++ changes.
Product-specific JSON remains available as `RuntimeOptions.productDocumentJson`;
source ordering must match the profile's source array. Directory Shell timing
output defaults to stdout, not an implicitly overwritten working-directory CSV;
the CLI still uses its explicitly supplied CSV path.
Designer does not compile C++: it automatically selects the adjacent
`runtimeTemplates/<platform>` before previously saved global paths. An explicit
template selection in the current Designer session takes precedence.
Product identity is carried in runtime metadata
and `runtimeProduct`; mismatched runtimes are rejected rather than silently
falling back to the default assembler.

Building the design kit does not require or copy model/image assets and does not
open cameras. Prepare and select these resources before final publication in
Designer; the generated `model` and `image` paths are placeholders relative to
the profile. Hik samples require MVS; TensorRT multi defaults to
directory replay and only needs `-DVISION_CAMERA_SDK=HIK_MVS` for camera use.
Use `-DVISION_SAMPLE_BACKEND=openvino` only with a compatible OpenVINO model
artifact. Single-source AnomalyPreset samples explicitly reject changes to
their fixed preprocessing (256 resize, 224 crop, one channel, mean 0.449,
standard deviation 0.226); customize their assembly to change it.

Design kit replacement is staged: build/deployment failures leave the existing
release intact. Unexpected files at the release root prevent replacement;
keep user projects and final products outside this replaceable kit.
Windows deployment uses MSVC and windeployqt, includes only the selected inference
backend and (when required) vendor camera runtime DLLs, and excludes Qt development
plugins. The included `vc_redist.x64.exe` installs the MSVC runtime on machines
that do not already have it. Final product assembly and sealing happen in Designer.
`visionShell --export-manifest <output> <profile>` exports actual endpoints,
including product registrations, but loads the runtime and can open cameras.
Adopted runtimes retain their own timing callbacks; Service reports
`stageMetricsAvailable:false` when no stage timing provider is wired.

### Packaging and preflight

Designer publication checks copy/write failures, rejects differing runtime/plugin files in a shared output, and refuses missing model manifests, selected backend libraries, and empty directory sources. Product/source IDs used in publication must match `[A-Za-z0-9][A-Za-z0-9_.-]*`; display names may be localized. A failed publication retains `publishing.lock`, and Shell refuses such an incomplete product. Remove only that failed product's directory and republish into a clean/versioned package root.

Published products carry `release-manifest.json` with SHA-256 hashes of their runtime, profile, model and assets. Shell verifies the listed files before loading the product. This is an integrity check, not a digital signature or license mechanism. Editing packaged files requires republishing. Full verification may take time for large model/image packages.

```powershell
bin\visionShell.exe --check products\<id>\profile.json
bin\visionShell.exe --smoke 30 products\<id>\profile.json
```

`--check` verifies package checksums when present and loads the profile, model, selected plugin and configured camera without starting acquisition. A development profile without a release manifest is explicitly reported as such. Windows runtime templates include Qt; Linux templates require the documented target Qt/runtime dependencies. Camera/GPU drivers remain target-machine prerequisites.

`--smoke` accepts 1..86400 seconds and exits successfully only after final session counters report at least one successful inspection, preview frames, and zero failed, missing, rejected, expired, dropped frames or source failures. A running image stream alone is not an acceptance pass. This strict no-loss check intentionally rejects an overloaded Drop configuration; test deliberate overload separately.

Before publishing the v1.0 tag, retain evidence for all gates:

1. Build/test the Runtime and Service on Windows MSVC and Linux GNU; build Shell/Designer and run `visionDesignerSelftest` on supported GUI builds. Vendor inference tests must run with real model packages, not be counted as passed when skipped.
2. Compare a fixed OK/NG golden dataset with the training reference, including threshold-borderline inputs; specify numerical tolerance and allowed false-accept/false-reject rates for the product.
3. Run the actual camera/backend/profile at the required input rate; record capture-to-delivery P50/P95/P99, deadline failures, both categories of loss, CPU/GPU use and memory. Set limits from the line's cycle time, not average FPS.
4. Perform at least a 24-hour initial soak and a 72-hour release soak on the target hardware, with bounded memory and reconciled frame/outcome counts. Inject queue overload, camera disconnect/silence, invalid output, model failure and repeated stop/recover/start.
5. Copy the complete package to a clean target without the repository/development environment, relocate it, run preflight and inspection, and verify the documented driver/runtime prerequisites.

Software tests do not certify hardware soak, model accuracy, device actuation, or clean-target deployment. The version metadata identifies the candidate; release approval requires the corresponding evidence.

Current candidate verification on the development host:

- Windows MinGW: 181 registered checks, 180 passed and one symlink-permission check skipped. Windows MSVC: 186 checks, 182 passed and four TensorRT model checks skipped.
- Linux GNU under WSL: Runtime and Service built successfully, with all 175 registered checks passing. Vendor plugins and the Linux GUI were not enabled in that build.
- The real OpenVINO plugin's five contract checks passed on both Windows toolchains with a generated dynamic-shape identity IR model, including output ownership after backend destruction.
- Lifecycle, fault, deadline and package checks were repeated 20 times; the four batch grouping/flush checks were repeated 30 times on each Windows toolchain.
- The real rubber-ring OpenVINO directory profile passed preflight and a five-second smoke run: 42 successful inspections and zero failed, dropped, missing, rejected or expired frames.
- A freshly deployed Qt runtime was relocated outside the repository and passed real inference with development directories removed from `PATH`: 27 successful inspections and zero losses/errors. This is a same-host relocation check, not clean-machine certification.
- The updated Hikrobot adapter and sample compiled with MSVC. Camera acquisition was not started for this candidate verification.

Formal release remains pending the field gates above and real TensorRT model validation. A passed or skipped test count must not be substituted for those acceptance results.

Confirm redistribution rights and license notices for Qt, inference runtimes and camera SDKs before shipping binaries. The local Qt tooling emitted license warnings during this verification; successful compilation does not resolve those warnings.