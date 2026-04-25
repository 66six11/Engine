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
    Renderer["packages/renderer-basic"]
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
    Renderer --> RhiVk
    Renderer --> RhiVkRG
    Renderer --> Shader
    App --> Core
    App --> Window
    App --> RG
    App --> RhiVk
    App --> RhiVkRG
    App --> Renderer
```

当前约束：

- `vke::rhi_vulkan` 是基础 Vulkan 后端，不公开依赖 RenderGraph。
- `vke::rhi_vulkan_rendergraph` 是 RenderGraph/Vulkan 适配层，负责把抽象 graph state 翻译为 Vulkan 类型。
- `renderer-basic` 组合 RenderGraph、Vulkan frame callback 和 Vulkan adapter，承载当前 clear frame orchestration。
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
    GLFW["GlfwInstance / GlfwWindow"]
    Ext["glfwRequiredVulkanInstanceExtensions"]
    Context["VulkanContext::create"]
    Device["选择 physical device<br/>创建 logical device / queue / VMA"]

    Start --> Args
    Args --> WindowSmoke
    Args --> VulkanSmoke
    Args --> FrameSmoke
    Args --> RGSmoke
    WindowSmoke --> GLFW
    VulkanSmoke --> GLFW
    VulkanSmoke --> Ext
    VulkanSmoke --> Context
    FrameSmoke --> GLFW
    FrameSmoke --> Ext
    FrameSmoke --> Context
    Context --> Device
```

状态：

- `--smoke-window` 已接入窗口创建。
- `--smoke-vulkan` 已接入 Vulkan context/device 创建。
- `--smoke-frame` 已接入 swapchain acquire、RenderGraph-driven clear、present。
- `--smoke-rendergraph` 是 RenderGraph CPU 编译和 Vulkan adapter 字段验证入口。

## 当前 Frame Loop 流程

```mermaid
flowchart TD
    Create["VulkanFrameLoop::create"]
    Swapchain["createSwapchain"]
    Images["getSwapchainImages"]
    Cmd["create command pool / command buffer"]
    Sync["create semaphore / fence"]
    Render["renderFrame"]
    Wait["wait in-flight fence"]
    Acquire["vkAcquireNextImageKHR"]
    Record["renderFrame(callback)"]
    GraphClear["renderer-basic<br/>recordBasicClearFrame"]
    Submit["vkQueueSubmit2"]
    Present["vkQueuePresentKHR"]
    Recreate["recreateSwapchain"]

    Create --> Swapchain --> Images --> Cmd --> Sync
    Render --> Wait --> Acquire
    Acquire -->|success/suboptimal| Record --> GraphClear --> Submit --> Present
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

状态：

- 已接入真实 Vulkan 命令录制。
- `--smoke-frame` 的 clear/present barriers 已由 RenderGraph compile result 经 Vulkan adapter 生成。
- 默认 `VulkanFrameLoop::renderFrame()` 仍保留内置 clear 路径，作为基础 RHI smoke fallback。
- `recordBasicClearFrame` 已下沉到 `renderer-basic`，sample-viewer 只传入 renderer-basic 提供的 frame callback。

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
    Execute["execute()"]
    Callbacks["按编译后 pass 顺序调用 callback"]

    Import --> AddPass --> Writes --> Callback
    Callback --> Compile --> Track --> PassPlan --> Final
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
    VkTransition["VulkanRenderGraphImageTransition<br/>oldLayout/newLayout<br/>stage/access"]
    Barrier["VkImageMemoryBarrier2"]
    CmdBarrier["vkCmdPipelineBarrier2"]

    RGTransition -->|"vulkanImageTransition"| VkTransition
    VkTransition -->|"vulkanImageBarrier"| Barrier
    Barrier --> CmdBarrier
```

状态：

- `vulkanImageTransition` 已实现。
- `vulkanImageBarrier` 已实现。
- `--smoke-rendergraph` 已验证 `TransferDst -> Present` 的 layout、stage、access 与 `VkImageMemoryBarrier2` 字段。
- `--smoke-frame` 已消费 RenderGraph 编译结果来录制 clear frame barriers。

## 下一步接入计划

```mermaid
flowchart TD
    Now["当前:<br/>renderer-basic 接管 clear frame orchestration"]
    Step1["下一步:<br/>抽出 compiled graph execution<br/>避免 clear 路径重复 compile"]
    Step2["再下一步:<br/>Triangle pass + dynamic rendering"]
    Step3["之后:<br/>shader-slang 接入 triangle shader"]
    Step4["之后:<br/>renderer-basic 接管更完整 frame orchestration"]

    Now --> Step1 --> Step2 --> Step3 --> Step4
```

建议推进顺序：

1. 保持 `VulkanFrameLoop` 基础 target 不依赖 RenderGraph。
2. 保持 `renderer-basic` 作为 RenderGraph 与 Vulkan frame callback 的组合层。
3. 为 RenderGraph 增加 compiled graph execution 入口，避免当前 clear frame 先 `compile()` 再 `execute()` 的重复编译。
4. 再接入 dynamic rendering triangle pass。
