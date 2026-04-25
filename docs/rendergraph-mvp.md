# Render Graph MVP

## 目标

跑通一个能 present 的完整 render graph frame。MVP 成功标准是：窗口打开，并且通过 graph-declared pass 呈现确定性的 clear color 或 triangle。

## 非目标

- 不做 material system。
- 不做 scene graph。
- 不做 async compute。
- 首版不强求 resource aliasing。
- shader 编译和验证稳定前，不接入自动 reflection。

## 里程碑 1：窗口与 Vulkan Context

退出条件：

- GLFW window 能打开。
- Vulkan instance 在可用时使用 API version 1.4。
- Debug 构建中启用 validation layer。
- surface 创建成功。
- physical device selection 能报告 queue family 和 swapchain support。
- logical device 与 VMA allocator 创建成功。

## 里程碑 2：Swapchain 与 Frame Loop

退出条件：

- swapchain image 可以 acquire 和 present。
- frame fence 与 semaphore 能避免 CPU/GPU hazard。
- resize/out-of-date 路径不会崩溃。
- command buffer 能录制并提交到 graphics queue。

## 里程碑 3：第一个 Render Graph

退出条件：

- graph builder 能创建 pass node 和 resource handle。
- 当前 swapchain image 作为 graph image import。
- 至少一个 pass 写入 swapchain image。
- compiler 能输出有序 pass list 和必要 layout transition。
- execution 能通过 pass callback 录制命令。

## 里程碑 4：Triangle Pass

退出条件：

- 最小 vertex/fragment shader 能编译为 SPIR-V。
- shader build 路径包含 SPIR-V validation。
- 使用 dynamic rendering 创建 graphics pipeline。
- triangle pass 声明 color attachment write access。
- graph 能把 swapchain image transition 到 rendering 和 present 所需 layout。

## 里程碑 5：可审核基线

退出条件：

- 构建脚本和依赖 profile 已文档化。
- Debug 日志能输出 render graph pass order 和 resource transition。
- 普通启动、渲染一帧、关闭路径没有已知 validation error。
- 有基础 smoke test 命令。

## MVP Graph 示例

```text
Acquire swapchain image
  -> ImportBackbuffer
  -> ClearColorPass writes Backbuffer as color attachment
  -> TrianglePass writes Backbuffer as color attachment
  -> PresentTransitionPass transitions Backbuffer to present
  -> Queue present
```

## 最小 API 草图

```cpp
auto backbuffer = graph.importImage("Backbuffer", swapchainImage, desc);

graph.addPass("ClearColor")
    .writeColor(backbuffer, ClearColor{0.02f, 0.04f, 0.08f, 1.0f})
    .execute([](RenderGraphContext& ctx) {
        ctx.beginRendering();
        ctx.endRendering();
    });

graph.addPass("Triangle")
    .writeColor(backbuffer)
    .execute([](RenderGraphContext& ctx) {
        ctx.bindPipeline("triangle");
        ctx.draw(3, 1, 0, 0);
    });
```

真实实现中不建议在 hot path 用字符串查 pipeline；但早期 debug log 和 graph visualization 可以接受。
