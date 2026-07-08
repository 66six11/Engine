# API Reference：Core

## `asharia::Error`

表示带 subsystem 分类的 engine error。

| 字段 | 类型 | 说明 |
|---|---|---|
| `domain` | `ErrorDomain` | 子系统分类 |
| `code` | `int` | 子系统内错误码 |
| `message` | `std::string` | 可读错误上下文 |

常用 domains：

| Domain | 使用场景 |
|---|---|
| `Core` | 基础设施错误 |
| `Platform` | window/platform 错误 |
| `Vulkan` | Vulkan/RHI 创建、提交和资源错误 |
| `Shader` | shader compile/reflection 错误 |
| `RenderGraph` | graph declaration/compile/execute 错误 |
| `Asset` | asset metadata/catalog/pipeline 错误 |
| `Reflection` | runtime reflection 错误 |
| `Serialization` | serialize/deserialize/migration 错误 |
| `Schema` | schema registry 错误 |
| `Archive` | archive parse/write 错误 |
| `CppBinding` | C++ binding 错误 |
| `Persistence` | persistence/migration 错误 |
| `Scene` | scene world 错误 |
| `Project` | project descriptor 错误 |
| `Material` | material validation/hash 错误 |

## `Result<T>`

```cpp
template <typename T>
using Result = std::expected<T, Error>;
```

用于返回值或 error 的 failable API。

返回值：

| 类型 | 说明 |
|---|---|
| `T` | 成功结果 |
| `Error` | 失败结果，保留 domain、code 和 message |

```cpp
asharia::Result<asharia::VulkanContext> context =
    asharia::VulkanContext::create(desc);
if (!context) {
    asharia::logError(context.error().message);
    return EXIT_FAILURE;
}
```

## `VoidResult`

```cpp
using VoidResult = std::expected<void, Error>;
```

用于无返回值但可能失败的 API。

```cpp
asharia::VoidResult registered = registry.freeze();
if (!registered) {
    return registered;
}
```

## `log(LogLevel, message, location)`

写入带 source location 的日志。

参数：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `level` | `LogLevel` | 是 | `Trace`、`Info`、`Warning`、`Error` |
| `message` | `std::string_view` | 是 | 日志内容 |
| `location` | `std::source_location` | 否 | 默认是调用点 |

便捷函数：

| 函数 | 说明 |
|---|---|
| `logTrace(message)` | Trace level |
| `logInfo(message)` | Info level |
| `logWarning(message)` | Warning level |
| `logError(message)` | Error level |

错误：

| 错误 | 触发条件 |
|---|---|
| 无返回错误 | logger API 当前不返回 `Result` |

## 验证方式

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

检查点：

- Failable constructor work 留在 `create()` 或 explicit function 中，并返回 `Result<T>`。
- `VkResult` 或 parse error 离开 subsystem 前转换成 `asharia::Error`。
