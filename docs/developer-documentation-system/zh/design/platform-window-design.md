# 详细设计：Platform、GLFW Window 与 Profiling 支撑层

## 背景

基础运行支撑由 `engine/core`、`engine/platform`、`packages/window-glfw` 和 `packages/profiling` 提供。它们为 app 和 renderer 提供错误/结果类型、平台 target、窗口/surface 创建和 frame profiling 工具。

## 目标

- 用 `core` 提供 `Error`、`Result`、`VoidResult`、logging 和 version。
- 用 `platform` 作为平台基础 target。
- 用 `window-glfw` 创建 GLFW instance/window，并提供 Vulkan surface factory 需要的 native handle。
- 用 `profiling` 提供轻量 frame profiler。
- 保持这些基础层不依赖 renderer、RenderGraph、asset pipeline 或 editor。

## 非目标

- 不在 window package 中创建 Vulkan device。
- 不在 profiling 中决定 renderer pass structure。
- 不在 core 中引入平台窗口库。
- 不把 app smoke 逻辑放入基础 packages。

## 当前约束

- `engine/core` 是 static library。
- `engine/platform` 是 interface target，依赖 `core`。
- `window-glfw` 依赖 `core`、`platform`、Conan `glfw`，并 public 链接
  `VulkanHeaders`，因为 public API 暴露 `VkSurfaceKHR`。
- `profiling` 是 interface target。
- Vulkan surface 创建通过 `glfwCreateVulkanSurface(window, instance)` helper 暴露给 RHI context。

## 总体方案

Core 定义项目级错误和 result contract。Platform target 用于承接平台基础依赖。Window layer 管理 GLFW init/terminate 和 `GLFWwindow` lifetime，并暴露 framebuffer extent、poll events、native handle、required Vulkan extensions 和 surface creation helper。

Profiling 作为轻量工具层，不拥有 renderer 或 app state。App 负责把 profiler scope 放在 frame loop 或 smoke 中。

## 模块划分

| 模块/文件 | 职责 |
|---|---|
| `engine/core/include/asharia/core/error.hpp` | `ErrorDomain` 和 `Error` |
| `engine/core/include/asharia/core/result.hpp` | `Result<T>` 和 `VoidResult` |
| `engine/core/include/asharia/core/log.hpp` | logging API |
| `engine/platform/CMakeLists.txt` | platform interface target |
| `packages/window-glfw/include/asharia/window_glfw/glfw_window.hpp` | GLFW instance/window/surface helper |
| `packages/profiling/include/asharia/profiling/frame_profiler.hpp` | frame profiler |

## 数据结构

| 数据 | 关键字段 | 说明 |
|---|---|---|
| `Error` | domain、code、message | project error payload |
| `WindowDesc` | width、height、title、visible | window creation input |
| `WindowFramebufferExtent` | width、height | swapchain target extent |
| `GlfwInstance` | GLFW init owner | process-level GLFW lifetime |
| `GlfwWindow` | `GLFWwindow*` | window lifetime owner |

## API 设计

- `GlfwInstance::create()` initializes GLFW and returns move-only owner.
- `GlfwWindow::create(instance, desc)` creates a window tied to the instance lifetime.
- `glfwRequiredVulkanInstanceExtensions(instance)` returns extensions for `VulkanContextDesc`.
- `glfwCreateVulkanSurface(window, instance)` returns `Result<VkSurfaceKHR>`.
- `GlfwWindow::pollEvents()` keeps event pumping outside frame loop internals.

## 关键流程

### 正常流程

1. App creates `GlfwInstance`.
2. App creates `GlfwWindow`.
3. App queries required Vulkan instance extensions.
4. App passes surface factory into `VulkanContextDesc`.
5. App polls events and calls frame loop.

### 失败流程

- GLFW init/window creation fails：return `Result` error with last GLFW error message.
- Vulkan extension query fails：return `Result` error.
- Surface creation fails：return `Result` error.
- Framebuffer extent zero：app/frame loop should handle minimized window path.

### 边界流程

- Window package can create surface but cannot create device/swapchain.
- RHI owns Vulkan surface after context creation.
- Profiling records timing data but does not own frame loop.

## 生命周期

`GlfwInstance` must outlive `GlfwWindow`. `GlfwWindow` must outlive Vulkan surface creation and app event loop. Vulkan context owns/destroys its surface. Profiling scopes are per-frame/per-region values.

## 错误处理

GLFW errors are converted to project `Error` through helper functions. Surface creation errors must include operation context and `VkResult` when available.

## 测试方案

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-window
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-vulkan
```

## 风险

- Destroying GLFW instance before window invalidates native handles. Mitigation: ownership order is explicit.
- Putting swapchain ownership into window layer breaks RHI boundary. Mitigation: window only creates surface.
- Profiling code can accidentally become renderer policy. Mitigation: profiler stays data collection only.
