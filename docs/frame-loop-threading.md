# Frame Loop 与线程设计

研究日期：2026-05-08

本文定义 VkEngine 从当前单线程 frame loop 演进到 worker pool、RenderThread 和多线程 command recording
时必须保持的边界。当前仓库仍以单线程 smoke host 为准；本文中的多线程部分是分阶段设计，不代表已经实现。

## 设计依据

- Godot 支持多线程，但 SceneTree 不是线程安全的；Server API 更适合从线程访问，直接 GPU 操作可能因为与 RenderingServer 同步而产生 stall。
- Unreal 把渲染流水线拆成 GameThread、RenderThread 和 RHIThread，通常允许渲染端落后或领先一个受控帧；GameThread 和 RenderThread 之间通过渲染命令和代理数据通信。
- Unity Job System 把并行边界放在数据任务上，使用 worker threads、work stealing 和 safety system 避免 race condition。
- Bevy ECS 根据 system 的数据访问关系自动并行；需要顺序时显式 chain。
- Vulkan host threading 允许多线程录制 command buffer，但同一个 command pool、descriptor pool 的相关操作需要外部同步；Khronos sample 建议 per-frame/per-thread pools。

## 当前单线程基线

当前真实路径仍保持：

```text
main thread
  poll window events
  acquire swapchain image
  begin command buffer
  renderer callback
    RecordGraph
    CompileGraph
    PrepareBackend
    RecordCommands
  end command buffer
  submit
  present
  retire completed frame resources
```

这个阶段的目标是稳定 RenderGraph、resource state、transient allocation、descriptor/pipeline cache 和
deferred destruction。除 shader build tool 和未来 asset import 外，不为了“看起来多线程”拆分 frame loop。

## 线程所有权规则

| 数据/对象 | Owner | Worker 可做 | Worker 不可做 |
| --- | --- | --- | --- |
| Window/OS event | main thread | 读取已发布的 input snapshot | 调用 GLFW window mutation 或 present |
| Scene/editor object | main thread | 读取 immutable snapshot 或构建结果数据 | 直接改 active scene tree、selection、inspector object |
| Script VM | main thread 或明确的 script owner | 只运行标记为 worker-safe 的纯数据任务 | 在 render command recording 中回调脚本 |
| RenderGraph builder | render owner thread | 后续可并行生成局部 draw list 数据 | 多线程同时修改同一个 graph builder |
| CompiledGraph | immutable frame data | 并行读取 | 修改 pass/resource topology |
| Vulkan device/context | RHI owner | 通过 RHI work queue 请求创建/销毁 | 随机线程直接创建/销毁 backend 对象 |
| Command pool | per-frame/per-thread owner | 录制自己拥有的 command buffer | 多线程共享同一个 command pool |
| Descriptor pool/cache | per-frame/per-thread owner | 分配/更新自己拥有的 descriptor set | 多线程从同一个 descriptor pool 分配/释放 |
| Queue submit/present | RHI owner | 提交 recorded command buffer 给 owner | 多 worker 直接 `vkQueueSubmit` |

## 分阶段路线

### 阶段 0：单线程稳定

保持当前路径。补齐：

- deferred destruction queue
- frame retirement fence/timeline accounting
- descriptor allocator
- transient resource pool
- pipeline layout / pipeline cache
- RenderGraph compile counters

验收：所有 `--smoke-*` 继续通过，`--bench-rendergraph` 可以观测 record/compile 开销。

### 阶段 1：CPU worker pool

新增 worker pool，但 worker 只处理 plain data：

```text
main thread
  build tasks:
    asset decode
    mesh processing
    CPU culling
    animation sampling
    shader compile request
    draw-list build
  collect results
  build render packet
```

要求：

- task 输入是 immutable snapshot、span、POD buffer 或 handle。
- task 输出是 result object 或 command/message，不直接改 owner 对象。
- asset/shader 编译错误带 source path、import setting、tool version 和 command line。

### 阶段 2：RenderThread

主线程负责 gameplay/editor/script/input，RenderThread 消费上一帧或当前帧的 `RenderFramePacket`：

```text
main thread:       update N+1 -> publish RenderFramePacket N+1
render thread:     consume packet N -> RecordGraph -> CompileGraph -> PrepareBackend -> RecordCommands -> Submit
RHI/GPU:           execute submitted work
```

第一版允许 one-frame-lag，但必须可配置关闭，方便调试 latency 和 data race。RenderFramePacket 必须是
不可变快照，包含 camera、view、draw items、material/resource handles、debug flags 和 frame constants。

### 阶段 3：多线程 command recording

RenderThread 编译 graph 后，只把适合并行的 draw ranges 或 pass work items 分给 worker：

```text
FrameResource[frameIndex]
  ThreadResource[workerIndex]
    VkCommandPool
    DescriptorPoolCache
    DescriptorSetCache
    UploadScratch
    CommandBufferList
```

要求：

- 每个 worker 只访问自己的 per-thread pools。
- pass callback 不捕获外部 `VkImage`、`VkBuffer`；所有 backend resource 由 binding table 提供。
- secondary command buffer 数量不得超过 CPU parallelism 和足够 draw count 的合理范围。
- RenderThread 收集 command buffers，按 compiled graph 顺序 execute/submit。

### 阶段 4：RHI thread / transfer queue

只有当 profiling 证明 queue submit、upload 或 pipeline creation 成为瓶颈时才引入。RHI thread 是
Vulkan backend owner，负责：

- queue submit / present
- deferred destruction retirement
- staging upload queue
- pipeline/background PSO creation
- optional transfer queue ownership

所有请求通过 RHI work queue 进入；worker 仍不直接调用 queue submit。

## Frame Loop 顺序

未来完整 runtime 可采用：

```text
PollEvents
InputUpdate
BeginFrame
FixedUpdate zero or more times
GameUpdate
ScriptUpdate
AnimationUpdate
PhysicsStep or PhysicsSync
SceneToRenderSnapshot
AssetHotReloadApply safe point
RenderRecordOrSubmit
Present
FrameRetire
DeferredCleanup
```

编辑器模式可在 GameUpdate 暂停时继续运行 EditorUpdate、ViewportRender 和 AssetImportPoll。暂停不等于停止
render/resource retirement。

## 同步与失败路径

- 主线程和 RenderThread 只通过 bounded queue、double/triple buffered packet 或 explicit fence 交互。
- `RenderFramePacket` 发布后不可变；热重载通过下一帧 packet 生效。
- worker task 失败返回 structured error；owner 线程决定 fallback resource 或终止 smoke。
- GPU resource destroy 不在提交帧立即执行，进入 deferred destruction 并等待 frame fence。
- 任何临时 `vkDeviceWaitIdle` 必须写注释说明是 shutdown、debug probe 还是 MVP 简化路径。
- shutdown 顺序：停止接收新任务，drain worker，停止 render thread，等待 GPU idle，销毁 frame resources，销毁 long-lived resources，销毁 device/context。

## 文档与代码审查门禁

- 新增线程或 queue 时，必须更新本文和 `flow-architecture.md` 的真实运行图。
- 新增 worker task 时，必须说明输入所有权、输出所有权、取消/失败语义和是否可并行。
- 新增 Vulkan 多线程录制时，必须证明 command pool、descriptor pool/cache、upload scratch 是 per-frame/per-thread。
- 新增 RenderThread 前，必须先有 RenderFramePacket 文档和 smoke fallback。
- 性能优化必须有 profiler counter；不能凭直觉加入常驻线程。

## 参考资料

- Godot thread-safe APIs: https://docs.godotengine.org/en/stable/tutorials/performance/thread_safe_apis.html
- Unreal parallel rendering overview: https://dev.epicgames.com/documentation/en-us/unreal-engine/parallel-rendering-overview-for-unreal-engine
- Unreal low latency frame syncing: https://dev.epicgames.com/documentation/en-us/unreal-engine/low-latency-frame-syncing
- Unity Job System overview: https://docs.unity3d.com/Manual/JobSystemOverview.html
- Bevy ECS quick start: https://bevy.org/learn/quick-start/getting-started/ecs/
- Vulkan Guide threading: https://docs.vulkan.org/guide/latest/threading.html
- Khronos command buffer usage and multi-threaded recording sample: https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html
