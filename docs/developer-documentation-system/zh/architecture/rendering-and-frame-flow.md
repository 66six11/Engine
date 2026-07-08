# 架构：渲染与 Frame Flow

## 目的

本文描述当前 rendering path 中 RenderGraph、RHI、renderer-basic、sample viewer 和 editor host 的边界。

## 当前模块

| 模块 | Target | 当前职责 |
|---|---|---|
| RenderGraph | `asharia::rendergraph` | 资源声明、pass 声明、schema validation、dependency sort、compile result、diagnostics snapshot |
| Vulkan RHI | `asharia::rhi_vulkan` | Vulkan instance/device/surface/swapchain、VMA allocation、pipeline/resource wrappers、frame loop |
| RenderGraph to Vulkan adapter | `asharia::rhi_vulkan_rendergraph` | RenderGraph image/buffer state 到 Vulkan layout/stage/access 的映射 |
| Basic renderer schema layer | `asharia::renderer_basic` | builtin pass type、params type、schema registry、draw item data |
| Basic Vulkan renderer | `asharia::renderer_basic_vulkan` | shader target dependency、pipeline/layout/resource recorder、RenderView diagnostics |
| Sample viewer | `asharia-sample-viewer` | interactive viewer、runtime smoke、RenderGraph benchmark |
| C++ editor host | `asharia-editor`、`editor-native` | ImGui editor shell、viewport、frame debugger、native bridge |

## 正常 Frame Flow

1. Host 通过 `packages/window-glfw` 创建 window。
2. Host 使用 required instance extensions 和 surface factory 创建 `VulkanContext`。
3. Host 创建 `VulkanFrameLoop`，管理 swapchain images 和 per-frame command submission。
4. Renderer code 构建 `RenderGraph`：import swapchain/offscreen images、创建 transient images/buffers、添加 passes，并设置 pass type、params、resource slots、callbacks 和 command lists。
5. Schema 不附着在 graph declaration 上；调用方在 `compile(schemaRegistry)` 时通过 `RenderGraphSchemaRegistry` 提供。
6. `RenderGraph::compile()` 验证 declarations、排序 passes、计算 transitions、裁剪允许 culling 的 unused passes。`diagnosticsSnapshot(compiled)` 和 `formatDebugTables(compiled)` 在 compile 后派生诊断视图。
7. `rhi_vulkan_rendergraph` 把 graph state 映射为 Vulkan state。
8. `renderer_basic_vulkan` 为 builtin schemas 和 RenderView paths 录制 Vulkan commands。
9. `VulkanFrameLoop::renderFrame()` submit 和 present。editor/studio interop 可以通过 native bridge 发布 rendered external images。

## RenderGraph 边界

`packages/rendergraph/include/asharia/rendergraph/` 暴露 backend-agnostic 类型：

- image/buffer handles，
- image/buffer states，
- resource-access shader stage enum：`None`、`Fragment`、`Compute`，不是完整 pipeline shader-stage 列表，
- resource slot schemas，
- pass schemas，
- command summaries，
- compile result 和 diagnostics 数据。

它不能包含 `vulkan/vulkan.h`，也不能知道 layouts、barriers、queues、descriptor sets 或 command buffers。

## Vulkan RHI 边界

`packages/rhi-vulkan/include/asharia/rhi_vulkan/` 拥有 Vulkan handles 和 lifetime：

- `VulkanContext` 拥有 instance、debug messenger、surface、physical device、logical device、graphics queue、VMA allocator。
- `VulkanFrameLoop` 拥有 swapchain、image views、command pool/buffer、semaphores、fence、timestamp query pool、deferred deletion queue。
- resource wrappers 拥有 Vulkan buffers、images、image views、samplers、shader modules、descriptor pools、pipeline layouts、pipelines。

`packages/rhi-vulkan/include-rendergraph/` 是该 package 暴露 RenderGraph integration 的唯一 header root；它负责 formats/extents、image layouts、shader stages、access masks 以及 `VkImageMemoryBarrier2` / `VkBufferMemoryBarrier2` 生成。

## Renderer Basic 分层

`asharia::renderer_basic` 是 backend-agnostic renderer declaration 层，当前链接 `asharia::core`、`asharia::rendergraph`、`asharia::shader_slang`。它包含 pass type 常量、params structs、schema registration functions、draw-item data。

`asharia::renderer_basic_vulkan` 是 Vulkan 实现层，public 链接 `asharia::renderer_basic`、`asharia::rhi_vulkan`、`asharia::rhi_vulkan_rendergraph`，private 链接 `asharia::material_core`。它通过 `asharia_add_slang_shader()` 编译 shader，并录制 backend commands。

当前 builtin schema registry 注册 transfer clear、dynamic clear、transient present、triangle/depth/mesh3d/MRT/fullscreen/draw-list、RenderView、compute dispatch/readback、buffer fill/copy 和 debug image copy passes。

## Editor And Studio Interop

`apps/editor` 是 C++ ImGui editor host 和 `editor-native` shared library。`apps/studio` 是 Avalonia shell，包含 C# models、services、views、native interop APIs 和 tests。

状态所有权：

- Native Vulkan resources 留在 C++ native code。
- Studio C# models 表示 snapshots、requests、statuses、handles。
- Native interop APIs 必须验证 ABI headers 和 ownership handoff。

`future`: 如果 Studio 成为主要 editor host，native runtime boundary 仍应保持显式，不能把 Vulkan ownership 移入 C# UI code。

## Failure Paths

- Surface/device creation failure 返回 `Result<T>` 和 `ErrorDomain::Vulkan`。
- Swapchain out-of-date/suboptimal status 返回 `VulkanFrameStatus`。
- RenderGraph compile failure 在 backend recording 前返回 error。
- Schema mismatch 是 RenderGraph validation failure，不是 Vulkan recording failure。
- External image/semaphore import failure 属于 native bridge/RHI error reporting。

## 验证方式

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

检查点：

- RenderGraph compile errors 在 Vulkan command recording 前可见。
- Swapchain resize 不在 frame loop 中使用 `vkDeviceWaitIdle`；当前 recreate path 可使用 `vkQueueWaitIdle`，并把 retired swapchain resources 留在 deferred deletion epoch 之外。
- RenderView diagnostics 在请求时包含 graph snapshot 和 execution events。
