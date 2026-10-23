# 业务应用架构约束与完整项目构建指南

> 状态（2026-09-23）：已定稿的架构约束。适用于"基于 VisionRuntime 开发带机械臂/自定义算子的
> 工业检测业务，并最终接入 GUI"的完整项目。本文档回答四个问题：业务代码放哪、回调怎么接、
> GUI 边界在哪、发行包怎么出。

## 1. 核心约束思想

### 1.1 框架的扩展点分两类，必须分清

| 扩展点 | 机制 | 是否需要重编框架 |
| --- | --- | --- |
| 推理后端（TensorRT/OpenVINO） | C ABI 动态插件 dll，profile 按 id 选择 | 否 |
| 前处理 / 后处理 / 结果回调 | 公开头文件接口（`IPreprocessor`/`IPostprocessor`/`std::function`），编译期静态链接 | 只重编引用方，不动框架库 |

前处理、后处理、回调**不是插件**。通用 Shell 的默认流水线仍由 `SessionAssembler` 组装。
产品专属 Shell 通过 `VisionService(ProductRuntimeFactory)` 接管 sample 自组装的
`RuntimeSession<AnomalyResult>` 或 `MultiCameraSession<AnomalyResult>`，不会创建第二条默认流水线。
profile.json 调整参数；修改 C++ 算子或业务回调后，重新编译对应产品 Shell。

新增声明式产品入口：`ProductApplicationFactory` 返回业务 session 与
`BusinessExports`。Service 自动注册声明的参数、命令、结构化状态和图像流，
Designer 导入同一声明生成的 manifest 后提供字段选择与绑定校验。
CLI 与产品 Shell 链接同一个 `visionProductApplication` 静态库，属于同进程方案 A，
不扫描源码、不引入 IPC。完整可运行示例见
[businessExports](../Samples/businessExports/README.md)。

接口声明应独立于业务初始化：`visionProductContract` 只包含描述信息，
`vision_export_product()` 自动在构建阶段生成 manifest；`createApplication()` 才创建
真实业务并绑定同一声明。导出不要求相机、模型或图片资源可用。
套件中的 manifest 自动被 Designer 发现，运行时校验声明与实际绑定一致。

### 1.2 两条使用路线按定制程度选择

- **重定制（自定义算子、机械臂联动、特殊结果处理）→ 产品专属 Shell 或终端 exe**：
  在共享业务组装函数中创建 Runtime，CLI 与 Shell 使用同一函数。Service 只附加 GUI 观察者、
  生命周期控制与设备端点，不替换业务回调，也不重新打开相机。
- **轻定制（只调阈值/尺寸/均值/相机参数）→ designer → shell 产品包**：profile.json 驱动，
  全程不改代码。

### 1.3 回调铁律

所有回调（结果 observer、`CompletionCallback`、`frameObserver`）都运行在流水线工作线程或
源线程上，**必须非阻塞**。机械臂通信（TCP/Modbus/EtherCAT）的做法：回调里只把
「sourceId + decision + score」投递到业务方自己的队列，由独立线程与设备 SDK 通信。

### 1.4 单模型约束（现状）

默认异常检测装配路径：一个产品 = 一条流水线 = 一个模型。`ProductProfile.model` 是产品级单例，所有相机共享同一
preprocess/backend/postprocess 并合批推理。多模型（不同相机不同模型）或"部分相机不推理"
需要改造 profile schema + assembler 路由 + designer 打包，属框架级功能，见第 5 节展望。

声明式产品路径不要求算法返回 `AnomalyResult`：使用
`adoptProductRuntime<MyResult>()` 接入自定义结果，通过 `StateSchema<MyResult>` 声明
对外字段。仅跑 OpenCV 的产品可在声明 `runtimeProduct` 后省略 `model`；
Designer 验证/发布也支持这类产品。多执行器/多模型路由仍由业务 session 自行组装，
不会由导出声明自动推导算法拓扑。

新增底层事件检测路径：`EventTriggerSource` 接受带工件/检测/拍照/视角身份的
外部采集请求；`EventInspectionSession::addRoute<T>()` 可为各视角配置不同结果类型的
pipeline，仅在汇总适配器处转换成 OK/NG。全部必需视角返回才完成，错误或缺图超时为
FAULT；相机响应超时后采集源锁故障，需重建，防止迟到帧串工件。
红外监听、去抖、传送带延时和编码器跟踪仍在 sample/业务层。
`adoptEventInspectionSession()` 将生命周期、帧流、中立结果、采集故障和帧统计接入
Service；业务 `IInspectionSession` 包装层拥有监听器/延时调度器并转发接口。
事件会话等待外部信号时仍为运行中，不要求伪装成连续 `IFrameSource`。
GUI 相机曝光/增益可用，但禁止无检测身份的裸 `softwareTrigger`；
`session.recover` 重建业务和采集源，清空待处理工件、定时任务及计数。
CLI 与 Shell 共享同一产品工厂，示例及运行方法见
[eventInspection](../Samples/eventInspection/README.md)。
这不是默认 profile 的多模型路由，也不会自动生成 DAG 或 PLC 通信。

### 1.5 GUI 边界：监控与调参，不接业务逻辑

v1.0 补充：`VisionService.resultObserver` 可将带相机索引、帧序号和执行 ID 的成功/失败/丢弃/
超时结果投递给业务队列；`inspection.result`/`source.fault` 供 GUI 监控。队列到 PLC/机械臂
的确认、幂等、工件 ID 映射与动作执行仍属于业务层。当前 Shell 内嵌自己的 Service，并非
独立业务 exe 的远程客户端；两者不能同时独占同一相机。需要共同监控同一检测实例时，
应先集成到同一 Service，或实现明确的进程间监控协议。

designer/shell 产品包只承担：相机画面流（`stream.*` 端点）、实时统计（`metrics.*`）、
曝光/增益调参（`camera.*`）、pipeline 参数调整。机械臂联动与自定义算子放在产品的共享
业务组装函数中，与专属 Shell 编译进同一进程。designer 是纯配置工具，永不负责编译、
不生成 C++/QML 代码，也不会扫描 sample C++；自定义端点从真实 Service manifest 导入。

### 1.6 为什么自定义算子暂不插件化

前处理零拷贝依赖 `TensorBuffer` 的 `shared_ptr` 共享所有权；跨 dll 传 C++ 对象违反 ABI
原则（backend 插件 ABI 明确禁止 STL/异常/虚对象跨边界，MinGW 宿主与 MSVC 插件的堆分配器
和 `shared_ptr` 控制块互不兼容）。正确的插件化方案是"句柄化 buffer 表 + host 分配回调"
（host 集中持有生命周期，跨边界只传 POD 视图和句柄，谁分配谁释放）。这套 ABI 成本不低，
等业务形态稳定、确有"现场换算子不重编"需求时再实施；参数化变换优先做成内置节点。

## 2. 推荐项目结构

业务方代码与框架同树（或并排）存放，分三层：

```text
Apps/<yourProduct>/
├─ appcore/                  # 业务核心静态库（不依赖 Qt/Service）
│   ├─ robotClient.hpp/.cpp      # 机械臂客户端：连接管理、指令发送、独立工作线程
│   ├─ resultDispatcher.hpp/.cpp # 结果回调注入口 + 非阻塞投递队列
│   └─ myPreprocess.hpp/.cpp     # 自定义 IPreprocessor / IPostprocessor 实现
├─ app/                      # 终端 exe（薄壳）
│   └─ main.cpp                  # 参数解析 + 流水线组装 + MultiCameraSession
├─ assets/
│   ├─ model/                    # 模型包（manifest + 引擎文件 + options）
│   ├─ profile.json              # 产品参数（阈值/尺寸/batch/mean/std）
│   └─ deployment.json           # 后端/设备/执行器策略
└─ package.cmd                 # 发行装配脚本（仿 Tools/assembleRelease.cmd）
```

依赖方向：`appcore` → `visionRuntime`；终端 `app` 与产品 Shell → `appcore`。
GUI 与业务共享一个 Runtime；进程外监控仍需另行实现 IPC。

## 3. 逐步构建一个完整项目

### 步骤 1：准备模型包

用仓库工具生成（manifest + 推理引擎 + options JSON）：

- `Tools/createTensorRtMultiSampleModel.py`（TensorRT 动态 batch 引擎）
- `Tools/createBatchBenchModel.py`（OpenVINO 动态 batch IR，需 venv python 的 openvino）

注意：PatchCore 类导出若内部 Reshape 写死 batch=1，动态 batch 推理会形状冲突——必须导出
原生动态 batch 模型（`Tools/createBatchBenchModel.py` 是参考实现；save_model 的输出张量名
要命名 Result 节点：`ops.result(x, name="score")`）。

### 步骤 2：编写 appcore 业务库

1. `IPreprocessor` 实现：单个虚函数 `process(PipelinePacket) -> Result<PreparedInput>`。
   可内部包一层 `PreprocessBuilder` 复用内置节点（CvResize/CvCenterCrop/ToTensor/Normalize），
   自定义逻辑（ROI、去噪等）放在前面或后面。
2. `IPostprocessor<AnomalyResult>` 实现（如需替换阈值判定）。
3. 机械臂客户端：自带工作线程，对外只暴露非阻塞接口（`pushReject(sourceId, score)`）。
4. 结果分发器：注册式回调（`onResult(callback)`），终端 exe 与未来的 shell 集成复用同一套。

### 步骤 3：编写终端 exe

照 `Samples/anomalyTensorRtMulti/anomalyTensorRtMultiSample.cpp` 结构，只改两处：

- `setPreprocessor(...)` 换成 appcore 的自定义节点；
- `PipelineTimingObserver`（或 executor 的 `CompletionCallback`）里调用
  `resultDispatcher`，而不是打印到 stderr。

CMake 要点（照 samples 惯例）：

```cmake
add_executable(yourApp main.cpp)
target_link_libraries(yourApp PRIVATE yourAppCore visionRuntime nlohmann_json::nlohmann_json)
add_dependencies(yourApp visionBackendTensorRt)   # 按需
# POST_BUILD 拷贝 profile.json / deployment.json / model / plugins 到 exe 旁
```

MSVC 必须动态 CRT（`MultiThreadedDLL`），与框架静态库一致。

### 步骤 4：构建与验证

```bat
cmake --preset mingw-debug -DVISION_BUILD_TENSORRT_PLUGIN=ON -DVISION_BUILD_SAMPLES=ON
cmake --build --preset mingw-debug --target yourApp
```

先用 `--dirs` 目录回放做无相机验证，再接相机 IP 实机验证，最后接机械臂（先打日志观察
投递时序，再开真实动作）。

### 步骤 5：业务发行包

写 `package.cmd` 拷贝：exe + backend 插件 dll（`plugins/<id>/`）+ 厂商运行时
（TensorRT/CUDA DLL，参考 sample 的 POST_BUILD 逻辑）+ 模型包 + json。目标机前提：
对应相机 SDK 驱动（海康 MVS）与 GPU 驱动已装。

### 步骤 6：产品专属 Shell（业务与 GUI 共同发布）

1. 将业务组装与回调放到 sample 原源码中的 `visionSample::createSampleRuntime`。
   `main()` 只保留 CLI 参数解析和启停；`VISION_SAMPLE_SHELL` 编译时不包含 `main()`。
   新 sample 需提供 `sampleRuntime.hpp` 和 `shell.cmake`。
2. 配置 `Samples/buildShell.cmd` 顶部的 Qt6 MSVC kit 路径；自定义 VS 安装填写
   `vcvars64.bat` 路径。Windows 脚本明确使用 MSVC x64、Ninja、Release，不依赖 preset。

   ```bat
   Samples\buildShell.cmd anomalyDirectory
   ```

   Linux 使用 `sh Samples/buildShell.sh <sample>`，需 Qt6 Quick 开发包与 Ninja。
   后续参数直接转交 CMake；不传 sample 默认 `anomalyDirectory`。
3. 脚本更新 `Publish/release/`：`visionDesigner/`、`runtimeTemplates/<windows|linux>/`、
   `profiles/<sample>.json` 与 `design.cmd`/`design.sh`。只保留本次平台、专属 Shell 和所选
   后端依赖；模型与图片不参与这一步，起始 profile 的资源路径是待配置占位。
   部署成功才替换旧套件，失败保留旧版；不打开相机。Linux Qt 依赖系统安装。
   这是可替换工作套件，用户项目和最终产品应保存到套件外。
4. 启动套件中的 `design.cmd`/`design.sh`，Designer 自动选中相邻的专属运行时模板；
   当前会话手动选择仍可覆盖，但旧全局设置不会盖过新套件。
   配置模型/图片或相机、编辑 GUI，再由 Designer 复制资源、密封并发布最终产品包。
   `runtimeProduct` 与模板元数据绑定业务身份；不能使用通用 Shell 替代专属运行时。
   自定义端点可用专属 Shell 的 `--export-manifest <output> <profile>` 导出；
   该操作会加载 Runtime，相机产品可能打开设备。

### 步骤 7：运行形态

产品专属 Shell 在同一进程运行业务检测、结果回调、GUI 监控与调参，检测由 Service 启停。
恢复调用同一产品工厂重建 Runtime，原回调保留。业务回调必须非阻塞。
未接入阶段计时提供者时，`metrics.performance.stageMetricsAvailable=false`；
结果与采集到交付的延迟指标仍提供，Runtime 自己的计时回调不受影响。

## 4. 重编译范围速查

| 改动 | 需要重编 |
| --- | --- |
| 业务逻辑（appcore/exe） | 只编你的两个 target |
| profile.json 参数（阈值/尺寸/mean/std/相机参数） | 什么都不编，改 JSON |
| 切换/升级推理后端 | 只编对应插件 target，宿主不动 |
| 框架 Runtime/Service 源码 | 对应框架 target + 所有下游 |
| 产品共享组装函数、业务回调或算子 | 重跑对应 sample 的 buildShell 脚本 |

## 5. 展望（已知差距，按需立项）

- **多模型路由**：`model`/`pipeline` 下沉到 per-source（或产品级默认 + per-source 覆盖）；
  assembler 按模型分组，同模型共享 batch executor；`model: null` 的源只出画面不推理；
  designer 逐源选模型、逐源打包到 `products/<id>/models/<sourceId>/`。
- **算子插件化**：定义 pre/postprocess C ABI（POD 帧视图 + buffer 句柄表 + host
  alloc/retain/release 回调），Runtime 加 `PluginPreprocessor`/`PluginPostprocessor` 宿主，
  Service 按 profile 插件 id 链式加载。回调不做插件（宿主侧策略，Service 层注入）。
- **结果数据上 GUI**：现状 `MetricsAggregator::timingObserver` 只计数，score/decision 不到
  UI；如需展示，扩展端点 payload 或新增 result stream 端点。
