# Frame Loop 与线程设计

研究日期：2026-05-08；最近更新：2026-08-05

本文定义 Asharia Engine 从当前单线程 frame loop 演进到 worker pool、RenderThread 和多线程 command recording
时必须保持的边界。`sample-viewer` 与 C++ editor 的 window/swapchain frame loop 仍以单线程 smoke host 为准；
Studio shared viewport 已落地一条范围受限的 native RenderThread owner 路径。除该明确列出的 Slice 外，本文中的
多线程阶段仍是设计，不代表完整 runtime frame loop、RHI Thread 或多线程 command recording 已经实现。

## 设计依据

- Godot 支持多线程，但 SceneTree 不是线程安全的；Server API 更适合从线程访问，直接 GPU 操作可能因为与 RenderingServer 同步而产生 stall。
- Unreal 把渲染流水线拆成 GameThread、RenderThread 和 RHIThread；GameThread 与 RenderThread 之间通过拥有明确
  生命周期的渲染命令和代理数据通信，不能让 RenderThread 解引用随时可变的 gameplay/editor object。
- Unity Job System 把并行边界放在数据任务上，使用 worker threads、work stealing 和 safety system 避免 race condition。
- Bevy ECS 根据 system 的数据访问关系自动并行；需要顺序时显式 chain。
- Vulkan host threading 允许多线程录制 command buffer，但同一个 command pool、descriptor pool 的相关操作需要外部同步；Khronos sample 建议 per-frame/per-thread pools。

## 当前 window/swapchain 单线程基线

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

这条基线描述 `VulkanFrameLoop` 驱动的 window/swapchain host，不描述 Studio 的 offscreen external-image Viewport。
Studio 的当前差异只在下文“Studio shared viewport owner Slice”中定义。

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

#### 已落地的 Studio shared viewport owner Slice

Studio 没有等待完整 runtime frame loop 迁移，而是在 `editor_native.dll` 的
`EditorSharedViewportRuntime` 内建立一条进程级 shared viewport RenderThread。它服务 Scene、Game 与 Preview
request，并独占该 runtime 的 Vulkan context、render producer、RenderGraph/command recording、graphics queue
submit、present-slot state、GPU epoch polling、retirement 与 shutdown。Avalonia panel、managed
`ViewportPresentationLifetime` 和单个 `ViewportSession` 都不是 renderer owner，也不得各建线程。

production 已硬切到 V7 stream ABI。caller 把借用字段复制为 owning `RenderFramePacket`，`submit_latest_v7` 原位覆盖
每 stream 唯一 pending request 并立即返回；RenderThread 在 ready cell 为空且 slot 可推进时消费。`try_take_ready_v7`
同样不等待 GPU，ready frame 通过 producer-finished semaphore 把完成依赖交给 compositor。旧 V1–V6 frame exports
不再导出，managed 没有 fallback；所有 Vulkan/producer 操作和析构仍只发生在 native RenderThread。

production `StudioCompositionSession` 会在后台 managed worker 上先调用 compatibility query，提前把 device/context
创建工作投递给同一 RenderThread。该 warm-up 不创建第二个 owner、不阻塞 shell ready，并在 shutdown 调用 runtime
teardown 前完成；frame request 仍使用真实 Avalonia compositor identity 做最终 compatibility 复验。

跨线程 `RenderFramePacket` 是发布后不可变的 owning value，至少包含：

```text
panel/session/target identity
target revision / request sequence
render kind
logical extent / allocation extent
camera snapshot
bounded debug-proxy array
presence flags / external-image handle family
```

`target kind` 不作为独立 packet 字段扩张：当前 C ABI 只接受 `DocumentScene`，在复制前验证并归一化；packet 保存
session/target identity 与 render kind。

packet 可以拥有 `std::string`、`std::vector` 和纯值字段；不得保留 C ABI caller 的 `std::span`、
`std::string_view`、managed pointer、Avalonia object、`SceneDocument`/World pointer 或可变 editor object。
RenderThread 内部可以从 owning packet 临时构造借用 view，但该 view 不得越过当前 dispatch。

V7 packet 带 view-local FOV axis、immutable scene `sourceRevision` 和 backend-neutral `scene-rendering` 的抽取结果；它不携带 scene/runtime
指针、asset importer state、resource registry reference 或 GPU key。Document ABI v2 与 V7 是硬切边界：旧 v1
document 及 v1--v6 viewport packet 不被转换或降级消费。mesh binding missing/wrong-kind/stale 是逐 item 的 fail-closed
no-draw diagnostics；packet 本身 malformed 则拒绝整帧，scheduler 不录制部分内容。

control/release mailbox 与 V7 stream scheduler 都有固定上限。互斥只保护队列、stream 状态与 diagnostic snapshot，
不跨越 Vulkan record/submit、GPU wait 或 retirement。每 stream 最多 executing 1、pending latest 1、ready 1、persistent
full slot 3；全局 frame-resource 上限为 4。Studio exact resize 在 extent/generation 改变时立即 retire 不匹配 stream，
避免旧流三槽长期占满预算，并让 latest exact generation 竞争 retirement 释放的 lane；总量仍不突破该上限。

full slot 一体持有 image、producer-finished/consumer-available semaphore pair、fence、command pool/buffer 与 frame resource。
`NotSubmittedToConsumer` 清除该帧的 consumer wait；`ConsumerAccessed` 要求下一次复用在 GPU submit 中等待 compositor
signal。stream close 时，managed 先 dispose persistent imports 并发送 `release_slot_import_v7`；native 再以 producer fence
和必要的 consumer-release fence 驱动 retirement。attach 内单张 drawing surface 与 detach `Processed` 仍不能替代 GPU
completion proof，producer-fence-only image reuse 继续被拒绝。

RenderThread 启动或执行失败时，production fallback 是 typed failure 与 Studio viewport degraded/unavailable；禁止
退回 Avalonia UI 或任意 caller thread 直接执行 Vulkan。无法安全确认 compositor/GPU 完成的 external resource
进入有界 quarantine，保留 renderer/context 到进程终止兜底。若未来增加显式 smoke/debug 单线程模式，它也必须
在进程内保持唯一 owner，不能成为第二条 production 路径。

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
- Studio shared viewport 的 mailbox mutex 只保护 queue/snapshot；Vulkan context、producer、packet retirement 与
  resource destruction 只由 native RenderThread 访问。release work 必须拥有最高优先级和保留容量，不能丢弃或被
  render backlog 阻塞；control 其次，render saturation 返回 backpressure。
- Studio compositor submission 前拒绝的 packet 只能以 `NotSubmittedToConsumer` release；submission 成功后必须以
  `ConsumerAccessed` release。submission、imported wrapper disposal 或 native release 结果歧义时，packet 与 image
  lease 进入有界 process-lifetime quarantine，不能提交一个永远不会 signal 的 consumer wait，也不能猜测为未访问。
- Studio external image retirement 不允许 host-side semaphore wait 或 render-loop queue/device idle。consumer-done
  semaphore 由 owner thread 的空 queue submit 转为可轮询 fence，未完成 packet 保持完整资源所有权。
- worker task 失败返回 structured error；owner 线程决定 fallback resource 或终止 smoke。
- GPU resource destroy 不在提交帧立即执行，进入 deferred destruction 并等待 frame fence。
- 任何临时 `vkDeviceWaitIdle` 必须写注释说明是 shutdown、debug probe 还是 MVP 简化路径。
- Swapchain recreate 是当前单 graphics/present queue 的显式同步例外：先检查 in-flight fence 和
  `vkQueueWaitIdle`，再把旧 swapchain、views 和 present-wait semaphores 移入局部 RAII owner；新资源
  全部创建成功后才一次性安装，所有成功/失败返回都先按 semaphore -> view -> swapchain 清理旧集合。
  该路径不得扩展为普通 render-loop wait，也不得把 retired 集合累积到 shutdown。
- Vulkan 1.x 未扩展路径没有直接的 present-completion fence。Khronos 明确指出 submit fence 不涵盖
  presentation wait，`vkQueueWaitIdle` 在这里是 current single-queue 的 practical fallback，而不是可推广
  到异步或多 present queue 的形式证明。未来放宽同步回收前必须引入
  `VK_EXT_swapchain_maintenance1` present fences 或等价的 spec-backed completion proof：
  https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
- 通用 shutdown 顺序：停止接收新任务，drain worker，停止 render thread，等待 GPU idle，销毁 frame resources，
  销毁 long-lived resources，销毁 device/context。
- Studio shared viewport 的跨 compositor shutdown 更严格：停止 request admission → drain managed
  import/commit → release 或 quarantine frame lease → drain native mailbox → RenderThread 完成有界 retirement →
  仅在 shutdown 路径做必要的 idle wait → RenderThread 销毁 frame resources、producer 与 context → thread exit →
  caller 不持 queue/runtime lock join。
  quarantine 未能证明安全时不提前拆 context；对象留在 process-lifetime storage，RenderThread 可以在不析构这些
  对象的情况下 exit/join，最终由 OS 终止进程兜底。

## 文档与代码审查门禁

- 新增线程或 queue 时，必须更新本文和 `docs/architecture/flow.md` 的真实运行图。
- 新增 worker task 时，必须说明输入所有权、输出所有权、取消/失败语义和是否可并行。
- 新增 Vulkan 多线程录制时，必须证明 command pool、descriptor pool/cache、upload scratch 是 per-frame/per-thread。
- 新增 RenderThread 前，必须先有 RenderFramePacket 文档和 smoke fallback。
- 性能优化必须有 profiler counter；不能凭直觉加入常驻线程。

## 参考资料

- Godot thread-safe APIs: https://docs.godotengine.org/en/stable/tutorials/performance/thread_safe_apis.html
- Godot RenderingServer: https://docs.godotengine.org/en/stable/classes/class_renderingserver.html
- Unreal threaded rendering: https://dev.epicgames.com/documentation/unreal-engine/threaded-rendering-in-unreal-engine
- Unreal parallel rendering overview: https://dev.epicgames.com/documentation/en-us/unreal-engine/parallel-rendering-overview
- Unreal low latency frame syncing: https://dev.epicgames.com/documentation/en-us/unreal-engine/low-latency-frame-syncing
- Unity Job System overview: https://docs.unity3d.com/Manual/JobSystemOverview.html
- Bevy ECS quick start: https://bevy.org/learn/quick-start/getting-started/ecs/
- Vulkan Guide threading: https://docs.vulkan.org/guide/latest/threading.html
- Vulkan `vkQueueSubmit` external synchronization: https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit.html
- Khronos command buffer usage and multi-threaded recording sample: https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html
- Avalonia 12.0.4 Vulkan GPU interop sample: https://github.com/AvaloniaUI/Avalonia/blob/12.0.4/samples/GpuInterop/VulkanDemo/VulkanSwapchain.cs
