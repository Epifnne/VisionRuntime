# VisonRuntime 编码约定

## 1. 命名风格

仓库不使用下划线分隔普通 C++ 标识符和项目自有文件名。命名必须采用以下风格：

| 对象 | 风格 | 示例 |
|---|---|---|
| 类、结构体、枚举、类型别名 | UpperCamelCase | `PipelineExecutor`、`ModelManifest` |
| 函数、成员函数 | lowerCamelCase | `prepareModel()`、`submitFrame()` |
| 局部变量、参数、非静态成员 | lowerCamelCase | `taskId`、`modelPath` |
| 文件名 | lowerCamelCase | `pipelineExecutor.hpp`、`modelManifest.hpp` |
| 目录名 | lowerCamelCase | `preProcess`、`postProcess` |
| namespace | lowerCamelCase | `visonRuntime`、`preProcess` |
| 枚举值 | UpperCamelCase | `QueueFull`、`BuildIfMissing` |
| 编译期常量 | `k` + UpperCamelCase | `kDefaultQueueSize` |
| 宏 | UPPER_SNAKE_CASE | `VISON_RUNTIME_EXPORT` |

宏是唯一允许使用下划线分隔的 C++ 名称；第三方 API、编译器预定义宏和外部协议字段保持其原始命名。

## 2. C++ 类型

- 所有 class、struct、enum class 和 using 定义的类型均使用 UpperCamelCase。
- 接口类使用 `I` 前缀，例如 `IInferenceBackend`、`IPreprocessor`。
- 不使用 `_t` 后缀表达普通项目类型。
- 缩写按普通单词处理，避免整段大写：`OnnxRuntimeBackend`、`OpenVinoBackend`、`TensorRtBackend`、`OcrResult`。
- RAII 所有权类型的名称应表达资源，例如 `TensorBuffer`、`ModelHandle`。

## 3. 文件和目录

- 一个主要公共类型对应一个同名 lowerCamelCase 头文件。
- 公共声明放在 `Runtime/include/<module>/`，内部实现放在 `Runtime/src/<module>/`。
- 文件扩展名统一使用 `.hpp` 和 `.cpp`。
- 测试文件采用 `<targetName>Test.cpp`，例如 `tensorTest.cpp`。
- CMake、Markdown、JSON Schema 和第三方文件遵循各自生态惯例，不强制改为驼峰。

## 4. 示例

```cpp
namespace visonRuntime::executor {

class PipelineExecutor {
public:
    Result<TaskHandle> submitFrame(Frame frame);

private:
    static constexpr std::size_t kDefaultQueueSize = 16;
    std::size_t queueCapacity_;
};

} // namespace visonRuntime::executor
```

成员变量允许使用结尾下划线区分成员与参数；该下划线是后缀，不是单词分隔符。除宏、成员变量后缀和外部名称外，不新增 snake_case 名称。
