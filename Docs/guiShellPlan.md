# 通用 GUI Shell 与 Service 层计划

## 1. 目标与定位

在 Runtime 核心之上新增两个组件，使后续产品通过配置（产品 profile JSON）即可生成不同的检测界面，不修改 C++/QML 代码：

- **visionService**：应用服务层静态库，纯 C++20，不依赖 Qt。封装会话生命周期、相机服务、指标聚合，并以"端点注册表"作为对上层的唯一契约。
- **visionShell**：Qt Quick（QML）界面宿主可执行程序，是本仓库中唯一链接 Qt 的 target。加载产品 profile 和 QML 页面，经绑定层连接 service 端点。

训练、校验、注册、部署（模型中心）不进入界面；界面只保留消费侧能力（选择模型包、查看版本）。

- **visionDesigner**：界面搭建工具，位于 `Tools/uiDesigner/`，Qt Quick 可执行程序。读取 service manifest（端点清单），以拖拽方式编辑产品 profile（相机配置 + 界面控件树），输出"配置即界面"的产品包。

**总体验收目标（闭环）**：从 visionDesigner 新建产品开始，完成相机/端点绑定与界面搭建，发布产品包，visionShell 加载该产品包并跑通目录源检测——全程不修改任何 C++/QML 源码。

依赖方向单向不可逆：

```text
Shell/  visionShell (exe, Qt6 Quick)   唯一含 Qt 的 target
  └─ Binding 层（Shell 内模块）        端点注册表 → QML 动态属性/模型/命令
Service/  visionService (静态库, 纯 C++) 端点注册表、会话控制器、事件分发、profile 加载、帧分流
  └─ Runtime (visionRuntime, 静态库)   仅补运行时相机 setter 与必要观察点，不引入 Qt

Tools/uiDesigner/  visionDesigner (exe, Qt6 Quick)  读 manifest + profile/控件模板，写 profile；
                                                      不链接 Runtime/Service
```

Runtime 核心仍然不依赖 Qt，不接受任何来自 Shell/Service/Designer 的反向依赖。

## 2. 已确认的决策

- 界面技术：QML（Qt Quick），运行时动态加载，Designer 未来直接生成 QML 片段。
- 组件位置：本仓库新顶层目录 `Service/` 与 `Shell/`，作为 Runtime 的消费者，复用现有 preset 与 `vision_target_runtime()`。
- 相机运行时参数：在核心 `ICameraDevice` 层补运行时 setter，不做"停 session 重建"的绕行方案。
- 同进程部署；不为跨进程传输预留序列化层，但端点抽象（命令/状态/图像分离）天然兼容未来的远程壳。
- Service 层自行组装 source/pipeline/executor/session（参照 `Samples/anomalyHikMvsMulti`），不使用 `AnomalyPreset` 门面，以换取帧分流与端点控制的自由度。

## 3. 端点抽象

端点是 Service 层对 UI 暴露的唯一概念，Binding 层只认识端点，不认识领域语义（七相机、HIL 等）。领域语义全部来自注册表与产品 profile。

| 端点类型 | 内容 | 示例 |
| --- | --- | --- |
| Parameter | 可读写的值，带类型、取值范围、读写性、授权级别 | 曝光、增益、触发模式、异常阈值 |
| Command | 幂等动作，返回 `core::Result` | 启动/停止、软触发、应用规格 |
| State | 只读状态流，版本化快照 + 订阅回调 | 就绪状态、帧计数、FPS、p50/p95/p99 |
| Stream | 图像帧流（`vision::Frame` 句柄 + sourceId） | 各路相机实时画面 |

订阅回调统一在 service 专用分发线程串行投递，UI 侧经 `Qt::QueuedConnection` 回 GUI 线程；UI 线程不直接调用任何 Runtime API。

## 4. 产品 profile

产品差异全部收敛到一个 profile JSON（带 `schemaVersion`，格式约定仿 `deployment.json`），内容包括：

- 相机列表与角色（序列号/IP、像素格式、默认曝光/增益、触发方式）。
- source/pipeline/deployment 组装参数（模型包路径、backend、队列容量、阈值等）。
- 暴露的端点清单（哪些参数/命令/状态/流对界面可见，授权级别）。
- 界面定义：页面列表、每页的控件树（控件类型、布局属性、端点绑定）。Shell 按此节在运行时实例化控件，而非逐产品手写 QML 文件。

产品包布局（publish 产物，也是 Designer 的输出单元）：

```text
<product>/
├─ profile.json          # 上述全部产品配置与界面定义
├─ theme/                # 可选：颜色/字体/暗色主题覆盖
└─ assets/               # 可选：图标、logo 等资源
```

Service 层按 profile 完成组装并注册端点；manifest 导出接口把注册表序列化为 JSON，供 Designer 拉取端点清单。

## 5. 实施阶段与验收标准

每个阶段独立交付、独立验收，验收通过后再进入下一阶段。

### 阶段 A — Runtime 核心增量

范围最小，是阶段 B 调参功能的前置。

工作内容：

1. `Runtime/include/camera/iCameraDevice.hpp` 增加运行时参数接口：`setExposureMicroseconds(double)`、`setGain(double)`，返回 `core::Result<void>`。
2. `HikrobotMvsCameraDevice` 实现 setter（写 MVS SDK 节点，关闭 Auto 后写值）；无相机 SDK 的构建（`VISION_CAMERA_SDK=NONE`）返回"不支持"错误码。
3. 验证帧分流可行性：Service 层在自有 `FrameCallback` 包装中把帧转发给 session 之前先派发到 Stream 端点；按 `FrameRetentionPolicy` 语义确认是否需要复制/保留 buffer。原则上不新增核心 API，实测不可行时再评估。

   验证结论（2026-09-14）：可行，无需新增核心 API。`TensorBuffer` 为共享所有权，Service 在自有 `FrameCallback` 包装内用 `frame.buffer()` 构造第二个 `Frame` 派发到 Stream 端点即可，零像素拷贝；MVS SDK buffer 在最后一个 Frame/Buffer 视图释放时才归还。默认 `FrameRetentionPolicy`（预处理后释放相机帧）与分流互不冲突，但 UI 侧视图会延长 SDK buffer 租约，`maxFramesInFlight` 需覆盖 session 在途帧与 UI 在途帧之和。

验收标准：

- camera setter 契约测试（NONE 构建下的不支持路径）通过；HikMVS 实机 smoke（可基于 `Runtime/tools/hikMvsCaptureSmoke.cpp` 扩展）通过。
- 现有测试全绿：`MinGW Anomaly Test Build`、`MSVC HikMvs Sample Build` 任务通过。

### 阶段 B — visionService 库

完成记录（2026-09-15）：

- `Service/visionService` 静态库落地：端点注册表（Parameter/Command/State/Stream）、专用分发线程、`SessionController` 状态机、`ProfileLoader`、`SessionAssembler`、`CameraService`、`manifestExporter`；`VISION_BUILD_SERVICE` 默认 OFF。
- Service gtest 19 项 + 全量 152 项通过；目录源端到端（fake backend 插件）以帧流/生命周期验证为主（fake backend 输出名硬编码 `"output"` 且输出形状=输入形状，不满足 anomaly postprocess 的 scalar 要求）。
- 关键约束：`MultiCameraSession` 由 SessionController 控制线程独占，`wait()` 只能由控制线程调用，`requestStop()` 只软通知；帧分流经 `MultiCameraSession::frameObserver` 零拷贝派发，VisionService 端必须共享持有帧向量 shared_ptr，不能 `std::move(*ptr)` 内容（会把 vector 清空）。

纯 C++，可与阶段 A 部分并行（A 完成前调参端点返回未实现错误）。

工作内容：

1. 端点注册表核心类型：Parameter/Command/State/Stream 描述结构与注册表；回调用 `std::function`；订阅回调在专用分发线程串行投递。
2. 会话控制器：生命周期状态机（Idle → Configuring → Running → Stopping），封装 `MultiCameraSession`/单相机 session 的创建、`start()`、`requestStop()`、`wait()`；遵守核心生命周期约定（只有控制线程调 `wait()`）。
3. profile 加载器：解析产品 profile JSON → 组装 `FrameSourceFactory`、`ModelPackageLoader`、`PipelineBuilder`、`createBatchExecutor`、session，并注册端点。
4. 相机服务：`enumerate()` 包装、参数读写端点（接阶段 A 的 setter）、软触发命令。
5. 指标聚合：挂 `TimedPipeline` 的 `PipelineTimingObserver`/`BatchPerformanceObserver` 与 per-source 统计，聚合为 State 端点（计数、FPS、p50/p95/p99）。
6. manifest 导出：注册表序列化为 JSON。
7. 测试：gtest 单测（注册表、状态机、profile 解析）+ `FileFrameSource` 目录源端到端集成测试（无需相机硬件）。

验收标准：

- Service gtest 套件通过；目录源端到端集成测试可启动、出结果、正常停止并输出统计。
- manifest 导出 JSON 与注册表内容一致。
- 不引入任何 Qt 依赖（以 target 链接关系为准）。

### 阶段 C — Binding 与 visionShell

验证记录（2026-09-16）：

- 图像通路 spike：`Frame`→`QImage` 零拷贝（deleter 持有 `TensorBuffer` 共享所有权，仅每帧一次 holder 堆分配，无像素拷贝）；`--smoke` 实测 UI 收帧与 session 入口 1:1（63/63，dropped=0），目录源有效速率受 Debug OpenVINO + block 回压限制（pipeline 4.5 fps，p50 3.4 ms），UI 不构成瓶颈。
- 动态 UI：控件经 `Shell/controls/NodeView.qml` 按 profile `ui` 节递归实例化；qmlcachegen 不允许静态递归类型，容器子节点经 `Loader` + URL 动态加载绕行。QML 子目录需在 main.qml 显式 `import "../controls"`。
- 闭环验收通过：`products/rubberRingCompact`（仅新 profile JSON，复用 rubberRing 模型与图片，布局改为 statNumber/resultTable 组合）smoke 跑通 64/64 帧、0 丢失。
- `publishShell` 发布根自包含验证通过（发布目录内 exe 直接 smoke 跑通，含 windeployqt Qt/MinGW 运行时与 OpenVINO 插件）。
- 回归：MinGW 全量 157 项测试通过；`VISION_BUILD_SHELL` 默认 OFF 不影响既有构建。
- 修复：`BatchPipelineExecutor::wait()` 补 `finishBatch()`，batch 模式下 `metrics.performance` 端点此前恒零。
- MSVC 支持已验证（2026-09-16）：`Build/MSVC-2026` 树以 `VISION_BUILD_SHELL=ON` + `CMAKE_PREFIX_PATH=D:/Qt-OpenSource/6.10.1/msvc2022_64` 配置，visionShell 与 publishShell 均构建通过（该树 OpenVINO 插件为 OFF，跑 openvino 产品需另开）。
- 实时统计补齐（2026-09-16）：`VisionService` 在 start 成功后拉专用 metrics 线程，每 500ms `publishLive()` 刷新 `session.summary`（经 `SessionController::liveCounters()` 直读运行中 session）与 `metrics.performance`（`TimedPipeline::livePerformanceSnapshot()` 当前批快照），批次结束由 publishSummary/batchObserver 覆盖最终值；`SessionController::completionCallback` 已接线（自然结束先停 metrics 线程再发最终值）。GUI 运行中 FPS/计数不再恒零。
- MSVC CRT 修复（2026-09-16）：`QQmlEngine::addImageProvider` 取得 provider 所有权，原传入栈对象导致引擎析构 delete 栈地址（MSVC debug 堆检出，MinGW 静默）；改为堆分配交由引擎管理。同次全工程 MSVC 统一动态 CRT。
- 遗留：多路画面与相机调参（阶段 A 联动）待相机硬件接入后实机验证；多次 start/stop 时 `TimedPipeline` 的 batch 统计不复位，第二轮起 live performance 为跨轮累计口径（沿用既有 finishBatch 限制，未加 beginBatch 钩子）。

工作内容：

1. CMake：`VISION_BUILD_SERVICE`、`VISION_BUILD_SHELL` 选项（默认 OFF）；Shell 内 `find_package(Qt6 COMPONENTS Quick)`；首期只接入 MinGW preset（`CMAKE_PREFIX_PATH` 已有 `D:/Qt/6.10.1/mingw_64`）。
2. **图像通路 spike（最先做）**：`Frame` → `QImage` 零拷贝包装（自定义 deleter 持有 buffer 引用计数）→ 自定义 `QQuickItem`/`QQuickImageProvider`；验证多路帧率与无每帧堆拷贝。
3. Binding 层：注册表 → `QQmlContext`/动态 `QAbstractListModel`；命令经 `Qt::QueuedConnection` 入 service 线程；状态/图像回 GUI 线程。
4. 通用控件集：状态灯卡、参数表单、命令按钮、结果表格、图像视图、统计数字——全部只绑定端点，不含领域语义。控件以 QML 组件形式实现，由 Shell 的页面装载器按 profile 界面定义节实例化（不逐产品生成 QML 文件）。
5. 参考产品界面：橡胶圈产品 profile（含界面定义）搭出生产监控/设备调试页，不修改任何代码。
6. publish target：仿 `Samples/anomalyDirectory`，组装 exe + `plugins/` + 产品包为自包含 release 根。

验收标准：

- spike：图像视图帧率 ≥ 相机（或目录源模拟）输出帧率，计数器断言无每帧堆分配拷贝。
- 参考界面可启动检测、显示多路画面、调参生效（联动阶段 A）、显示统计。
- **闭环验收：只改 profile JSON（不改任何代码）生成第二个产品界面并跑通目录源检测。**
- 回归：现有 MinGW/MSVC 构建与测试不受影响（新选项默认 OFF）。

### 阶段 D — visionDesigner 搭建工具

独立 Qt Quick 可执行程序，位于 `Tools/uiDesigner/`。只读写 profile JSON 与控件模板，不链接 Runtime/Service，因此可在无相机、无推理环境的开发机上运行。

工作内容：

1. **manifest 接入**：加载阶段 B 导出的 manifest JSON（或直连运行中的 service 拉取），左侧呈现可绑定的端点树（Parameter/Command/State/Stream，含类型、范围、授权级别）。
2. **产品装配**：编辑 profile 的产品侧——相机列表与角色、source/pipeline/deployment 参数、暴露端点子集。提供从模板新建产品（七相机橡胶圈为首个模板）。
3. **画布编辑**：中央画布按 profile 界面定义节实时渲染控件树；控件从控件集拖入，支持移动、删除、网格对齐、属性面板（布局属性 + 端点绑定下拉选择）。编辑结果写回 profile JSON。
4. **预览模式**：用模拟数据源（端点假值发生器：计数自增、正弦曲线、目录图片轮播）驱动画布，验证绑定正确性；可切换真实 service 连接做联调预览。
5. **校验与发布**：保存时按 schema 校验（端点引用存在、类型匹配、授权级别合法、必填绑定完整），错误定位到控件；发布输出产品包目录，可直接被 visionShell 加载。
6. **闭环验收**：Designer 新建产品 → 绑定端点与界面 → 发布 → Shell 加载 → 目录源检测跑通并出结果——全程不修改任何 C++/QML 源码。

验收标准：

- 新 target `visionDesigner` 在 MinGW preset 下构建通过，`VISION_BUILD_DESIGNER` 选项（默认 OFF）控制。
- 从空模板搭建一个双相机产品界面并保存，重新打开 Designer 后画布完整还原（往返一致）。
- 模拟数据源预览下，参数/命令/状态/图像四类绑定全部可验证。
- 发布的产品包被 visionShell 直接加载并通过目录源检测（即总闭环）。

### 阶段顺序与依赖

```text
A（核心 setter）──┐
                  ├──> B（visionService）──> C（Shell + 控件集）──> D（Designer）──> 总闭环
B 的调参部分依赖 A ┘                                     ▲
                          D 只依赖 B 的 manifest 与 C 的控件集，不改 B/C 架构
```

## 6. 构建与工程约定

- 遵循 `Docs/codingConventions.md`：命名、`Result<T>` 错误处理、显式源文件清单（不 glob）、`.hpp`/`.cpp` 扩展名。
- `Service/` 结构仿照 Runtime：`include/<module>/`、`src/<module>/`、`tests/`；通过 `vision_target_runtime()` 绑定 Runtime。
- Qt 只出现在 `Shell/` 与 `Tools/uiDesigner/`；`Service/` 的 CMake 中禁止出现 Qt 链接。
- 构建选项：`VISION_BUILD_SERVICE`、`VISION_BUILD_SHELL`、`VISION_BUILD_DESIGNER`，均默认 OFF，互不隐式开启；Shell 依赖 Service 时在选项层显式检查。
- Qt 作为平台依赖参考现有厂商 SDK 约定，通过 `CMAKE_PREFIX_PATH`/`Qt6_DIR` 解析，不把 Qt 路径写入 Runtime 核心。
- Designer 与 Shell 共享同一套控件集：控件 QML 组件放在 `Shell/controls/`，Designer 通过文件路径引用（开发期）或 qrc 嵌入（发布期），避免两份实现漂移。

## 7. 明确不做（本计划范围外）

- session pause/resume、运行中更换模型或 deployment。
- 跨进程/远程传输层。
- 手写 QML 的解析与反向编辑；Qt Creator/.ui 工作流不作为搭建路径。
- Designer 的撤销/重做、多用户协作、版本管理——首版保存即覆盖 profile，版本由 git 管理。
- `logs/` 子系统（logger、metrics registry）补全——指标需求由阶段 B 的聚合器在 service 内自足。

## 8. 风险

1. Qt for MSVC 是否安装未确认；首期 Shell/Designer 只支持 MinGW preset，MSVC 支持列为阶段 C 验证项。
2. 原图显示与 `FrameRetentionPolicy`（默认预处理后释放）的交互在阶段 C spike 实测；兜底方案是 service 侧分流时复制到自有缓冲池（每帧一次拷贝，可接受）。
3. `MultiCameraSession` 仍属增量实施中的 API；封装在 Service 层内，核心 API 变动只影响 service 一层，不波及 UI 与 Designer。
4. 界面定义放在 profile JSON 意味着界面表达能力受控件集约束；这是有意取舍——换得 Designer 的往返一致与零代码目标。出现控件集无法表达的需求时，优先扩控件集，不允许产品侧塞手写 QML 逃逸。
