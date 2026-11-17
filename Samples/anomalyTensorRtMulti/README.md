# anomalyTensorRtMulti — TensorRT 动态 batch + 多流并行的多源检测示例

手工组装完整流水线（FrameSourceFactory +
PreprocessBuilder + PluginInferenceBackend + AnomalyThresholdPostprocessor +
`RuntimeFactory::createBatchExecutor` + `MultiCameraSession`），参数由
完整产品 `profile.json` 驱动，CLI 与 Shell 共享同一管线，演示 TensorRT 插件的两个可选能力：

- **动态 batch**：engine 以 `min/opt/max = 1/4/8` 的 batch profile 构建，
  `BatchPipelineExecutor` 按 `maxBatchSize`/`flushTimeout` 聚合成任意 N≤8 的
  batch tensor 一次推理；插件侧 `dynamicBatch: true` 在加载时显式校验
  profile 覆盖范围。
- **多流并行**：插件选项 `streams: N` 创建 N 个独立
  `IExecutionContext` + CUDA stream 的工作器，`infer()` 空闲优先
  round-robin 派发，`streams: 1` 时退化为原有的单 context 串行语义。

## 运行

在 `profile.json` 中配置 `sources` 的相机 IP 或文件目录、`model` 的模型和插件路径、
`pipeline` 的算法参数，以及 `ui` 的界面。所有相对路径基于 profile 所在目录。

```bat
anomalyTensorRtMultiSample.exe Samples\anomalyTensorRtMulti\profile.json
```

默认 `durationMilliseconds:20000`，运行 20 秒后输出首帧 score/decision 与全局
received/dropped 统计；image/ 复用 `anomalyDirectory/image` 的灰度 BMP
（224×224 模型对 2048×2448 原图先 resize 到短边 224 再中心裁剪），可对照
score 检查阈值判定。单通道 `toTensor` 只接受 Gray8 帧，目录源必须用灰度
图（PPM/P6 会被解码成 BGR 三通道导致逐帧预处理失败）。

### Shell 编译与 Designer 工作流

按仓库约定，先配置 Qt MSVC kit，然后运行：

```bat
Samples\buildShell.cmd anomalyTensorRtMulti
```

该入口以 MSVC Release 构建，并直接替换 `Publish/release/` 设计工具包。
`runtimeTemplates/windows/visionShell.exe` 链接本示例运行时，配套
TensorRT/CUDA/Qt 依赖；`profiles/anomalyTensorRtMulti.json` 是起始配置。
它不是已经设计完界面、密封后的最终产品包。

构建本身不复制模型和图片。构建完成后，将本示例的 `model/` 复制到
`Publish/release/profiles/model/`，将 `image/` 中的 BMP 分配到
`profiles/image/camera1/`、`profiles/image/camera2/`，作为两路独立
模拟相机。起始配置中的模型和目录路径都相对于 profile，插件指向
`../runtimeTemplates/windows/plugins`。每次替换设计工具包后需重新补齐资源。

双击 `Publish/release/design.cmd`，自行在 Designer 中设计界面并发布最终
产品。用户项目和最终产品应保存到设计工具包之外，以免下次编译覆盖。
编译完成并补齐资源后，可先验收运行时：

```bat
cd Publish\release
.\runtimeTemplates\windows\visionShell.exe --check profiles\anomalyTensorRtMulti.json
.\runtimeTemplates\windows\visionShell.exe --smoke 10 profiles\anomalyTensorRtMulti.json
```

预处理缓冲池同时覆盖阶段队列和正在推理的 batch，避免 Release 全速
回放时耗尽池容量。本示例一轮检测会在 20 秒后自动结束，可再次启动。
目标机器仍需兼容的 NVIDIA GPU 和驱动，engine 必须与 GPU/TensorRT 版本兼容。

## 文件

- `profile.json`：采集源、模型/插件路径、preprocess 形状/归一化、阈值、
  `maxBatchSize`、`flushTimeout`、`backend.streams` / `backend.dynamicBatch` 和 UI。
- `deployment.json`：保留的旧配置示例，不再由本 sample CLI 读取。
- `model/`：PatchCore 动态 batch 模型包。`manifest.json` 声明
  `[4,1,224,224]` 基准形状与 `maxBatchSize: 8`；`artifacts/patchcore-dynbatch.engine`
  由 trtexec 构建：

  ```bat
  trtexec --onnx=model_dynbatch.onnx --saveEngine=patchcore-dynbatch.engine ^
      --minShapes=images:1x1x224x224 --optShapes=images:4x1x224x224 ^
      --maxShapes=images:8x1x224x224
  ```

  ONNX 由 PatchCore 仓库的 `export_onnx_dynamic_batch.py` 从
  `memory_bank.npy` 重新导出（原 `model.onnx` batch=1 是烘焙进图的常量，
  该脚本仅加 `dynamic_axes` 即可，forward 本身是 batch 安全的）。

## 实测（RTX 3060 Laptop，TensorRT 10.16，MSVC Debug）

trtexec 对该 engine：batch=1 单次 GPU compute ≈ 8.1 ms；batch=4 ≈ 20.8 ms
（≈5.2 ms/帧），batch 摊薄吞吐约 1.55×。4 路目录源全速回放 20 s：
received=444、failed=0、dropped=0，四路均衡（111/111/111/111）。

## 构建

TensorRT 插件在 Windows 上要求 MSVC：

```bat
call "D:\Visual Studio\Common7\Tools\VsDevCmd.bat" -arch=amd64
cmake -S . -B Build\MSVC-2026 -G Ninja -DCMAKE_BUILD_TYPE=Debug ^
    -DVISION_BUILD_TENSORRT_PLUGIN=ON -DVISION_BUILD_SAMPLES=ON
cmake --build Build\MSVC-2026 --target anomalyTensorRtMultiSample
```
