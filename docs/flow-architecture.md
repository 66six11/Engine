# 流程架构图

本文档记录当前代码真实流程。每次实现或重构后都需要同步更新，用来帮助审查架构走向、包边界和下一步开发顺序。

## 维护规则

- 代码改变了运行流程、包依赖、资源状态、同步路径或 smoke 命令时，必须更新本文档。
- 图中的“已接入”表示当前运行路径真实使用；“smoke 验证”表示已有测试入口但尚未接入主 frame loop；“下一步”表示目标方向。
- RenderGraph 图层必须保持后端无关；Vulkan layout、stage、access、barrier 翻译只允许出现在 `packages/rhi-vulkan`。

## 当前包依赖

```mermaid
flowchart TD
    App["apps/sample-viewer"]
    Core["engine/core"]
    Platform["engine/platform"]
    Window["packages/window-glfw"]
    RG["packages/rendergraph"]
    RhiVk["packages/rhi-vulkan<br/>vke::rhi_vulkan"]
    RhiVkRG["packages/rhi-vulkan<br/>vke::rhi_vulkan_rendergraph"]
    Renderer["packages/renderer-basic<br/>vke::renderer_basic"]
    RendererVk["packages/renderer-basic<br/>vke::renderer_basic_vulkan"]
    Shader["packages/shader-slang"]

    Platform --> Core
    Window --> Core
    Window --> Platform
    RG --> Core
    RhiVk --> Core
    RhiVkRG --> RhiVk
    RhiVkRG --> RG
    Renderer --> Core
    Renderer --> RG
    Renderer --> Shader
    RendererVk --> Renderer
    RendererVk --> RhiVk
    RendererVk --> RhiVkRG
    App --> Core
    App --> Window
    App --> RG
    App --> RhiVk
    App --> RhiVkRG
    App --> Renderer
    App --> RendererVk
```

当前约束：

- `vke::rhi_vulkan` 是基础 Vulkan 后端，不公开依赖 RenderGraph。
- `vke::rhi_vulkan_rendergraph` 是 RenderGraph/Vulkan 适配层，负责把抽象 graph state 翻译为 Vulkan 类型。
- `renderer-basic` 只描述后端无关的 basic renderer graph 片段。
- `renderer-basic-vulkan` 组合 RenderGraph、Vulkan frame callback 和 Vulkan adapter，承载当前 clear frame orchestration。
- `sample-viewer` 负责窗口、context 和 smoke 命令入口；`--smoke-frame` 不再持有 clear pass 录制细节。

## 启动与 Context 流程

```mermaid
flowchart TD
    Start["main()"]
    Args["解析命令行参数"]
    WindowSmoke["--smoke-window"]
    VulkanSmoke["--smoke-vulkan"]
    FrameSmoke["--smoke-frame"]
    RGSmoke["--smoke-rendergraph"]
    DynamicSmoke["--smoke-dynamic-rendering"]
    TriangleSmoke["--smoke-triangle"]
    GLFW["GlfwInstance / GlfwWindow"]
    Ext["glfwRequiredVulkanInstanceExtensions"]
    Context["VulkanContext::create"]
    Device["选择 physical device<br/>创建 logical device / queue / VMA"]
    ShaderBuild["shader-slang package<br/>slangc + spirv-val<br/>triangle SPIR-V"]
    RendererObject["BasicTriangleRenderer<br/>shader modules / pipeline layout / vertex buffer / pipeline<br/>BasicDrawItem"]

    Start --> Args
    Args --> WindowSmoke
    Args --> VulkanSmoke
    Args --> FrameSmoke
    Args --> RGSmoke
    Args --> DynamicSmoke
    Args --> TriangleSmoke
    WindowSmoke --> GLFW
    VulkanSmoke --> GLFW
    VulkanSmoke --> Ext
    VulkanSmoke --> Context
    FrameSmoke --> GLFW
    FrameSmoke --> Ext
    FrameSmoke --> Context
    DynamicSmoke --> GLFW
    DynamicSmoke --> Ext
    DynamicSmoke --> Context
    TriangleSmoke --> GLFW
    TriangleSmoke --> Ext
    TriangleSmoke --> Context
    TriangleSmoke --> ShaderBuild
    TriangleSmoke --> RendererObject
    Context --> Device
```

状态：

- `--smoke-window` 已接入窗口创建。
- `--smoke-vulkan` 已接入 Vulkan context/device 创建。
- `--smoke-frame` 已接入 swapchain acquire、RenderGraph-driven clear、present。
- `--smoke-dynamic-rendering` 已接入 swapchain acquire、RenderGraph color write、dynamic rendering clear、present。
- `--smoke-triangle` 已接入 `BasicTriangleRenderer`、dynamic-rendering graphics pipeline、RenderGraph color write、draw、present。
- `--smoke-rendergraph` 是 RenderGraph CPU 编译和 Vulkan adapter 字段验证入口。

## 当前 Frame Loop 流程

```mermaid
flowchart TD
    Create["VulkanFrameLoop::create"]
    Swapchain["createSwapchain"]
    Images["getSwapchainImages"]
    Views["create swapchain image views"]
    Cmd["create command pool / command buffer"]
    Sync["create semaphore / fence"]
    Render["renderFrame"]
    Wait["wait in-flight fence"]
    Acquire["vkAcquireNextImageKHR"]
    Record["renderFrame(callback)"]
    GraphClear["renderer-basic-vulkan<br/>recordBasicClearFrame"]
    Triangle["renderer-basic-vulkan<br/>BasicTriangleRenderer::recordFrame"]
    WaitStage["record result<br/>acquire wait stage"]
    Submit["vkQueueSubmit2"]
    Present["vkQueuePresentKHR"]
    Recreate["recreateSwapchain"]

    Create --> Swapchain --> Images --> Views --> Cmd --> Sync
    Render --> Wait --> Acquire
    Acquire -->|success/suboptimal| Record --> GraphClear --> WaitStage --> Submit --> Present
    Record --> Triangle --> WaitStage
    Acquire -->|out of date| Recreate
    Present -->|out of date/suboptimal| Recreate
```

`--smoke-frame` 当前真实录制流程：

```mermaid
flowchart TD
    Begin["vkBeginCommandBuffer"]
    BuildGraph["Build RenderGraph<br/>Backbuffer + ClearColor transfer write"]
    Compile["compile()"]
    ToTransfer["adapter barrier:<br/>Undefined -> TransferDst"]
    Clear["vkCmdClearColorImage"]
    ToPresent["adapter barrier:<br/>TransferDst -> Present"]
    End["vkEndCommandBuffer"]

    Begin --> BuildGraph --> Compile --> ToTransfer --> Clear --> ToPresent --> End
```

`--smoke-dynamic-rendering` 当前真实录制流程：

```mermaid
flowchart TD
    Begin["vkBeginCommandBuffer"]
    BuildGraph["Build RenderGraph<br/>Backbuffer + DynamicClearColor color write"]
    Compile["compile()"]
    ToColor["adapter barrier:<br/>Undefined -> ColorAttachment"]
    BeginRendering["vkCmdBeginRendering<br/>loadOp clear"]
    EndRendering["vkCmdEndRendering"]
    ToPresent["adapter barrier:<br/>ColorAttachment -> Present"]
    End["vkEndCommandBuffer"]

    Begin --> BuildGraph --> Compile --> ToColor --> BeginRendering --> EndRendering --> ToPresent --> End
```

`--smoke-triangle` 当前真实录制流程：

```mermaid
flowchart TD
    ShaderBuild["shader-slang package<br/>slangc 编译 Slang<br/>spirv-val 验证 SPIR-V"]
    RendererObject["BasicTriangleRenderer"]
    PipelineObjects["VulkanShaderModule<br/>VulkanPipelineLayout<br/>VulkanBuffer<br/>VulkanGraphicsPipeline"]
    Begin["vkBeginCommandBuffer"]
    BuildGraph["Build RenderGraph<br/>Backbuffer + ClearColor transfer write<br/>+ Triangle color write"]
    Compile["compile()"]
    ToTransfer["adapter barrier:<br/>Undefined -> TransferDst"]
    Clear["vkCmdClearColorImage"]
    ToColor["adapter barrier:<br/>TransferDst -> ColorAttachment"]
    BeginRendering["vkCmdBeginRendering<br/>loadOp load"]
    Draw["bind pipeline<br/>bind vertex buffer<br/>set viewport/scissor<br/>vkCmdDraw(3)"]
    EndRendering["vkCmdEndRendering"]
    ToPresent["adapter barrier:<br/>ColorAttachment -> Present"]
    End["vkEndCommandBuffer"]

    ShaderBuild --> RendererObject --> PipelineObjects
    Begin --> BuildGraph --> Compile --> ToTransfer --> Clear --> ToColor --> BeginRendering --> Draw --> EndRendering --> ToPresent --> End
```

状态：

- 已接入真实 Vulkan 命令录制。
- `--smoke-frame` 的 clear/present barriers 已由 RenderGraph compile result 经 Vulkan adapter 生成。
- `--smoke-dynamic-rendering` 已验证 swapchain image view、dynamic rendering attachment clear 和 `ColorAttachment -> Present` transition。
- `--smoke-triangle` 已验证 `shader-slang` 构建出的 Slang SPIR-V、`BasicTriangleRenderer` 管理的 shader module、pipeline layout、host-upload vertex buffer、dynamic rendering graphics pipeline、`BasicDrawItem` draw 参数、ClearColor + Triangle 两个 graph pass、viewport/scissor dynamic state 和 triangle draw。
- 无参数 sample viewer 已接入交互式 triangle 循环，并已手动验证 resize/minimize 后仍可恢复持续渲染。
- RenderGraph transition 录制通过 `RenderGraphImageHandle -> VkImage` binding 查找真实 Vulkan image；当前 smoke 只绑定 Backbuffer，后续 depth/transient image 必须显式加入 binding 表。
- 默认 `VulkanFrameLoop::renderFrame()` 仍保留内置 clear 路径，作为基础 RHI smoke fallback。
- frame callback 会返回 `VulkanFrameRecordResult.waitStageMask`，用于匹配 acquire semaphore 的等待阶段。
- `recordBasicClearFrame` 和 triangle shader/pipeline 装配已下沉到 `renderer-basic-vulkan`，sample-viewer 只传入后端 recording callback。

## RenderGraph 编译与执行流程

```mermaid
flowchart TD
    Import["importImage(RenderGraphImageDesc)"]
    AddPass["addPass(name)"]
    Writes["writeColor / writeTransfer"]
    Callback["execute(callback)"]
    Compile["compile()"]
    Track["按 image 当前 state 追踪转换"]
    PassPlan["生成 RenderGraphCompiledPass"]
    Final["生成 finalTransitions"]
    DebugTables["formatDebugTables(compiled)"]
    Execute["execute(compiled)"]
    Callbacks["按编译后 pass 顺序调用 callback"]

    Import --> AddPass --> Writes --> Callback
    Callback --> Compile --> Track --> PassPlan --> Final
    Final --> DebugTables
    Compile --> Execute --> Callbacks
```

当前抽象状态：

- `Undefined`
- `ColorAttachment`
- `TransferDst`
- `Present`

当前 write 声明：

- `writeColor(image)` 会要求 image 进入 `ColorAttachment`。
- `writeTransfer(image)` 会要求 image 进入 `TransferDst`。

## RenderGraph 到 Vulkan 的翻译流程

```mermaid
flowchart TD
    RGTransition["RenderGraphImageTransition<br/>oldState/newState"]
    Binding["RenderGraphImageHandle -> VkImage binding"]
    VkTransition["VulkanRenderGraphImageTransition<br/>oldLayout/newLayout<br/>stage/access"]
    Barrier["VkImageMemoryBarrier2"]
    CmdBarrier["vkCmdPipelineBarrier2"]

    RGTransition -->|"vulkanImageTransition"| VkTransition
    RGTransition -->|"image handle"| Binding
    VkTransition -->|"vulkanImageBarrier + bound VkImage"| Barrier
    Binding --> Barrier
    Barrier --> CmdBarrier
```

状态：

- `vulkanImageTransition` 已实现。
- `vulkanImageBarrier` 已实现。
- `recordRenderGraphTransitions` 已要求调用方提供 `VulkanRenderGraphImageBinding` 表，不再隐式假设所有 transition 都作用在当前 swapchain image。
- `--smoke-rendergraph` 已验证 `TransferDst -> Present` 的 layout、stage、access 与 `VkImageMemoryBarrier2` 字段。
- `--smoke-frame` 已消费 RenderGraph 编译结果来录制 clear frame barriers。
- `--smoke-rendergraph` 已输出 resources、passes、transitions 的 Markdown 调试表格。

## 下一步接入计划

```mermaid
flowchart TD
    Now["当前:<br/>交互式 triangle viewer"]
    Step1["已接入:<br/>Vulkan pipeline objects"]
    Step2["已接入:<br/>Triangle pass"]
    Step3["已接入:<br/>shader-slang triangle shader"]
    Step4["已接入:<br/>renderer-basic-vulkan 接管 triangle 装配"]
    Step5["已接入:<br/>renderer-basic 接管 ClearColor + Triangle graph"]
    Step6["已接入:<br/>BasicDrawItem draw 参数"]
    Step7["已接入:<br/>host-upload vertex buffer"]
    Step8["之后:<br/>mesh asset 路线"]

    Now --> Step1 --> Step2 --> Step3 --> Step4 --> Step5 --> Step6 --> Step7 --> Step8
```

建议推进顺序：

1. 保持 `VulkanFrameLoop` 基础 target 不依赖 RenderGraph。
2. 保持 `renderer-basic` 后端无关，Vulkan 命令录制放在 `renderer-basic-vulkan`。
3. 保持 RenderGraph 调试表格只输出抽象 RG 信息；Vulkan layout/stage/access 调试表应放在 Vulkan adapter 层。
4. 当前只接入固定顶点数据与 host-upload vertex buffer，后续再扩展 mesh asset，不在当前 MVP 阶段提前扩大 mesh 架构。
