# Thirdparty

项目不使用 vcpkg。第三方库的预编译包或源码包直接下载并解压到此目录，推荐布局：

```text
Thirdparty/
└─ <package>/
   └─ <version>/
      ├─ include/
      ├─ lib/
      └─ bin/
```

`Thirdparty` 中的依赖文件属于本机开发环境，不提交到 Git。各依赖通过 CMake imported target 隔离；在 CMake 配置中使用 `<PackageName>_ROOT` 或专用 cache variable 指向实际目录，不将第三方头文件路径传播到无关模块。

依赖版本、下载地址和固定提交应在引入依赖时记录到本文档，确保其他开发环境可以复现。运行 CMake target `bootstrapDependencies` 可按下表提交下载或核验全部基础依赖。

## 已下载依赖

| 依赖 | 版本 | 源码地址 | 固定提交 |
| --- | --- | --- | --- |
| OpenCV | 4.12.0 | `git@github.com:opencv/opencv.git` | `49486f61fb25722cbcf586b7f4320921d46fb38e` |
| GoogleTest | 1.17.0 | `git@github.com:google/googletest.git` | `52eb8108c5bdec04579160ae17225d66034bd723` |
| nlohmann/json | 3.12.0 | `https://github.com/nlohmann/json.git` | `55f93686c01528224f448c19128836e7df245f72` |
| spdlog | 1.15.3 | `https://github.com/gabime/spdlog.git` | `6fa36017cfd5731d617e1a934f0e5ea9c4445b13` |
| OpenVINO | 2026.2.1 | `https://pypi.org/project/openvino/2026.2.1/` | PyPI 2026.2.1 platform wheel |
| Hikrobot MVS | 4.8.1 | 海康机器人 MVS 官方开发包 | 厂商版本 4.8.1 |

OpenVINO 使用平台隔离目录，CMake 会按目标平台自动查找：

```text
Thirdparty/openvino/2026.2.1/
├─ windows-x86_64/openvino/cmake/OpenVINOConfig.cmake
└─ linux-x86_64/openvino/cmake/OpenVINOConfig.cmake
```

Windows 安装命令：

```powershell
python -m pip install --target Thirdparty/openvino/2026.2.1/windows-x86_64 openvino==2026.2.1
```

WSL Ubuntu 安装命令（在仓库根目录执行）：

```bash
python3 -m pip install --target Thirdparty/openvino/2026.2.1/linux-x86_64 openvino==2026.2.1
```

这些平台包属于本机依赖并由 `.gitignore` 排除。需要使用其他位置时，可显式设置 `OpenVINO_DIR` 指向包含 `OpenVINOConfig.cmake` 的目录。

海康 MVS 4.8.1 使用平台隔离布局：

```text
Thirdparty/hik-mvs/4.8.1/
├─ include/MvCameraControl.h
├─ windows-x86_64/
│  ├─ lib/MvCameraControl.lib
│  └─ bin/MvCameraControl.dll
└─ linux-x86_64/lib/libMvCameraControl.so
```

CMake 按目标平台读取头文件和链接库，也可设置 `HIK_MVS_ROOT`。厂商的
`MvCamCtrlSDK_STD_V4.8.1_260729.zip` 提供 Linux Runtime，Windows 客户端的
`Development` 目录提供公共头文件和 `MvCameraControl.lib`。当前已迁入两个平台的
x64 运行库、公共头文件和 Windows import library。目标机仍需安装匹配版本的 MVS
驱动和系统服务。