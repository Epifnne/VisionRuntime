# 可双击运行的发布包计划

## 产品专属运行时补充

使用 sample 自组装流水线或业务回调时，先运行 `Samples/buildShell.cmd <sample>`
（Windows MSVC x64，脚本顶部配置 Qt/VS 路径）或 `sh Samples/buildShell.sh <sample>`。
不传参数默认 `anomalyDirectory`；直接更新 `Publish/release/`。
套件包含 `visionDesigner/`、`runtimeTemplates/<windows|linux>/`、起始 profile 与
`design.cmd`/`design.sh`，只包含本次产品/平台的 Shell、所选后端和必要运行依赖。
构建不需要模型包或回放图片，不进行最终产品密封，不启动采集。
暂存部署全部成功才替换旧套件；用户项目和最终产品请保存到套件之外。

运行套件中的 `design.cmd`/`design.sh`。Designer 默认发现相邻的 `runtimeTemplates/<platform>`，
优先于旧全局设置；当前会话明确选择的其他模板仍生效。
在 Designer 中选择模型与资源、组装 GUI 并发布最终 `bin/` + `products/` + 入口脚本包。
Designer 不负责编译 C++。
专属 Shell 接管 sample 的 Runtime，保留原业务回调，仅附加 GUI 观察者。
`runtimeProduct` 与 `runtime-version.json.productRuntime` 绑定产品，避免使用通用或其他产品的
Shell 时静默丢失业务逻辑。以下通用 Shell 发布流程仍适用于默认流水线产品。

> v1.0 发布加固：发布现在检查复制和脚本写入失败、所选后端插件、模型 manifest 与目录源资源；
> 冲突的运行时文件拒绝混用。产品包新增 `release-manifest.json` SHA-256 校验以及
> `publishing.lock` 未完成标记。Shell 启动校验清单，并支持 `--check` 预检。
> 目录源为空或缺失不再仅警告，而是阻止发布。`assembleRelease.cmd` 输出已存在时拒绝覆盖，
> 请使用新的版本化目录。实际发布门禁与支持范围以 [README](../README.md#v10-contract-and-release-gates) 为准。

> 状态（2026-09-23）：**阶段 1、2 已实现**。Designer 发布对话框可选 Windows/Linux 平台，
> 打包 `bin/` + 重写 profile + 生成入口脚本；`publishShell` 支持 Linux（无 windeployqt，
> 目标机需预装 Qt 6.10+）；新增 `publishDesigner` target。本文档保留为设计与分发指南。

## 1. 目标与定位

让 visionDesigner 的「发布」产出**自包含、可复制、双击即可运行**的产品运行包：
把发布目录拷到任何一台无开发环境的 Windows 机器（装有对应相机 SDK 运行时），用户双击入口即启动
visionShell 并加载该产品，不需要安装 Qt、不需要本仓库、不需要命令行。

现状差距：

- Designer 的 `publish()` 只写 `<目标目录>/<产品id>/profile.json`——模型包、后端插件、目录源图片、
  visionShell 可执行文件与 Qt 运行时都不在包内，profile 里的相对路径（`../rubberRing/model` 等）
  发布后立即悬空。
- Shell 侧已有 `publishShell` target（windeployqt 自包含 release 根），但它是**构建期**固定打包
  `Shell/products/` 下既有产品，与 Designer 在**运行期**发布新产品互不衔接。

目标布局（发布到用户选定目录后）：

```text
<发布目录>/                     # 整个目录即可拷贝分发（按平台分别产出）
├─ 启动-<产品名>.cmd            # Windows 双击入口
├─ run-<产品名>.sh              # Linux 入口（chmod +x；GUI 下可从桌面双击）
├─ bin/
│   ├─ visionShell(.exe)
│   ├─ Qt 运行时               # Windows: windeployqt 产出；Linux: 见第 5 节
│   └─ plugins/                 # 后端插件（.dll / .so）
├─ products/<产品id>/
│   ├─ profile.json            # 路径已重写为包内相对路径
│   ├─ model/                  # 模型包（从 packagePath 复制）
│   ├─ assets/                 # 目录源图片等（从各 directory 源复制）
│   └─ theme/                  # 可选
└─ ThirdpartyRuntime/           # 相机 SDK 运行时提示或随附（见第 5 节风险）
```

**发布包是平台相关的**：Windows 构建产出的 exe/dll 无法在 Linux 运行，反之亦然。
因此「发布」永远发布**当前构建平台**的包；需要 Linux 包时，在 Linux 构建树
（`Build/Linux-GNU`）里构建 `publishShell`，再用该平台的 Designer 执行发布。

## 2. 已确认的决策

- **复用而非重造打包**：发布包的 bin/ 直接复制 `publishShell` target 的产物
  （exe + Qt 运行时 + plugins），Designer 不自己实现 windeployqt 逻辑。
  Designer 发布时调用/读取构建树中已生成的 release 根。
- **profile 路径重写**：发布时把 `model.packagePath`、`model.pluginDirectory`、各目录源
  `directory` 解析为绝对路径 → 复制进产品包 → 重写为包内相对路径
  （`model`、`plugins`、`assets/<sourceId>`）。保存（save）语义不变，仍是用户指定文件原样写出。
- **入口脚本按平台生成**：Windows 生成 `启动-<产品名>.cmd`（UTF-8 BOM，双击调用
  `bin\visionShell.exe products\<id>\profile.json`）；Linux 生成 `run-<产品名>.sh`
  （`#!/bin/sh` + `exec "$(dirname "$0")/bin/visionShell" "$(dirname "$0")/products/<id>/profile.json"`，
  发布时置可执行位）。仓库已有 `setup_vars.cmd`/`setup_vars.sh` 双脚本惯例可循。
  后续可替换为 Windows 快捷方式（.lnk）或 Linux `.desktop` 文件。
- Designer 仍不链接 Runtime/Service；打包只做文件复制与 JSON 路径改写。

## 3. 实施阶段

### 阶段 1 — Designer 发布流程增强（核心）

`Tools/uiDesigner/src/designerBackend.cpp` 的 `publish()` 扩展为完整打包：

1. 校验通过后，解析 profile 中的资源路径（相对路径以 profile 所在目录为基准）：
   - `model.packagePath` → 复制到 `products/<id>/model/`，重写为 `model`；
   - `model.pluginDirectory` → 对应后端插件 DLL 复制到 `bin/plugins/`
     （若与 release 根已有插件重复则跳过），profile 中重写为包内相对路径；
   - 每个 `type: "directory"` 源的 `directory` → 复制到 `products/<id>/assets/<sourceId>/`，
     重写为该相对路径；目录为空或不存在时给 warning 不阻断发布。
2. 复制运行时装载：从构建树 release 根（`Build/<preset>/Shell/release/visionShell` 或
   显式配置的 designer 侧"运行时模板目录"）复制 `bin/` 到发布目录。
   release 根不存在时给出明确错误并提示先构建 `publishShell`。
3. 生成入口脚本 `启动-<产品名>.cmd`（UTF-8 with BOM，中文 Windows cmd 兼容）。
4. Designer 设置页（或 QSettings）记录"运行时模板目录"，默认自动探测构建树。

验收：桌面发布 `designer-dual`（目录源产品）后，整个发布目录拷到另一分区，
双击入口脚本跑通目录源检测界面，不报缺文件。

### 阶段 2 — 构建侧 release 根稳定化

1. `publishShell` 保持唯一职责：产出 `<build>/Shell/release/visionShell` 自包含根
   （已有），确认其在 MinGW release preset 下稳定可用；MSVC 树按需另行验证。
2. 顶层增加 `publishDesigner` target：产出 Designer 自身可分发的 release 包
   （visionDesigner.exe + Qt 运行时），供"设计端"分发——与产品运行包区分。
3. CI/release 流程：release 构建一次产出三件套——visionShell 运行根、
   visionDesigner 设计包、（可选）文档。

验收：`cmake --build --preset <release> --target publishShell publishDesigner`
一次构建产出两个 release 根。

### 阶段 3 — 相机产品实机验收

1. 发布一个 hikrobot 相机产品包（serialNumber 或 ipAddress 接入），
   目录拷贝到装有大恒/海康 SDK 运行时的目标机。
2. 双击入口 → 界面启动 → 开始检测 → 调参端点生效。
3. 记录相机 SDK 运行时的分发方式（随包携带 redist 或要求预装），写回本文档第 5 节。

## 4. 验收标准（总闭环升级）

- Designer 新建/编辑产品 → 校验通过 → 发布 → **发布目录整体拷贝到无仓库机器** →
  双击入口运行检测界面 → 出结果。全程不修改任何 C++/QML 源码，不需要命令行。
- 包内所有路径为相对路径，发布目录可整体移动/重命名。
- profile 保存（save）与发布（publish）语义清晰分离：save 写工作文件，publish 产运行包。

## 5. 风险与遗留

- **相机 SDK 运行时**：海康 MVS 等需要目标机装驱动/运行时库；首版在入口脚本检测
  DLL 缺失时给出明确提示，不随包分发厂商运行时（许可证需单独确认）。
- **Linux Qt 运行时收集**：`windeployqt` 无官方 Linux 等价物。可选路线按优先级：
  (1) 目标机用系统 Qt 包（要求发行版有 Qt 6.10+，入口脚本 `ldd` 检测缺失并提示
  `apt install` 命令）；(2) 第三方 `linuxdeployqt`（年久失修，需评估）；
  (3) AppImage 打包。首版建议路线 (1)，把"目标机装 Qt 运行时"列为 Linux 发布前提，
  入口脚本做依赖自检；Windows 保持 windeployqt 全量随包。
- **模型包体积**：目录源图片与模型可能很大，复制打包慢；首版全量复制，后续评估
  硬链接/增量选项。
- **插件目录语义**：`pluginDirectory` 当前指开发机上的插件构建目录；发布后被包内
  `bin/plugins` 取代，profile 重写时需注意 visionShell 的默认插件搜索路径约定
  （以 `Service` 层 `pluginInferenceBackend` 的解析为准）。
- **多产品同包**：同一发布目录发布多个产品时共享 `bin/`，入口脚本每产品一个；
  profile id 冲突时拒绝覆盖，提示改名。

## 6. 构建发行版（含双平台运行时模板）

设计端分发的 release 包 = visionDesigner（Windows）+ 两套 `runtimeTemplates`
（windows / linux），让 Designer 发布时可任选平台。构建全程 4 线程。

### Windows（MinGW Release）

```bat
cmake --preset mingw-release ^
  -DVISION_BUILD_SERVICE=ON -DVISION_BUILD_SHELL=ON ^
  -DVISION_BUILD_DESIGNER=ON -DVISION_BUILD_OPENVINO_PLUGIN=ON
cmake --build --preset mingw-release --target publishShell publishDesigner
```

产物：
- `Build/Release/Shell/release/visionShell`（windeployqt 自包含运行根）
- `Build/Release/Tools/uiDesigner/release/visionDesigner`（设计器，自带 Shell/controls）

### Linux（WSL / Ubuntu）

```bash
# 一次性：装 Qt 6.10.1（aqtinstall，用户级）+ 系统 OpenGL/X11 开发库（sudo apt）
#   见上文；并用 aqt 补 --archives icu（qmlimportscanner 需要）。
# 配置/构建时务必用干净 PATH（不含 Windows 的 MinGW Qt）并设 LD_LIBRARY_PATH：
export PATH=$HOME/Qt/6.10.1/gcc_64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export LD_LIBRARY_PATH=$HOME/Qt/6.10.1/gcc_64/lib
cmake -B Build/Linux-GNU-Release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=$HOME/Qt/6.10.1/gcc_64 \
  -DVISION_BUILD_SERVICE=ON -DVISION_BUILD_SHELL=ON \
  -DVISION_BUILD_DESIGNER=ON -DVISION_BUILD_OPENVINO_PLUGIN=ON
cmake --build Build/Linux-GNU-Release --target publishShell publishDesigner -- -j 4
```

产物：
- `Build/Linux-GNU-Release/Shell/release/visionShell`（exe + plugins + products，
  目标机需预装 Qt 6.10+ 运行时）
- `Build/Linux-GNU-Release/Tools/uiDesigner/release/visionDesigner`

### 组装分发包

```bat
Tools\assembleRelease.cmd
```

产出 `Publish/release/`：
```
Publish/release/
├─ visionDesigner/            # Windows 设计器（双击即用）
└─ runtimeTemplates/
   ├─ windows/                # visionShell Windows 运行根
   └─ linux/                  # visionShell Linux 运行根
```

把 `Publish/release/` 拷给设计机器，在 Designer 的发布对话框里把
「运行时模板目录」分别指向 `runtimeTemplates/windows` 与 `runtimeTemplates/linux`
（仅首次；之后 QSettings 记住），即可任选目标平台发布产品包。
