# Studio viewport rendering

最近更新：2026-08-09

## 当前 production 链路

```text
SceneDocument snapshot
  -> Application ViewportSession invalidation + coalesced RefreshRequested
  -> ViewportSession.TryPublishLatest immutable snapshot
  -> EngineBridge ViewportRenderStream (V5)
  -> editor_native per-stream pending-latest
  -> process-level EditorSharedViewportRuntime RenderThread
  -> renderer_basic_vulkan offscreen external image
  -> per-stream ready frame
  -> Avalonia CompositionDrawingSurface.UpdateWithSemaphoresAsync
  -> explicit frame completion
```

Studio Scene View 当前渲染深灰背景、analytic XZ world grid、原点 XYZ 轴和每个 debug proxy 的 XYZ 轴。它还没有把
`BasicRenderSceneDesc::drawItems` 接入真实 scene mesh；该缺口与 V5 presentation pipeline 分开。

## 模块职责

| 模块 | 责任 | 禁止 |
| --- | --- | --- |
| `ViewportSession` | document/camera/extent/exposed invalidation；coalesced refresh signal；发布 immutable request；维护内容呈现序列下界 | 持有 native/GPU handle；等待 frame completion 才允许新 request；把 geometry revision 冒充内容 revision |
| `ViewportBridge` / `ViewportRenderStream` | V5 ABI 映射、typed status、slot/frame lease | 调 Vulkan；猜测 stale native metadata |
| `ViewportCompositionControl` | persistent import cache、exact geometry generation、surface update、detach drain | 创建 renderer thread；逐帧 `Task.Run`；直接持 Vulkan object |
| `EditorSharedViewportRuntime` | stream registry、latest/ready/slot scheduler、唯一 owner thread、retirement | 引用 managed/SceneDocument object |
| `EditorSharedViewportRenderProducer` | full slot Vulkan resources、record/submit、grid/debug overlay | composition API；UI layout policy |

## V5 ABI

Frame path 只使用：

```text
open_stream_v5(compatibility) -> streamId
submit_latest_v5(streamId, owning request snapshot)
try_take_ready_v5(streamId) -> optional self-describing frame
complete_frame_v5(streamId, slotId, completionKind)
release_slot_import_v5(streamId, slotId)
close_stream_v5(streamId)
poll_stream_v5(streamId)
destroy_stream_v5(streamId)
```

V4 frame symbols不导出，也没有 managed fallback。`query_composition_compatibility` 是一次性 device/handle control plane；
`query_runtime_stats_*` 是 diagnostics，不属于 frame ownership。

request 复制：session id、target id/revision、sequence、kind、logical/allocation extent、camera 和最多 256 个 debug proxy。
ready frame 自描述：session/target/revision/sequence、kind、logical/allocation extent、slot identity、stable external handles、format、
memory size 和 native frame index。

## 帧与槽状态

```text
submit latest
   |
   v
pendingLatest (overwrite allowed)
   |
RenderThread, only when ready is empty and a slot can progress
   |
   v
Ready ----tryTake----> Presented ----complete----> Available
  ^                                                |
  |                                                |
  +---------------- next render/reuse -------------+
```

上限：executing 1、pending 1、ready 1、slot 3。ready 未被 consumer take 时，scheduler 不再产生第二个 ready；
这让 burst submit 稳定落到 pending-latest cell，而不是积累 FIFO。

一个 persistent slot 包含 external image、两个 external binary semaphores、producer fence、command pool/buffer、frame resource
context 和稳定 exported handles。Avalonia imported wrappers 以 slot identity 缓存；同一 slot 后续 frame 必须返回相同 handles。

## GPU synchronization

producer submit：

```text
optional wait consumerAvailable (slot was previously ConsumerAccessed)
record/render external image
signal producerFinished
producer fence tracks submit completion
```

composition update：

```text
wait producerFinished
sample external image
signal consumerAvailable
```

`UpdateWithSemaphoresAsync` completion / `ConsumerAccessed` 只表示 Avalonia surface update 已完成并允许资源复用，不表示该帧
已经被显示器扫描输出。物理 present/display-change 必须由 PresentMon、PIX 或 ETW 单独测量。

NotSubmitted frame 不会产生 consumerAvailable signal，因此 native 在下一次 slot reuse 前清除 consumer wait。stream retirement
若最后一帧 ConsumerAccessed，则通过 owner queue 的 wait-only submit 把 consumer semaphore 转成可轮询 fence；image 只有在
producer/consumer completion 都成立后才回收。

## Resize

control 以 `ceil(Bounds * RenderScaling)` 采样当前 panel `PixelSize`，并把同一个 extent 同时写入 request 的 logical/allocation
字段。V5 保留双字段以维持自描述 ABI，但 Studio surface presentation 的硬约束是：

```text
external-image allocation == frame logical extent == commit-time panel PixelSize
```

RenderGraph target、render area、viewport、scissor、camera projection、export/import 和 composition surface 全部使用这个 exact extent。
allocation padding + crop、旧 image stretch 和“最终再收敛”都不是可接受过渡帧；control 的 `ClipToBounds` 只防止越界绘制。

物理 extent 每次变化都会推进 geometry generation、立即隐藏旧 surface，并把尺寸不匹配的 active/desired stream 从可呈现集合移除后
开始逐流 retirement。新的 exact-extent desired stream 只消费 latest request。ready frame 在 composition callback 中再次复验
extent、generation、presentation identity/revision 和单调 sequence；通过后，drawing-surface image 与 visual size/opacity 在同一
compositor transaction 中更新。旧 stream completion 只能完成资源释放，不能更新 surface。

可见性同时要求 surface extent 与 geometry generation 匹配，所以 A→B→A 不会复活第一个 A 的 snapshot。每个 stream 的
`ViewportStreamWorkFence` 只等待本流 pump/presentations；全局 outstanding frame-resource 上限仍为 4，steady stream 最多三槽，
尺寸变化时尽早退役旧流，避免其三槽阻塞最新 exact generation。

## Managed pump

没有固定 UI render timer：invalidations 只覆盖最新状态。Bounds/DPI 通过一枚 `DispatcherPriority.Render` latch 在 layout 的 Render
boundary 提前发布最新 exact-size native request，ready frame 仍由后续 `RequestCompositionUpdate` 在 composition commit 前复验；
其他 invalidation 由下一次 composition callback 最多发布一次 request。`IsRealtime=true` 是 Scene View 默认值，并由 panel 的 Realtime toggle 显式控制；即使 scene/camera 静止，
每次 callback 仍只为下一次 composition commit 重挂一次，因此 native frame N+1 与 surface update N 并行，warm-up 后目标为每
compositor tick 一个 exact frame，最低验收为 60 FPS。`IsRealtime=false` 不自动产生下一条 realtime invalidation；session 从
clean 变 dirty 时只发送一次 `RefreshRequested`，control 将它 marshal 到 UI Render priority 并请求下一次 composition update。
后续 camera/target 修改在已有 pending request 上继续合并，不把 burst 变成 FIFO。

unpromoted resize candidate 只允许首个 request；自动 Realtime 预填充必须等首帧 Promote 后才恢复。真实 dirty 仍可 latest-wins
覆盖候选，避免以性能优化牺牲内容 freshness。尺寸变化会在排下一次 admission 前清空 active/desired identity 并按原 work fence
退役旧流，零尺寸也保留 `ExtentChanged`，因此 OnDemand 面板恢复到相同像素尺寸时不会永久空白。

不可见、detach 或 lifetime pause 都停止 admission。control 同时观察自身及 visual ancestors 的 `IsVisible`；隐藏的 dock tab 不
持续生产或更新 surface，重新可见时写入 `Exposed` 并请求一帧。attach 新 surface 或替换 session 同样写入 `Exposed`，因此 clean
OnDemand session 在 detach/reattach 或 tab 内容复用后不会永久空白。移除/替换 session 会立刻隐藏旧 surface 并逐流退役
active/desired stream；presentation lifetime replacement/resume 同样写入 `Exposed`，补回 pause 边界被拒绝的 clean request。
已 closed 但绑定尚未清除的 session 不再接收 UI/visibility/realtime invalidation。旧场景像素不能跨会话留在 panel。pump 只在所属 stream 有 pending、ready 或 executing 时短暂异步让步并
轮询。native submit/try-take/complete 都不等待 GPU，热路径没有逐帧 `Task.Run`。纯 native `try-take/poll` 等待显式使用
`ConfigureAwait(false)`，不会把每毫秒 continuation 排回 Avalonia dispatcher；UI thread 只进行 request snapshot、Avalonia import、
composition commit，以及完成帧到达后的单次状态更新。candidate stream close/poll 同样在 UI context 外等待，避免 resize 时与
layout、input 和 composition 争抢 dispatcher。每个 stream 独占 `ViewportStreamWorkFence`；close 先停止该流 admission，再只等待
该流的 pump 与 presentations，最后 dispose import、release slot-import 并 destroy。新 desired stream 不等待旧流 pump，切断
“旧三槽 + candidate 一槽占满全局四槽，而旧流退役又等待 candidate pump”的资源闭环。
若 stream open/submit 只返回可恢复的 `Backpressure`，pending reasons 保留并在下一次 composition commit 单次重试；终端 interop、
device 或 render failure 进入 degraded 状态，不建立错误热循环。

## 刷新身份、内容门禁与时间

Unity Scene View 的可采用部分是“语义 invalidation → repaint request → 实际 render”三段分离，以及隐藏 dock tab 不运行
持续 refresh；Asharia 不采用 Unity 默认静止 dirty-only 和 Always Refresh 约 30 Hz 的门限，因为本项目把前台 Realtime
exact surface-update `>=60 FPS` 定义为硬验收。OnDemand 是显式节能模式，不是默认模式。

| 字段 | owner 与语义 | 不用于 |
| --- | --- | --- |
| `TargetRevision` | Application 的 SceneDocument 内容 revision | camera、geometry、frame cadence |
| `RequestSequence` | 每个 session 单调的 immutable request / completion identity | wall time、simulation delta |
| `MinimumPresentableSequence` | managed 内容门禁；target/camera/exposed 改变后拒绝更早 request | extent resize；geometry 由 generation 单独门控 |
| native `frameIndex` | RenderThread 的进程级 render-attempt identity；失败允许留下 gap | 生成时间或假定 60 Hz |
| `timeSeconds` | `EditorSharedViewportRuntime` steady clock epoch 后的单调 elapsed | Avalonia commit、GPU timeline、物理 present 时间 |
| `deltaSeconds` | 上一次任意 stream 成功 shared-viewport record 到本次 sample 的真实间隔；首帧为 0 | World simulation fixed-step delta 或 per-view cadence |

`Realtime` 只改变 cadence，绝不推进内容门禁；否则下一拍 invalidation 会让正在返回的每一帧都变 stale。extent 同样不推进
内容门禁，exact size 由 geometry generation + commit-time extent 独占裁决。dirty-only 长时间空闲后，下一帧的绝对时间跳到
当前 monotonic elapsed，delta 反映真实空闲间隔，不按 `frameIndex / 60` 补播假帧。

selection 与 native overlay intent 当前还没有进入 V5 immutable request；现有 selection 只属于 shell，grid/axes 仍是 producer
固定策略。未来接线必须增加显式 view-state snapshot/revision，并纳入内容门禁，不能复用 `TargetRevision` 或只设置 managed flag。

## Close 与 quarantine

正常关闭：停止 submit，完成/拒绝已 take frame，移除 surface visual 并等 `Processed`，dispose imports，release slot import，
close/poll/destroy stream，最后 dispose drawing surface。runtime shutdown 在所有 presentation lifetime drain 之后。

submission 已开始但 completion 歧义、import disposal 失败或 native completion 失败时，lease、wrappers 和 stream 进入最多 4 项的
process-lifetime quarantine；不把歧义解释为 NotSubmitted 或 ConsumerAccessed。

## 诊断计数

native stream poll 暴露：submitted、coalesced、rendered、slot count、presented count、pending/ready/executing 和 lifecycle。
runtime stats 继续暴露 frame epochs、external image pool、renderer creation、owner thread 和 retirement。性能判断至少比较：

```text
submittedRequests
coalescedRequests
renderedFrames
slotCount
UpdateWithSemaphoresAsync latency
resize input-to-surface-update latency
unique exact-submitted / update-completed geometry generations
completed/observed generation coverage
Bounds-to-first-exact-submit / update-completion p95
requested-mismatch opacity hidden duty
surface-update FPS / p95 / maximum interval
physical present/display-change FPS / p95 / dropped frames
```

验收入口为 `Editor.exe --smoke-studio-viewport-cadence`，或设置
`ASHARIA_RUN_STUDIO_GPU_ACCEPTANCE=1` 后运行
`Realtime_scene_viewport_and_panel_resize_sustain_at_least_60_fps`。smoke 会先将 stream 预热到稳定多槽，再连续改变内部 Grid panel 的 exact
pixel extent；resize 窗口要求每个 accepted frame 都满足 allocation == logical == commit-time panel `PixelSize` 且
90 次刺激至少观察 72 个 Bounds generations 且 measurement window `>=500 ms`；按唯一 geometry generation 统计 exact update
completion `>=60/s`、相邻 completion p95 `<=25 ms`、Bounds→completion p95 `<=25 ms`，最终 Bounds generation 被观察后最多再
等待两个 Avalonia rendered composition batches；同时报告 generation coverage 与 requested-mismatch
opacity hidden duty。结束后回到初始 A extent，必须由一个新的 exact generation 恢复 surface，再以 5 秒稳态窗口
门控 exact surface-update `>=60 FPS`、p95 `<=25 ms` 和 max `<=100 ms`。
专用 GPU lane 另以 PresentMon/ETW 门控物理显示层。

2026-08-09 的 RTX 4060 / 200 Hz 最新 Release 证据：0.77 s resize 窗口观察 90 个 Bounds generations，完成 85 个 unique
exact generations（110.24/s，coverage 94.4%），相邻 completion p95 13.13 ms、Bounds→submit p95 5.95 ms、
Bounds→completion p95 12.39 ms、requested-mismatch opacity hidden duty 43.2%，最终尺寸在额外 1/2 rendered batches 内追上。
回到初始 A extent 并确认新 exact generation 后，稳态 5 秒 1120 帧（223.94 FPS；bounded window 223.20 FPS）、p95 5.08 ms、
max 5.53 ms。随后切换 OnDemand：camera change 只唤醒一个 exact frame，稳定后不再自发产帧；ancestor hidden 期间 frame
count 不变，重新可见、lifetime resume 和 clean session replacement 都由 `Exposed` 产生 exact frame
（sequence 1331/1332/1333/1334）。中间失败基线为 29 个 exact frame / 0.75 s
（38.72 FPS），其旧 active stream 占用三个全局 lane；旧的 total-exact 指标会被同 generation 重复帧污染，已由 unique-generation
gate 取代。一轮无 ETW event loss 的同 PID/QPC PresentMon 配对采样显示：resize 顶层物理 display 187.03/s、p95 9.99 ms、
max 15.04 ms、14.2% presents 未显示；steady 为 197.81/s、p95 5.06 ms、max 10.07 ms、11.7% 未显示。它只证明顶层窗口
display cadence；exact panel generation 由应用侧 unique gate 证明。后续两次复采报告 ETW event loss，已作废。
超过刷新率的 surface updates 会被 compositor 合并或丢弃，不能按 surface-update 数量推断物理帧数。

## 后续边界

- 接入真实 scene draw items，不改变 V5 presentation ownership；
- 多 Scene/Game/Preview view 共用 runtime，profile 后再决定 weighted fairness；
- compositor stall 若证实造成 graphics queue head-of-line blocking，再评估 dedicated retirement queue；
- 材质/动画 preview 创建独立 `ViewportSession`/stream，不复制 renderer 或 native thread。

## 参考

- [ADR-0006](../adr/0006-viewport-interactive-resize.md)
- [ADR-0011](../adr/0011-native-shared-viewport-render-thread.md)
- Avalonia GPU interop sample: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/samples/GpuInterop/DrawingSurfaceDemoBase.cs
- Avalonia compositor scheduling: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/src/Avalonia.Base/Rendering/Composition/Compositor.cs
- Avalonia Windows high-refresh fix: https://github.com/AvaloniaUI/Avalonia/pull/21643
- Unity Scene View refresh source: https://github.com/Unity-Technologies/UnityCsReference/blob/01963ac2f4a49b1a86c11e812faf472e1fa51db3/Editor/Mono/SceneView/SceneView.cs
- Unity `SceneView.RepaintAll`: https://docs.unity3d.com/6000.2/Documentation/ScriptReference/SceneView.RepaintAll.html
- O3DE viewport resize/update-later: https://github.com/o3de/o3de/blob/1fe32b68a99b83508bed05a6778fef023ad51c2d/Gems/Atom/Tools/AtomToolsFramework/Code/Source/Viewport/RenderViewportWidget.cpp#L229-L248
- Vulkan synchronization: https://docs.vulkan.org/spec/latest/chapters/synchronization.html
