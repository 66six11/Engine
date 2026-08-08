# ADR-0006：Scene View 交互式 resize 与 V5 多槽呈现

状态：Accepted / Implemented
日期：2026-08-08
最近修订：2026-08-09（exact-extent presentation）

## 背景

Scene View 拖动 dock 分隔条时，Avalonia `Bounds`、native offscreen render、GPU completion 和 compositor
采样处在不同时间域。把每次 `BoundsChanged` 当成必须顺序执行的 render command，会产生旧尺寸帧回放、重复分配、
UI 等待 native，以及画面在多个 extent 之间跳动。

此前 V4 虽然已经把 Vulkan 操作迁到唯一 native RenderThread，并修复了 consumer-done 前错误回收 image 的问题，
但 production 路径仍是同步 create/wait/release one-shot packet。它不能证明 interactive resize 下的 latest-wins、
持久 import identity 或 render/compositor overlap。

## 参考边界

- Unreal 的 threaded rendering 使用 immutable render command/snapshot 跨线程传递，RenderThread 不解引用随时可变的
  editor/game object；Asharia 采用相同的 owner boundary，但保留 package-first 和单一 native runtime。
- Avalonia 12.0.4 `VulkanSwapchainImage` 示例把 image、producer-finished semaphore 与
  consumer-available semaphore 作为持久 pair；下一次 producer 使用同一 image 前等待 consumer signal。
- Godot RenderingServer 也把 scene/UI object 与 render owner 分开；这支持“发布 snapshot，不跨线程借用 SceneDocument”这一选择。
- O3DE Atom 把 `OnViewportSizeChanged` 与 `OnRenderTick` 定义为独立通知；尺寸变化是状态，render tick 才是消费点。

采用模式：immutable request、single render owner、bounded latest-wins、persistent full slot。
拒绝模式：每个 resize event 一个 FIFO render command、每 viewport 一条 Vulkan thread、CPU bitmap readback、
producer-fence-only image reuse。

## 决策

### 1. V5 是 production 硬切 ABI

Studio 只调用以下 viewport frame 生命周期入口：

```text
editor_viewport_open_stream_v5
editor_viewport_submit_latest_v5
editor_viewport_try_take_ready_v5
editor_viewport_complete_frame_v5
editor_viewport_release_slot_import_v5
editor_viewport_close_stream_v5
editor_viewport_poll_stream_v5
editor_viewport_destroy_stream_v5
```

V4 create/release packet symbols不导出，managed 没有 fallback。兼容性 query、runtime stats 和 shutdown 是独立控制面，
不构成旧 frame path。

### 2. 每个 stream 的队列严格有界

```text
executing render : max 1
pending latest   : max 1
ready frame      : max 1
persistent slots : max 3
```

`submit_latest` 只复制 owning request snapshot；若 pending 已存在，新请求原位覆盖旧请求并增加 coalesced counter。
RenderThread 不回放被覆盖的 resize 中间态。`try_take_ready` 不等待 GPU；frame 携带 producer-finished semaphore，
compositor 在 GPU 侧等待。

ready frame 是自描述合同，包含 session/target/revision/sequence、logical/allocation extent、render kind、slot identity
与 external handles。managed 不保存“最近 N 个请求”猜测异步完成帧的身份。

### 3. full slot 持久复用

一个 slot 一体拥有：

```text
external image + allocation
producer-finished semaphore
consumer-available semaphore
producer fence + command pool/buffer
frame resource context
stable exported handles
managed imported image/semaphore wrappers
```

slot 状态为：

```text
Available -> Ready -> Presented -> Completing -> Available
                                      |
                                      +-> Retired (stream close)
```

`ConsumerAccessed` 表示 `UpdateWithSemaphoresAsync` 的 surface update job 已完成；它是资源复用边界，不是物理屏幕
scanout 时间。下一次复用同一 slot 的 producer submit 等待 consumer-available semaphore。
`NotSubmittedToConsumer` 明确清除这次 consumer wait，因为对应 signal 永远不会发生。

managed imported wrappers 只在 slot 首次出现时创建，后续 frame 验证 handles 不变并复用 wrapper。stream close 前先 dispose
wrapper，再调用 `release_slot_import_v5`；native 只有在 import release 和 GPU completion 都成立后才销毁 slot。

### 4. resize 使用 exact-extent geometry generation

`ViewportCompositionControl` 以 `ceil(Bounds * RenderScaling)` 计算 commit-time panel `PixelSize`。Studio 提交给 native 的
`logical extent` 与 `allocation extent` 都等于这个物理像素尺寸；V5 ABI 仍保留两个字段，但 Studio presentation 不使用
allocation padding。每次成功进入 `CompositionDrawingSurface` 的 frame 必须满足：

```text
frame allocation extent
  == frame logical extent
  == composition callback 中再次采样的 panel PixelSize
```

该约束同时覆盖 external image、export/import、RenderGraph target、render area、viewport、scissor 与 camera projection。
不得把大 allocation crop 到 panel，也不得把旧 image stretch 到新 Bounds；`ClipToBounds` 只是越界防线，不是尺寸同步机制。

一个 native stream 的 allocation extent 固定，不能原地改变。每次物理 pixel extent 变化都推进 geometry generation：

```text
Bounds / DPI changes
  -> advance geometry generation and hide the old surface visual
  -> remove active/desired references and immediately retire mismatched streams
  -> open an exact-extent desired stream and submit only the latest request
  -> candidate ready frame reaches the composition callback
  -> revalidate extent + geometry generation + presentation identity/sequence
  -> update surface image and visual placement in the same compositor transaction
  -> successful exact frame becomes active
```

surface 可见性同时要求 extent 与 geometry generation 匹配。因此 A→B→A 即使回到相同 pixel extent，也不能重新暴露 A 的旧
surface snapshot；只有第二个 A generation 的 exact frame 可以恢复 opacity。旧 stream 在尺寸变化时立即进入逐流 retirement，
其 completion 只能完成资源退役，不能再写 surface。这样旧 stream 的三个 slot 不会在快速 resize 中长期占住全局四个
frame-resource lane；新的 desired stream 仍受同一个全局硬上限约束，不会无界分配。

### 5. managed pump 是逐流所有权，实时帧由 compositor cadence 驱动

`Bounds`、camera、document revision、exposed 或 realtime invalidation 只覆盖待发布状态。`RequestCompositionUpdate` 在 layout
完成后的下一次 composition commit 前最多消费一次最新状态；不使用固定 16 ms UI timer，也不把每个 Bounds event 变成 frame。
`IsRealtime=true` 是 Scene View 默认策略：即使 scene/camera 静止，回调提交 frame N 后仍为下一次 commit 重挂 frame N+1，
并和同一拍消费的 frame N-1 重叠；warm-up 后目标为每个 compositor tick 一个 exact surface update，最低验收为 60 FPS。
`IsRealtime=false` 不自动重挂 realtime invalidation，只响应 initial、target、camera、extent、exposed 等 dirty 原因。detach、不可见
或 presentation lifetime 暂停时，两种模式都停止 frame admission。native submit 立即返回；managed pump 只在所属 stream
存在 pending/ready/executing 时以短异步让步轮询，没有逐帧 `Task.Run`。native-ready
与 stream-close 的 1 ms poll 使用不捕获 UI context 的异步等待；只有完成帧才恢复到 UI thread 执行 import 和 composition commit。
这避免 resize 期间把 polling continuation 与 Avalonia layout/input/render 排在同一个 dispatcher。composition commit 仍只有一个
稳定 `CompositionDrawingSurface`；`RequestCompositionUpdate` 同时作为 commit 前的 latest-state 门控和完成帧的 surface commit 边界。
每个 `StreamPresentationState` 独占一个 `ViewportStreamWorkFence`。candidate close 先封闭该流 admission，再只等待该流自己的
pump 与 presentation tasks，之后才释放 import/slot/stream；新 desired stream 可以同时启动自己的 pump。禁止用全局 pump task
同时承担新流生产与旧流退役，否则旧流三槽加 candidate 一槽会占满全局四槽，而旧流又等待被 candidate 卡住的同一 task，形成闭环。
可恢复 `Backpressure` 只在下一次 composition commit 单次重试，不依赖新的 Bounds event；终端失败仍进入 degraded 状态。

### 6. detach 与失败语义

正常 detach 顺序：

```text
invalidate attachment generation
-> remove CompositionSurfaceVisual
-> await removal batch Processed and current update
-> request stream close
-> dispose persistent imports
-> release slot-import identities
-> poll Closed
-> destroy stream
-> dispose drawing surface
```

若 compositor submission 已开始但结果歧义，或 import/native completion 失败，不猜测 completion kind；frame、imports 与 stream
进入有界 process-lifetime quarantine。该路径不得为了“收口”发出一个可能永远不 signal 的 consumer wait。

## 网格与 resize 的关系

world grid 的 `minorSpacing`/`majorSpacing` 是世界单位，camera projection 使用 exact panel extent 的 aspect。render target 与
panel 使用相同 pixel extent，不以 padding/crop 改变网格世界间距。小窗口中若出现视觉间距变化，应检查 projection 或错误的
旧帧缩放，不能通过按 viewport pixel size 改 shader spacing 修复。

## 结果与限制

- resize 中间 request 被 coalesce，不再按 FIFO 乱序呈现；
- native render、GPU 和 compositor 最多通过三槽重叠；
- 同一 slot 不再逐帧 create/export/import/destroy；
- 尺寸变化后旧 surface 立即隐藏并退役；新 generation 成功前允许短暂空白，但禁止 crop/stretch 的错误帧；
- 每个成功 surface commit 都满足 allocation == logical == commit-time panel `PixelSize`；
- Scene View 默认连续渲染；`IsRealtime=false` 则为 dirty-only，两者都由 composition commit 节奏驱动；
- Avalonia 从 12.0.4 升级到 12.1.0，Windows render cadence 至少取 60 Hz 与显示器最高刷新率中的较大者；
- 当前 consumer wait 和 producer submit 仍共用 graphics queue。compositor signal 异常延迟时可能造成 queue head-of-line
  blocking；只有 profiling 证明需要时，才增加 dedicated retirement queue。

## 验证

- native smoke 覆盖：ready/pending latest coalescing、三槽上限、第四请求 backpressure、invalid completion 不消费所有权、
  slot reuse、import release、close/destroy；
- managed tests 覆盖：V5 ABI layout、自描述 ready frame、exact-once completion、persistent slot identity、stream close；
- managed 行为测试覆盖：exact extent admission、A→B→A generation 不复活旧 surface、旧流退役不阻止新流 pump、pump 同步重入、
  close failure quarantine、cadence p95/max 与 ring wrap；
- opt-in `--smoke-studio-viewport-cadence` 在真实 Studio/Avalonia/Vulkan 窗口内预热三槽，再连续改变内部 Grid panel 的 exact
  pixel extent；resize 窗口要求每个 accepted frame 都满足 allocation == logical == commit-time panel `PixelSize` 且
  surface-update `>= 60 FPS`，随后以 5 秒稳态窗口硬门控 exact surface-update `>= 60 FPS`、p95 `<= 25 ms`、
  max `<= 100 ms`；物理显示层另用
  PresentMon/ETW 验证，不能从 `UpdateWithSemaphoresAsync` 推断；
- distribution 要求全部 V5 exports；旧 V4 create/release exports 缺失是预期硬切结果。

2026-08-09 的 RTX 4060 / 200 Hz exact-extent 实测中，中间版本因旧 active stream 占住三个全局 lane，只完成 29 个 exact frame / 0.75 s
（38.72 FPS，p95 32 ms，15 个 stale candidate reject）。加入尺寸变化立即 retire 与 geometry generation gate 后，最终 resize
窗口完成 57 个 exact frame / 0.76 s（75.11 FPS），通过 `>= 60 FPS` gate；回到初始 A extent 并确认新 exact
generation 后，5 秒稳态完成 1094 帧（218.76 FPS），p95 5.26 ms、max 9.46 ms。真实 Studio 静止场景的
PresentMon 物理显示证据为 189.8 FPS、display-change p95 9.94 ms；物理层仍必须在
专用 lane 独立复验，不能由 surface-update 数字代替。

## 资料

- Unreal threaded rendering: https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine
- Godot thread-safe APIs: https://docs.godotengine.org/en/stable/tutorials/performance/thread_safe_apis.html
- O3DE viewport resize/update-later: https://github.com/o3de/o3de/blob/1fe32b68a99b83508bed05a6778fef023ad51c2d/Gems/Atom/Tools/AtomToolsFramework/Code/Source/Viewport/RenderViewportWidget.cpp#L229-L248
- O3DE rejected 150 ms resize debounce: https://github.com/o3de/o3de/pull/19033
- Avalonia GPU interop sample: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/samples/GpuInterop/DrawingSurfaceDemoBase.cs
- Avalonia 12.1 Windows high-refresh fix: https://github.com/AvaloniaUI/Avalonia/pull/21643
- Avalonia multi-image interop rationale: https://github.com/AvaloniaUI/Avalonia/discussions/15948#discussioncomment-9712534
- Vulkan synchronization: https://docs.vulkan.org/spec/latest/chapters/synchronization.html
