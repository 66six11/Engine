# API Reference：Core

## `asharia::Error`

表示带 subsystem 分类的 engine error。

| 字段 | 类型 | 说明 |
|---|---|---|
| `domain` | `ErrorDomain` | 子系统分类 |
| `code` | `int` | 子系统内错误码 |
| `message` | `std::string` | 可读错误上下文 |

常用 domains 包括 `Core`、`Platform`、`Vulkan`、`Shader`、`RenderGraph`、`Asset`、`Reflection`、`Serialization`、`Schema`、`Archive`、`CppBinding`、`Persistence`、`Scene`、`Project`、`Material`。

## `Result<T>`

```cpp
template <typename T>
using Result = std::expected<T, Error>;
```

用于返回值或 error 的 failable API。

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

## 日志 API

`log(LogLevel level, std::string_view message, std::source_location location)` 写入带 source location 的日志。便捷函数：

| 函数 | 说明 |
|---|---|
| `logTrace(message)` | Trace level |
| `logInfo(message)` | Info level |
| `logWarning(message)` | Warning level |
| `logError(message)` | Error level |

## 验证方式

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

检查点：

- Failable constructor work 留在 `create()` 或 explicit function 中，并返回 `Result<T>`。
- `VkResult` 或 parse error 离开 subsystem 前转换成 `asharia::Error`。
