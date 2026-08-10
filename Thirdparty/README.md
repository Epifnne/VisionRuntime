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

依赖版本、下载地址和校验值应在引入依赖时记录到本文档，确保其他开发环境可以复现。

## 已下载依赖

| 依赖 | 版本 | 源码地址 | 固定提交 |
| --- | --- | --- | --- |
| OpenCV | 4.12.0 | `git@github.com:opencv/opencv.git` | `49486f61fb25722cbcf586b7f4320921d46fb38e` |
| GoogleTest | 1.17.0 | `git@github.com:google/googletest.git` | `52eb8108c5bdec04579160ae17225d66034bd723` |