# anomalyTensorRtMulti — TensorRT 动态 batch + 多流并行的多源检测示例

以 `anomalyHikMvsSample` 的方式手工组装完整流水线（FrameSourceFactory +
PreprocessBuilder + PluginInferenceBackend + AnomalyThresholdPostprocessor +
`RuntimeFactory::createBatchExecutor` + `MultiCameraSession`），参数由
`profile.json` / `deployment.json` 驱动，演示 TensorRT 插件的两个可选能力：

- **动态 batch**：engine 以 `min/opt/max = 1/4/8` 的 batch profile 构建，
  `BatchPipelineExecutor` 按 `maxBatchSize`/`flushTimeout` 聚合成任意 N≤8 的
  batch tensor 一次推理；插件侧 `dynamicBatch: true` 在加载时显式校验
  profile 覆盖范围。
- **多流并行**：插件选项 `streams: N` 创建 N 个独立
  `IExecutionContext` + CUDA stream 的工作器，`infer()` 空闲优先
  round-robin 派发，`streams: 1` 时退化为原有的单 context 串行语义。

## 运行

样例为自包含运行目录（构建后 exe 旁已有 model/ image/ profile.json
deployment.json plugins/）。

```bat
rem 相机源（每台相机一个 IP，对应一路 stream）：
anomalyTensorRtMultiSample.exe model deployment.json profile.json 169.254.1.10 169.254.1.11

rem 无相机验证：--dirs 后接逗号分隔的回放目录（每目录一路源，循环回放）：
anomalyTensorRtMultiSample.exe model deployment.json profile.json --dirs image,image,image,image
```

`--dirs` 模式跑 20 秒后输出首帧 score/decision 与全局/per-source
received/dropped 统计；image/ 复用 `anomalyDirectory/image` 的灰度 BMP
（224×224 模型对 2048×2448 原图先 resize 到短边 224 再中心裁剪），可对照
score 检查阈值判定。单通道 `toTensor` 只接受 Gray8 帧，目录源必须用灰度
图（PPM/P6 会被解码成 BGR 三通道导致逐帧预处理失败）。

## 文件

- `profile.json`：产品参数（preprocess 形状/归一化、阈值、`maxBatchSize`、
  `flushTimeout`，以及插件的 `backend.streams` / `backend.dynamicBatch`）。
- `deployment.json`：backend 插件目录与 executor 策略（`pipelineParallel`
  + `block` 回压）。
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
