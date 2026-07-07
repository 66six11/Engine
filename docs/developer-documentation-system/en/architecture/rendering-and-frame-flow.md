# Architecture: Rendering And Frame Flow

## Purpose

This document describes the current boundaries between RenderGraph, RHI, renderer-basic, the sample viewer, and editor host in the rendering path.

## Current Modules

| Module | Target | Current responsibility |
|---|---|---|
| RenderGraph | `asharia::rendergraph` | Resource declarations, pass declarations, schema validation, dependency sort, compile result, diagnostics snapshot |
| Vulkan RHI | `asharia::rhi_vulkan` | Vulkan instance/device/surface/swapchain, VMA allocation, pipeline/resource wrappers, frame loop |
| RenderGraph to Vulkan adapter | `asharia::rhi_vulkan_rendergraph` | Mapping RenderGraph image/buffer state to Vulkan layout/stage/access |
| Basic renderer schema layer | `asharia::renderer_basic` | Builtin pass type, params type, schema registry, draw item data |
| Basic Vulkan renderer | `asharia::renderer_basic_vulkan` | Shader target dependencies, pipeline/layout/resource recorders, RenderView diagnostics |
| Sample viewer | `asharia-sample-viewer` | Interactive viewer, runtime smoke, RenderGraph benchmark |
| C++ editor host | `asharia-editor`, `editor-native` | ImGui editor shell, viewport, frame debugger, native bridge |

## Normal Frame Flow

1. Host creates a window through `packages/window-glfw`.
2. Host creates `VulkanContext` with required instance extensions and a surface factory.
3. Host creates `VulkanFrameLoop` for swapchain images and per-frame command submission.
4. Renderer code builds a `RenderGraph`:
   - imports swapchain or offscreen images,
   - creates transient images/buffers,
   - adds passes,
   - sets pass types, params, resource slots, callbacks, and command lists.
5. Schemas are supplied separately through `RenderGraphSchemaRegistry` when the caller invokes `compile(schemaRegistry)`.
6. `RenderGraph::compile()` validates declarations, orders passes, calculates transitions, and culls allowed unused passes. `diagnosticsSnapshot(compiled)` and `formatDebugTables(compiled)` derive diagnostic views after compile.
7. `rhi_vulkan_rendergraph` maps graph states to Vulkan states.
8. `renderer_basic_vulkan` records Vulkan commands for builtin schemas and RenderView paths.
9. `VulkanFrameLoop::renderFrame()` submits and presents. For editor/studio interop, native bridge code can publish rendered external images instead of only presenting to the swapchain.

## RenderGraph Boundary

`packages/rendergraph/include/asharia/rendergraph/` exposes backend-agnostic names:

- image and buffer handles,
- image/buffer states,
- resource-access shader stage enum (`None`, `Fragment`, `Compute`), not a complete pipeline shader-stage list,
- resource slot schemas,
- pass schemas,
- command summaries,
- compile result and diagnostics.

It must not include `vulkan/vulkan.h` and must not know layouts, barriers, queues, descriptor sets, or command buffers.

## Vulkan RHI Boundary

`packages/rhi-vulkan/include/asharia/rhi_vulkan/` owns Vulkan handles and lifetime:

- `VulkanContext` owns instance, debug messenger, surface, physical device, logical device, graphics queue, and VMA allocator.
- `VulkanFrameLoop` owns swapchain, image views, command pool/buffer, semaphores, fence, timestamp query pool, and deferred deletion queue.
- Resource wrappers own Vulkan buffers, images, image views, samplers, shader modules, descriptor pools, pipeline layouts, and pipelines.

The adapter header root `packages/rhi-vulkan/include-rendergraph/` is the only place where this package exposes RenderGraph integration.

## Renderer Basic Split

`asharia::renderer_basic` is an interface target for backend-agnostic renderer declarations:

- pass type constants such as `builtin.raster-triangle`,
- params structs such as `BasicTransferClearParams`,
- schema registration functions,
- draw-item data and render graph helper types.

`asharia::renderer_basic_vulkan` is the Vulkan implementation:

- `asharia::renderer_basic` links `asharia::core`, `asharia::rendergraph`, and `asharia::shader_slang`;
- `asharia::renderer_basic_vulkan` publicly links `asharia::renderer_basic`, `asharia::rhi_vulkan`, and `asharia::rhi_vulkan_rendergraph`, and privately links `asharia::material_core`;
- compiles Slang shaders through `asharia_add_slang_shader()`,
- records backend commands and collects `BasicRenderViewDiagnostics`.

The builtin schema registry currently covers transfer clear, dynamic clear, transient present, triangle/depth/mesh3d/MRT/fullscreen/draw-list, RenderView, compute dispatch/readback, buffer fill/copy, and debug image copy passes.

## Editor And Studio Interop

`apps/editor` currently contains the C++ ImGui editor host and `editor-native` shared library. `apps/studio` is an Avalonia shell with C# models, services, views, native interop APIs, and tests.

State ownership:

- Native Vulkan resources remain in C++ native code.
- Studio C# models represent snapshots, requests, statuses, and handles.
- Native interop APIs must validate ABI headers and ownership handoff before using imported handles.

`future`: if Studio becomes the primary editor host, the native runtime boundary should stay explicit and should not move Vulkan ownership into C# UI code.

## Failure Paths

- Surface or device creation failure returns `Result<T>` with `ErrorDomain::Vulkan`.
- Swapchain out-of-date or suboptimal status returns `VulkanFrameStatus`, not an uncategorized boolean.
- RenderGraph compile failure returns an error before backend recording.
- Schema mismatch is a RenderGraph validation failure, not a Vulkan recording failure.
- External image/semaphore import failure belongs to native bridge/RHI error reporting.

## Validation

Build gates:

```powershell
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

Runtime smoke examples:

```powershell
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-rendergraph
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-dynamic-rendering
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-triangle
build\cmake\msvc-debug\apps\sample-viewer\asharia-sample-viewer.exe --smoke-offscreen-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
```

Checkpoints:

- RenderGraph compile errors are observable before Vulkan command recording.
- Swapchain resize can recreate without using `vkDeviceWaitIdle` in the frame loop. Current code may use `vkQueueWaitIdle` during recreation and keeps retired swapchain resources alive outside the deferred deletion epoch.
- RenderView diagnostics contain graph snapshot and execution events when requested.
