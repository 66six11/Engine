# API Reference: Core

## `asharia::Error`

Represents a typed engine error.

Fields:

| Field | Type | Description |
|---|---|---|
| `domain` | `ErrorDomain` | Subsystem category |
| `code` | `int` | Subsystem-local error code |
| `message` | `std::string` | Human-readable error context |

Common domains:

| Domain | Use case |
|---|---|
| `Core` | Infrastructure errors |
| `Platform` | Window/platform errors |
| `Vulkan` | Vulkan/RHI creation, submission, and resource errors |
| `Shader` | Shader compile/reflection errors |
| `RenderGraph` | Graph declaration/compile/execute errors |
| `Asset` | Asset metadata/catalog/pipeline errors |
| `Reflection` | Runtime reflection errors |
| `Serialization` | Serialize/deserialize/migration errors |
| `Schema` | Schema registry errors |
| `Archive` | Archive parse/write errors |
| `CppBinding` | C++ binding errors |
| `Persistence` | Persistence/migration errors |
| `Scene` | Scene world errors |
| `Project` | Project descriptor errors |
| `Material` | Material validation/hash errors |

## `Result<T>`

```cpp
template <typename T>
using Result = std::expected<T, Error>;
```

Use when a function returns either a value or an error.

Return values:

| Type | Description |
|---|---|
| `T` | Success result |
| `Error` | Failure result with domain, code, and message preserved |

Example:

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

Use when a function can fail but does not return a value.

Example:

```cpp
asharia::VoidResult registered = registry.freeze();
if (!registered) {
    return registered;
}
```

## `log(LogLevel, message, location)`

Writes a log message with source location.

Parameters:

| Parameter | Type | Required | Description |
|---|---|---|---|
| `level` | `LogLevel` | yes | `Trace`, `Info`, `Warning`, `Error` |
| `message` | `std::string_view` | yes | Log content |
| `location` | `std::source_location` | no | Defaults to the call site |

Helper functions:

| Function | Description |
|---|---|
| `logTrace(message)` | Trace level |
| `logInfo(message)` | Info level |
| `logWarning(message)` | Warning level |
| `logError(message)` | Error level |

Errors:

| Error | Trigger |
|---|---|
| No returned error | The logger API currently does not return `Result` |

## Validation

Compile users of the core API:

```powershell
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

Checkpoints:

- Failable constructors stay as `create()` or explicit functions returning `Result<T>`.
- `VkResult` or parse errors are converted to `asharia::Error` with context before leaving the subsystem.
