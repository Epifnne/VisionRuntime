# 多相机输入 + 同模型批量推理实施计划

## 目标与决策

- 目标：多相机（IFrameSource ×N）由 Runtime 统一管理的 session 接入，同模型仅加载一次，帧聚合成 batch（N>1）一次推理，结果按样本拆分回各相机回调。
- 决策（用户确认）：
  1. 真正组 batch（N>1 一次 infer），非仅共享权重逐帧。
  2. 目标后端：OpenVINO + TensorRT 都支持。
  3. batch 触发：凑满 batchSize 即推理 + 超时窗口强制 flush，两者可配置。
  4. 验证工具链：Windows MSVC + WSL GNU，不使用 MinGW。
- 关键架构决策：
  - 对外提交接口不变：仍是 `submit(PipelinePacket, CompletionCallback) -> TaskHandle`，组 batch 完全封装在新执行器内部；batch 内第 i 个样本 ↔ 第 i 个提交任务（FIFO 顺序映射），逐任务 fulfill future + 回调。
  - 后处理零接口改动：执行器把 batch 输出 tensor 按 batch 维切片成 N 个单样本 TensorMap，逐个调用现有 `IPostprocessor::process`。
  - 预处理逐帧进行（现有链不动，`ToTensor` 仍产 [1,C,H,W]），聚合在 infer 前做 tensor concat。
  - `PipelinePacket` 增加可选 `sourceId`（诊断/丢帧统计 per-camera）；`FrameMetadata.sequenceNumber` 各相机从 0 冲突问题由 (sourceId, seq) 二元组解决。cameraId 不进 ResultType，由提交侧回调闭包捕获。
  - 现有单源 `RuntimeSession` / `FrameExecutor` / `PipelineParallel` 全部保持不变，新功能为新类、纯增量。

## 现状关键事实

- `RuntimeSession` = 1 source + 1 executor（Runtime/include/runtime/runtimeSession.hpp:15）。
- `PipelinePacket` 仅含 cameraFrame_/businessFrame_/executionId_，无 source 标识（Runtime/include/pipeline/pipelinePacket.hpp:98）。
- ABI `VisionRuntimeTensorView` 有 dimensions+rank，可表达 batch 维（Runtime/include/backends/backendPluginApi.h:55）；host 侧 `PluginInferenceBackend::infer` 直接透传 TensorMap（Runtime/src/backends/pluginInferenceBackend.cpp:283）。
- anomaly preset manifest 校验硬编码 `input.shape == {1,1,224,224}`（Runtime/include/runtime/presets/anomalyPreset.hpp:164）；`ToTensor` 硬编码 batch=1（Runtime/src/preProcess/frameNodes/toTensorNode.cpp:85）。
- OpenVINO 插件：create 时原样编译模型，单 InferRequest + mutex（Runtime/plugins/openvino/openVinoPlugin.cpp:94,102）。
- TensorRT 插件：infer 时已调用 `setInputShape(dimensions)` + `enqueueV3`，动态 shape 通路已存在，瓶颈只在 engine optimization profile 的 maxBatch（Runtime/plugins/tensorrt/tensorRtPlugin.cpp:173）。
- vendor 回归测试已验证动态 N 可传递（[2,1,4,5]，Runtime/tests/backends/vendorPluginTest.cpp:61）。
- 测试模板：Runtime/tests/executor/pipelineExecutorTest.cpp（executor/FrameExecutor 全套）、vendorPluginTest.cpp、anomalyPresetTest.cpp。
- CMake 为显式源文件清单：Runtime/cmake/visionAddRuntime.cmake:45、Runtime/tests/CMakeLists.txt:182。

## 实施步骤（分阶段）

### Phase 0 — 计划落盘
1. 新建 `Docs/multiCameraBatchPlan.md`（本文件）。

### Phase 1 — 帧来源标识（阻塞后续所有阶段）
2. `PipelinePacket` 增加 `std::optional<std::uint32_t> sourceId`（构造参数追加，带默认值保持现有调用兼容；getter/setter）。
3. 测试：扩展 Runtime/tests/pipeline/pipelinePacketTest.cpp 覆盖 sourceId 移动语义。

### Phase 2 — 多源 session（依赖 1；与 Phase 4 并行）
4. 新头文件 `Runtime/include/runtime/multiCameraSession.hpp`：模板类 `MultiCameraSession<ResultType>`，构造接收 `std::vector<std::unique_ptr<camera::IFrameSource>>` + 一个 `IPipelineExecutor<ResultType>`（即 Phase 3 的 batch executor）+ `FrameExecutionOptions` 扩展版。
   - 每个 source 按下标分配 sourceId；启动时逐个 `source->start(...)`，callback 闭包捕获 sourceId，把 (sourceId, frame) 写入 packet 并提交 executor。
   - 统一 `start()/wait()/requestStop()`：start 任一 source 失败则回滚已启动 source；wait 等待全部 source + executor；汇总 per-camera + 全局 `FrameExecutionSummary`。
   - source 失败策略复用 `SourceFailurePolicy`（Skip/Stop），Stop 时触发整体 requestStop。
5. 工厂支持：`FrameSourceConfig` 不变，session 构造方（Sample/工厂函数）按配置数组创建 N 个 source。可在 `RuntimeFactory` 加 `createMultiCameraFromPreset` 便捷入口（可选，放 Phase 8）。

### Phase 3 — batch 聚合执行器（依赖 1、4）
6. 新组件 `Runtime/include/executor/batchPipelineExecutor.hpp`（模板，header-only 参照 frameExecutor.hpp 风格）：
   - 配置 `BatchInferenceOptions { std::size_t maxBatchSize; std::chrono::milliseconds flushTimeout; }`。
   - 内部三级：预处理 worker（逐帧跑 `IPreprocessor`，即现有 PreprocessChain）→ 聚合队列（凑满 maxBatchSize 或 flushTimeout 到点即出队一组 PreparedInput）→ 推理 worker（concat N 个 [1,C,H,W] tensor → [N,C,H,W]，调一次 `IInferenceBackend::infer`，输出按 batch 维切片成 N 个 TensorMap）→ 逐样本跑 `IPostprocessor` 并 fulfill 对应 ExecutorTask。
   - 聚合超时用独立定时线程或推理 worker 内 condition_variable 等待（推荐 cv + timeout，少一个线程）。
   - flush 时不足 batchSize 按实际 N 推理（动态 batch）。
   - 完成派发复用 `CompletionDispatcher`；QueueFull/dropped 统计 per-sourceId。
7. 单测 `Runtime/tests/executor/batchPipelineExecutorTest.cpp`（模板 pipelineExecutorTest.cpp）：凑满触发、超时 flush、混合到达、顺序映射正确性（第 i 样本结果回到第 i 任务）、requestStop 排空。

### Phase 4 — 放开 batch=1 硬编码（依赖 1；与 Phase 2 并行）
8. anomaly preset manifest 校验：batch 维改动态——校验改为 rank==4 且 C/H/W 匹配、N 任意（Runtime/include/runtime/presets/anomalyPreset.hpp:164 附近，含报错文本）。
9. `ToTensor` 保持逐帧 [1,C,H,W] 不动；concat 逻辑放执行器（Phase 3）。
10. 测试：anomalyPresetTest.cpp 增加 N>1 manifest 用例；preprocessChainTest.cpp 不动。

### Phase 5 — OpenVINO 动态 batch（依赖 4；与 Phase 6 并行）
11. `Runtime/plugins/openvino/openVinoPlugin.cpp`：create 时在编译前对单输入模型 reshape 为动态 batch（`ov_model_reshape`，batch 维设 dynamic），再 compile；保留单 InferRequest + mutex。
12. 配置项：插件 create config 增加可选 `dynamicBatch`/`maxBatchSize`（读现有插件 config 解析处一并扩展）。
13. 回归：vendorPluginTest.cpp 增加 [N,1,224,224]（N=2/4）infer 用例。

### Phase 6 — TensorRT 动态 batch（依赖 4；与 Phase 5 并行）
14. 插件 infer 通路已支持动态 shape；工作是验证 + 约束文档化：engine 必须以 profile maxBatch ≥ maxBatchSize 构建。
15. create 时校验 engine profile 覆盖配置的 maxBatchSize，不满足则报错（参照现有 profile 拒绝路径测试）。
16. 回归：vendorPluginTest.cpp TensorRT 用例。

### Phase 7 — Sample 与端到端（依赖 2、3、5/6 至少其一）
17. 新 Sample `Samples/anomalyHikMvsMulti/`（参照 anomalyHikMvs）：deployment JSON 扩展为多相机数组（IP 列表 + 每相机 sourceId），构建 MultiCameraSession + BatchPipelineExecutor；completion 回调打印 (cameraId, score)。
18. Runtime/CMakeLists 与 Samples 的 CMake 显式登记全部新文件；测试在 Runtime/tests/CMakeLists.txt:182 之后登记。

### Phase 8 — 文档收尾
19. 更新 `Docs/architecture.md`：多通道章节（L251 附近）从"未来规划"改为已实现说明 + batch executor 线程模型图；`Docs/backendPlugins.md` 补充动态 batch 契约。
20. changelog.md 记录。

## 完成记录（2026-09-16）

- Phase 1–7 全部落地（2026-09-11 起）：`PipelinePacket::sourceId` 烙印、`MultiCameraSession`、`BatchPipelineExecutor`（maxBatchSize 凑批 + flushTimeout 动态合批）、anomaly manifest 放开 batch 维 N≥1、OpenVINO 插件 `dynamicBatch`、TensorRT 插件 `maxBatchSize` 校验、Samples `anomalyHikMvsMulti` 与 `anomalyTensorRtMulti`。
- Phase 8 文档收尾完成：`Docs/architecture.md` 多通道章节与 `Docs/backendPlugins.md` 动态 batch 契约已更新；changelog 已记录。
- 后续重构（2026-09-16）：拼接/单次推理/切片从执行器收编进 `Pipeline::inferBatch`（共享 helpers 在 `pipeline/batchTensors.hpp`），执行器只负责凑批与超时 flush 调度，不再直调后端，`backend()` 访问器已移除——Phase 3 中"推理 worker 直调 `IInferenceBackend`"的描述以重构后结构为准。
- 基准结论（MinGW Debug + OpenVINO CPU）：4 源不限流时 batch=4 与 batch=1 均约 300 fps（预处理单线程饱和），batch 吞吐收益 <3%，收益场景在 GPU/TensorRT 等高 per-call 开销后端。

## 验证

1. Windows MSVC（Ninja）configure + build + ctest 全绿，含新增单测（batchPipelineExecutorTest、packet/preset/vendor 扩展用例）。
2. WSL GNU 全量构建 + ctest 无回归。
3. `MSVC HikMvs Sample Build` 任务确认原单相机样例不回归。
4. 手动：两台（或一真一目录源模拟）相机跑 anomalyHikMvsMulti，验证吞吐提升与 (cameraId, 结果) 对应正确；观察 flushTimeout 下延迟上界。

## 明确排除

- 不做 DAG/图执行器、不做条件分支、不做一帧分叉多模型。
- 不做跨通道结果时间对齐/融合（业务层自行处理）。
- 不做 OpenVINO request pool / TensorRT 多 context（batch 内串行推理已满足目标；留作后续优化）。
- 不做异构模型/异构预处理链（同模型 ⇒ 同链）。
- 静态 maxBatch 零填充模式（动态 batch 优先，性能不足时再评估）。

## 风险与缓解

1. OpenVINO 动态 batch 性能回退风险 → 先用 benchmark 对比 N=1 静态 vs 动态；若回退明显再引入静态填充模式。
2. TensorRT engine 需重新导出（profile maxBatch>1）→ 在 Sample README 与 backendPlugins.md 写明 engine 构建要求。
3. 慢相机拖住整批 → flushTimeout 兜底 + per-source dropped 统计暴露问题。
