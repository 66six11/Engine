# 架构设计

## 目标

先做一个小而完整的 Vulkan renderer，用最少功能证明 render graph 从声明、编译、同步到
执行、present 的完整流程。第一个稳定 frame 比大而全的抽象更重要。

## 模块边界

- `platform`：GLFW window、输入轮询、OS 集成、surface 创建。
- `core`：日志、错误类型、文件系统辅助、断言、构建配置。
- `rhi`：Vulkan instance、physical device 选择、logical device、queue、swapchain、
  allocator、command pool、descriptor、pipeline、同步原语。
- `rendergraph`：graph builder、resource registry、pass declaration、graph compiler、
  barrier planner、transient resource 生命周期、执行器。
- `renderer`：frame orchestration、和场景无关的渲染 pass、present。
- `shader`：shader build product、SPIR-V 加载、validation hook、未来 reflection。
- `app`：可执行入口和 sample scene。

## 所有权模型

- `Context` 拥有 instance、debug messenger、surface、physical device 选择结果、logical
  device、queue 和 allocator。
- `Swapchain` 拥有 swapchain images；这些 image 作为 imported render graph resource，
  不作为普通 transient image。
- `FrameContext` 拥有每帧 command buffer、fence、semaphore、descriptor scratch 和 graph
  execution 临时状态。
- `RenderGraph` 拥有单帧 pass declaration。持久 GPU resource 由 renderer 或 resource
  manager 拥有，再 import 到 graph。
- `CompiledGraph` 拥有单帧解析后的执行计划和 barriers。

销毁顺序：

1. shutdown 时等待 GPU idle。
2. 销毁 frame resources。
3. 销毁 renderer 拥有的 resource 和 pipeline。
4. 销毁 swapchain。
5. 销毁 allocator-backed resources。
6. 销毁 allocator、device、surface、debug messenger、instance。

## Render Graph 数据模型

核心概念：

- `ResourceHandle`：逻辑 buffer/image 的 typed handle。
- `ResourceDesc`：image/buffer 的尺寸、格式、usage、生命周期、clear 行为。
- `PassNode`：名称、queue domain、声明的 read/write、execute callback。
- `AccessDesc`：pipeline stage、access mask、image layout、subresource range。
- `ImportedResource`：外部拥有的资源，例如 swapchain image。
- `TransientResource`：graph 拥有的单帧临时资源。

编译步骤：

1. 校验 pass 声明。
2. 解析 resource lifetime 和可选 alias。
3. 根据 read/write 依赖进行拓扑排序。
4. 在 producer/consumer access 之间插入 synchronization2 barriers。
5. 分配 transient resources。
6. 生成可执行 pass list。

## MVP Pass Flow

最小可用 graph：

1. import 当前 swapchain image。
2. 需要时创建 transient color image；MVP 也可以直接写 swapchain image。
3. 添加 `ClearColorPass`。
4. 添加 `TrianglePass`。
5. 添加 `PresentTransitionPass`。
6. 执行 graph 并 present。

建议 MVP 渲染路径：

- 使用 dynamic rendering。
- 使用单 graphics queue。
- 根据 swapchain image count 做 double/triple buffering。
- 先使用固定 pipeline。
- descriptor allocator 和 shader reflection 等三角形稳定后再做。

## 同步策略

- CPU/GPU frame pacing 使用每帧一个 fence。
- acquire/present 使用 binary semaphore，保持 swapchain 兼容性。
- GPU work submission 优先使用 `vkQueueSubmit2`。
- graph 内 image transition 使用 `vkCmdPipelineBarrier2`。
- timeline semaphore 放到 MVP 后，如果能简化多帧调度再引入。

## Swapchain 策略

- out-of-date 或 surface 不兼容时 recreate swapchain。
- suboptimal 初期作为 warning 路径；后续再决定是否立即 recreate。
- render graph resource 尽量不直接依赖 raw swapchain image index。
- swapchain extent 改变时重建 framebuffer-sized transient resources。

## 后续扩展点

- descriptor allocator 与 bindless/resource table。
- pipeline cache 与 pipeline library。
- async compute queue domain。
- RenderDoc capture label 与 GPU timestamp。
- shader hot reload。
- asset loading 与 scene graph。
- depth prepass、G-buffer、lighting、postprocess、UI pass。
