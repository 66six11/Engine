# Detailed Design: Platform, GLFW Window, And Profiling Support

## Background

Base runtime support is provided by `engine/core`, `engine/platform`, `packages/window-glfw`, and `packages/profiling`. These pieces provide error/result types, platform target, window/surface creation, and frame profiling tools for apps and renderer code.

## Goals

- Use `core` for `Error`, `Result`, `VoidResult`, logging, and version.
- Use `platform` as the base platform target.
- Use `window-glfw` to create GLFW instance/window and expose the native handle needed for Vulkan surface creation.
- Use `profiling` for lightweight frame profiling.
- Keep these base layers independent from renderer, RenderGraph, asset pipeline, and editor.

## Non-Goals

- Do not create Vulkan devices in the window package.
- Do not decide renderer pass structure in profiling.
- Do not introduce platform window libraries into core.
- Do not put app smoke logic into base packages.

## Current Constraints

- `engine/core` is a static library.
- `engine/platform` is an interface target and depends on `core`.
- `window-glfw` depends on `core`, `platform`, and Conan `glfw`.
- `profiling` is an interface target.
- Vulkan surface creation is exposed through `glfwCreateVulkanSurface(window, instance)` for RHI context setup.

## Overall Design

Core defines project error and result contracts. The platform target carries platform-level dependencies. The window layer owns GLFW init/terminate and `GLFWwindow` lifetime, and exposes framebuffer extent, event polling, native handle, required Vulkan extensions, and surface creation helper.

Profiling is a lightweight utility layer. It does not own renderer or app state; apps place profiler scopes in frame loop or smoke code.

## Module Breakdown

| Module/file | Responsibility |
|---|---|
| `engine/core/include/asharia/core/error.hpp` | `ErrorDomain` and `Error` |
| `engine/core/include/asharia/core/result.hpp` | `Result<T>` and `VoidResult` |
| `engine/core/include/asharia/core/log.hpp` | logging API |
| `engine/platform/CMakeLists.txt` | platform interface target |
| `packages/window-glfw/include/asharia/window_glfw/glfw_window.hpp` | GLFW instance/window/surface helper |
| `packages/profiling/include/asharia/profiling/frame_profiler.hpp` | frame profiler |

## Data Structures

| Data | Key fields | Notes |
|---|---|---|
| `Error` | domain, code, message | project error payload |
| `WindowDesc` | width, height, title | window creation input |
| `WindowFramebufferExtent` | width, height | swapchain target extent |
| `GlfwInstance` | GLFW init owner | process-level GLFW lifetime |
| `GlfwWindow` | `GLFWwindow*` | window lifetime owner |

## API Design

- `GlfwInstance::create()` initializes GLFW and returns a move-only owner.
- `GlfwWindow::create(instance, desc)` creates a window tied to instance lifetime.
- `requiredGlfwVulkanInstanceExtensions()` returns extensions for `VulkanContextDesc`.
- `glfwCreateVulkanSurface(window, instance)` returns `Result<VkSurfaceKHR>`.
- `GlfwWindow::pollEvents()` keeps event pumping outside frame-loop internals.

## Key Flows

### Normal Flow

1. App creates `GlfwInstance`.
2. App creates `GlfwWindow`.
3. App queries required Vulkan instance extensions.
4. App passes surface factory into `VulkanContextDesc`.
5. App polls events and calls frame loop.

### Failure Flow

- GLFW init/window creation fails: return `Result` error with last GLFW error message.
- Vulkan extension query fails: return `Result` error.
- Surface creation fails: return `Result` error.
- Framebuffer extent is zero: app/frame loop should handle minimized window path.

### Boundary Flow

- Window package can create surface but cannot create device/swapchain.
- RHI owns Vulkan surface after context creation.
- Profiling records timing data but does not own frame loop.

## Lifetime

`GlfwInstance` must outlive `GlfwWindow`. `GlfwWindow` must outlive Vulkan surface creation and app event loop. Vulkan context owns/destroys its surface. Profiling scopes are per-frame/per-region values.

## Error Handling

GLFW errors are converted to project `Error` through helper functions. Surface creation errors must include operation context and `VkResult` when available.

## Test Plan

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-window
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-vulkan
```

## Risks

- Destroying GLFW instance before window invalidates native handles. Mitigation: ownership order is explicit.
- Putting swapchain ownership into window layer breaks RHI boundary. Mitigation: window only creates surface.
- Profiling code can accidentally become renderer policy. Mitigation: profiler stays data collection only.
