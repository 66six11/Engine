# ADR-0006：Viewport Presentation Transaction 与 V5 多槽呈现

状态：Accepted / Implemented（platform-neutral capability + Windows integration；physical scanout acceptance pending）
日期：2026-08-08
最近修订：2026-08-10（interactive top-level resize capability 与 release-stop 合同）

## 背景

Scene View 拖动 dock 分隔条时，Avalonia `Bounds`、native offscreen render、GPU completion 和 compositor
采样处在不同时间域。把每次 `BoundsChanged` 当成必须顺序执行的 render command，会产生旧尺寸帧回放、重复分配、
UI 等待 native，以及画面在多个 extent 之间跳动。

只在 `Bounds` 改变后隐藏不匹配 surface 也不够：旧 frame 不会被 crop/stretch，但新 exact frame 就绪前会暴露 panel
背景。2026-08-09 的旧路径实测 `requested-mismatch opacity hidden duty` 为 43.2%；在 200 Hz 显示器上，即使一次空白
只持续约一个 display interval，也足以成为可见闪屏。更重要的是，`Bounds` 已经公开改变后，旧 image 必须在 crop、stretch
或空白三者中选择一个；因此严格的“exact、无拉伸、无裁剪、无空白”不能只在 `ViewportCompositionControl` 内事后修补，
必须让 Studio 自有 dock resize 采用 requested/committed 两阶段事务。这个问题并不只属于 dock：Scene View 的 exact
geometry、Game Preview 的独立 fit policy，以及 Frame Debugger 的 immutable capture 都需要在“候选内容已可用”和“成为可见
front”之间建立同一套可审计边界。因此 production 抽象是 `Viewport Presentation Transaction`；dock resize 只是向它提供
layout proposal 的 adapter，不拥有 surface、stream 或 transaction lifecycle。

相同矛盾也出现在 Studio 顶层 Window resize，但故障表现是 Scene-only flash：Window chrome 与其他 Avalonia 内容仍在，Scene
front 因新 workspace `Bounds` 已公开、exact replacement 尚未 ready 而短暂隐藏，并不是整个 HWND 闪烁。对于 Windows 普通装饰
边框拖动，如果 USER32 先接受 proposed `RECT`，事后同样只能在 crop、stretch 或 blank 中选择。Main Window 与 Floating Window
因此需要一个可选的 interactive top-level resize capability，在公开新 native geometry 前先准备 workspace 内所有受影响的 exact
Scene endpoint。该 capability 是 platform-neutral 的 application/presentation 合同；具体 native proposal hook 属于独立平台
integration，不进入 shared host、transaction coordinator 或 endpoint owner。

此前 V4 虽然已经把 Vulkan 操作迁到唯一 native RenderThread，并修复了 consumer-done 前错误回收 image 的问题，
但 production 路径仍是同步 create/wait/release one-shot packet。它不能证明 interactive resize 下的 latest-wins、
持久 import identity 或 render/compositor overlap。

## 参考边界

- Unreal 的 threaded rendering 使用 immutable render command/snapshot 跨线程传递，RenderThread 不解引用随时可变的
  editor/game object；这是引用事实。Asharia 采用其 immutable handoff 与 owner boundary，但不复制 Unreal 的模块/API，仍保留
  package-first、headless contract 和单一 native runtime。
- Avalonia 12.1.0 `VulkanSwapchainImage` 示例把 image、producer-finished semaphore 与
  consumer-available semaphore 作为持久 pair；下一次 producer 使用同一 image 前等待 consumer signal。
- Avalonia 12.1.0 `GridSplitter` 的 `ShowsPreview` 路径只移动 preview adorner，并在 `DragCompleted` 才写真实 definition。
  Asharia 采用 requested/committed layout 分离，但不采用“整次 drag 只在结尾 commit”；`EditorDockStagedGridSplitter` 把
  proposal 交给 project-owned coordinator，以 prepared exact frame 的 cadence 持续 commit。
- Godot RenderingServer 也把 scene/UI object 与 render owner 分开；这支持“发布 snapshot，不跨线程借用 SceneDocument”这一选择。
- O3DE Atom 把 `OnViewportSizeChanged` 与 `OnRenderTick` 定义为独立通知，并拒绝以 150 ms resize debounce 掩盖卡顿；这是引用
  事实。Asharia 采用“proposal 是状态、render/publish 是消费点”和无 drag-end debounce，不采用其 Qt widget owner/API。
- Unity Scene View 把 scene/camera/selection change 变成 repaint request，只有 repaint pass 才真正 render；隐藏 dock tab
  不运行持续 refresh。这是 Unity 公开源码/API 所能证明的行为。Asharia 采用这种状态/request/render 分层，但拒绝 Unity 默认
  dirty-only 与 Always Refresh 约 30 Hz，因为本项目要求前台 Realtime exact surface-update 至少 60 FPS。
- Avalonia 12.1.0 的 `Win32Properties.AddWndProcHookCallback` 允许 Windows integration 为 top-level 注册 Win32 hook；对应 Windows
  backend 源码中 hook 先于 Avalonia 内部 WndProc 处理执行，`handled=true` 会短路后续处理。Win32 `WM_SIZING` 把可读写的
  screen-space `RECT` 交给 integration 并允许返回 `TRUE` 接受修改后的矩形。这给普通装饰边框 drag 提供了 precommit seam，
  但没有把 USER32/DWM 与 Avalonia compositor 变成一个物理事务，也不是 shared presentation assembly 应拥有的 API。
- 已检查的 Unreal threaded-rendering public contract 与 Unity SceneView public source/API 都没有公开“native Window geometry 与
  editor viewport surface 共享一个 physical commit fence”的先例。Asharia 的 platform-neutral capability、独立 Windows integration
  与 release-stop 是由 exact Scene、package-first/cross-platform owner boundary 与 no-crop/no-stretch/no-blank 本地目标推导出的项目
  合同；不声称复制 Unity/Unreal API，也不外推为其他引擎内部实现不存在。

采用模式：immutable request、single render owner、bounded latest-wins、persistent full slot、per-endpoint ownership、
requested/committed state、independent candidate surface 与 prepare/validate/publish/render/retire transaction。
拒绝模式：每个 resize event 一个 FIFO render command、每 viewport 一条 Vulkan thread、CPU bitmap readback、
producer-fence-only image reuse、仅设置 `GridSplitter.ShowsPreview`、拖动结束 debounce、把 HWND/WM/P/Invoke 放进 shared
host/transaction，以及跨 compositor 或 native Window geometry 的伪原子 publish。

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

### 4. Viewport Presentation Transaction 是通用可见提交合同

每个 presentation endpoint 独占 front/candidate surface、stream、import cache、geometry/content gate 与 retirement。每个 participant
membership 的身份至少由 `SessionId + EndpointEpoch + TransactionId` 组成；group 内 `TransactionId` 共享，而 session/epoch 按 endpoint
复验：`SessionId` 防止跨内容会话复活旧帧，`EndpointEpoch`（当前 control 的 attach/
presentation generation）防止 detach、重绑或 compositor replacement 后的旧 completion 写回，`TransactionId` 区分同一 endpoint
上的 proposal。frame 的 request sequence、target revision 与 geometry generation 仍是更细的内容/尺寸门禁，不能代替这三个 owner
identity。

事务状态统一为：

```text
Proposal -> Preparing -> Prepared -> Validated -> Published -> Rendered -> Retiring -> Completed
Proposal/Preparing/Prepared/Validated -> Aborted
Published/Rendered/Retiring with ambiguous outcome -> Quarantined
```

`Prepared` 只表示独立 candidate surface 已完成 `UpdateWithSemaphoresAsync`，尚未成为 front；所有 participant 都进入
`Validated` 后才能 group publish。`Published` 表示同一 UI/composition turn 已写入目标状态，`Rendered` 表示该 compositor 的共享
batch barrier 已完成，随后才允许 retire replaced front。publish 前的失败/cancel 必须 abort 全组并保留旧 front；publish 后结果
歧义不能伪装为成功或回滚，必须 quarantine 仍可能被 compositor 引用的 owner graph。

同一 transaction 的 participant 只有在 `PresentationAtomicScope` 指向同一个 Avalonia compositor 时才允许 all-or-nothing group
publish：layout/state mutation 与所有 `visual.Surface`/`Size` switch 进入同一 batch。跨 compositor 没有一个共同的 Avalonia commit
barrier，因此明确不原子；调用方必须拆成独立事务并接受各自结果，不能用相同 `TransactionId` 宣称全局原子。

consumer policy 与 transaction engine 分离：Scene endpoint 使用 exact extent；Game Preview endpoint 可以拥有独立 fit policy，但 fit
结果必须在 proposal 中冻结，不能在 publish 时临时重算；Frame Debugger 的 immutable capture 是独立 endpoint/participant，冻结的
capture identity 不与实时 Scene/Game front 共用可变 presentation state。Dock adapter 只把 pointer delta、min/max 和 layout rounding
翻译成 proposal 与 `applyLayout`/`rollbackLayout` mutation。顶层窗口同样只通过
`IInteractiveTopLevelResizeAdapterProvider` / `IInteractiveTopLevelResizeAdapterFactory` /
`IInteractiveTopLevelResizeAttachment` / `IInteractiveTopLevelResizeSink` / `IInteractiveTopLevelResizeCommit` 与
`InteractiveTopLevelResizeProjection` 交换 platform-neutral proposal、attachment lifetime 与可逆 outer-layout commit；shared contract
不携带 native handle、message id 或 P/Invoke surface。

### 5. Scene exact policy、dock 与 Window layout adapter

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

一个 native stream 的 allocation extent 固定，不能原地改变。`EditorDockStagedGridSplitter` 把 pointer delta 先解释为 requested
layout，`EditorDockSplitResizePolicy` 计算受 min/max 与 layout rounding 约束的 definition proposal，
`EditorDockSplitResizeCoordinator` 保持当前 committed `GridLength`、viewport `Bounds` 与 visible front 不变，再把受影响 endpoint
交给 `ViewportPresentationTransactionCoordinator`。Dock adapter 的一次 proposal 执行：

```text
dock splitter proposal (latest wins)
  -> synchronously apply proposed GridLength only inside a layout probe
  -> capture proposed exact viewport PixelSize while Bounds notifications are probe-only
  -> restore committed GridLength before the UI dispatcher yields
  -> Proposal(SessionId, EndpointEpoch, TransactionId, participants)
  -> prepare one independent candidate CompositionDrawingSurface per endpoint
  -> await every candidate UpdateWithSemaphoresAsync; group reaches Prepared
  -> arm and validate every participant against its frozen policy/identity
  -> apply GridLength + every visual.Surface/Size in one same-compositor publish; Opacity remains 1
  -> await the shared composition batch Rendered barrier
  -> retire each replaced front through its endpoint-owned work fence
```

layout probe 必须同步完成并在返回 dispatcher 前恢复旧 `GridLength`；probe 期间的临时 `Bounds` 只用于测量，不推进可见 geometry
generation、不会隐藏 front，也不能发起普通 resize admission。prepared ticket 在真实 commit 前先 arm；任一 endpoint 的真实
`Bounds` 与 target 不等时，同一 UI turn 恢复旧 `GridLength` 并 abort 全组，不能把部分正确、部分错误的 surface 发布给 compositor。

front surface 可见性仍要求 extent 与 geometry generation 匹配。因此 A→B→A 即使回到相同 pixel extent，也必须由第二个 A
proposal 独立 prepare；ticket、generation 和 identity gate 禁止复活第一个 A 的旧 snapshot。candidate surface update 成功只表示
“可提交”，不表示已经成为 front；只有 armed exact `Bounds` 才能推进 geometry generation 与 presented state。失败、取消、过期
或 `UpdateWithSemaphoresAsync` fault 都保留旧 front、旧 committed layout 与 `Opacity=1`。compositor submission 结果歧义时隔离
candidate resources，不以显示错误画面换取回收。

旧 front 不能在 swap 指令进入 compositor 后立即销毁。只有 group surface/size switch 对应共享 batch 的 `Rendered` 完成，才开始逐流
retirement 并最终 dispose 旧 drawing surface；每个 stream 的 work fence 继续等待其 pump/presentation，external image 仍受
producer/consumer completion 约束。steady stream 的三槽加 candidate 首帧最多占用全局四槽；candidate 未成为 front 前禁止
Realtime 预填充，成功 `Published` 后才恢复新 front 的 steady 流水；旧 front 的实际 retirement/dispose 仍由后续共享 batch
`Rendered` barrier 门控。

Main Window 与 Floating Window 的 dock workspace 共用 `EditorDockPresentationLayoutHost`。host 只实现
`IInteractiveTopLevelResizeSink`，并从 application composition root 查询可选的
`IInteractiveTopLevelResizeAdapterFactory`；没有 capability 的平台不会 attach native hook。shared host、
`ViewportPresentationTransactionCoordinator` 与 `ViewportCompositionControl` 不引用 HWND、Win32 message、USER32 或 P/Invoke。
Windows 的 proposal interception、RECT projection 和 native commit owner 位于独立
`Asharia.Studio.Presentation.Avalonia.Windows` integration assembly。当前 Windows precommit scope 严格限定为 fixed-DPI epoch、
普通 decorated border drag：

```text
WM_ENTERSIZEMOVE
  -> require every visible viewport front exact
  -> snapshot last-accepted HWND RECT + client size + render scaling + client-to-workspace delta
WM_SIZING(proposed screen-space RECT)
  -> native hot path copies proposal, writes last-accepted exact RECT back, returns TRUE
  -> post at most one coalesced DispatcherPriority.Render drain
drain outside WndProc
  -> project latest proposed RECT to workspace logical size
  -> host keeps one active request + one queued latest successor
  -> synchronously probe all currently visible exact endpoint target extents
  -> restore committed workspace layout before the UI thread yields
  -> prepare candidate surfaces while HWND/layout/front remain at last accepted state
publish turn
  -> revalidate sizing/DPI/chrome/endpoint epochs
  -> SetWindowPos + TopLevel.UpdateLayout
  -> validate actual HWND/workspace Bounds and all exact endpoint extents
  -> publish all same-compositor surfaces; accept actual HWND RECT only after Published
WM_EXITSIZEMOVE
  -> close the interaction epoch; every unaccepted outer commit becomes stale
  -> discard the queued successor and never apply its raw proposed RECT after release
  -> let already-started candidate GPU/consumer work finish, then abort and retire it by owner fence
  -> keep the Window/workspace/front at the last Published exact RECT
```

WndProc hook 不遍历 visual tree、不创建 transaction object、不等待异步 prepare，也不运行 renderer；所有 projection、participant
discovery 与 proposal allocation 都在 coalesced drain 中完成。同一 interaction 的状态仍是最多一个 active preparation/publish 加一个
queued latest；新 `WM_SIZING` 只替换 queued successor，不取消已经进入 GPU/consumer 的 active candidate。publish 前 projector、
DPI/chrome epoch、endpoint membership、extent 或 outer apply 失败时保持/恢复 last-accepted HWND、committed workspace 与全部旧 front；
detach 或 interaction epoch 失效使尚未 publish 的 request 取消。`WM_EXITSIZEMOVE` 是明确的 interaction epoch 终点：尚未
`Accept()` 的 commit 从该消息起 `IsCurrent()==false`，shared host 不再把它应用到 outer layout；已进入 native render、GPU 或
consumer 的 candidate 不被同步强杀，而是在返回后走普通 pre-publish abort 与 work-fence retirement。publish 后歧义仍沿 transaction
quarantine 处理，不能回滚到可能已被 compositor 引用的旧 owner graph。

release-stop 选择“停在最后 Published exact RECT”，而不是在鼠标释放后追赶 raw cursor 的最后 proposal。因此 accepted RECT
允许比 raw proposed RECT 落后 0–1 个 candidate；diagnostics/smoke 必须同时输出 raw proposal、accepted RECT 与 pixel/logical lag，不能把
raw cursor 位置伪装成 committed truth。这个取舍消除了 release 后额外一次 `SetWindowPos` 所制造的 grow gap 或 shrink crop；代价是
窗口最终边缘可能相对释放点回退一个尚未完成的 proposal。拖动期间每次成功 Published transition 仍需要 native `SetWindowPos`，所以
USER32/DWM geometry 与 Avalonia batch 在 drag 中依然没有物理原子保证。

这个次序只提供应用/UI transaction contract。`SetWindowPos`/USER32/DWM 与 Avalonia composition batch 没有一个共享的公开
physical commit fence；即使 drag 中的 outer geometry、`UpdateLayout` 与 surface switch 位于同一 UI publish turn，也不能据此宣称
同一扫描帧物理原子。`RequestCompositionUpdate` 只安排 composition callback，batch `Rendered` 只证明该 Avalonia batch 已处理，二者
都不是 DWM/LCD scanout receipt。独立 Windows-only opt-in WGC acceptance 已实现 corner-sentinel DWM-composited pixel observer，并增加
release capture window：在 `WM_EXITSIZEMOVE` 关闭 epoch 后，所有 WGC-delivered release samples 都必须匹配最后 accepted/Published
exact extent，禁止 gap、crop、stretch、blank 与 spill。该 gate 只检查 WGC 实际交付的样本，不是无损 DWM refresh stream，也不是
LCD scanout evidence。

Snap、maximize/restore、程序化 Window/`Bounds` resize、DPI/跨屏 transition、没有 interactive resize capability 的非 Windows
top-level，以及其他无法 precommit 的 geometry source 不属于上述 seam，仍走 exact-only hidden fallback：新 Bounds 已公开后隐藏
不匹配 front，再等新 exact surface。它禁止 crop/stretch，但允许短暂 blank，尚未达到零闪。该 fallback 必须以单独 source/metric
记录，不能证明 owned dock 或 capability-backed precommit 验收，也不能反过来放宽其零隐藏契约。

### 6. managed pump 是逐 endpoint/stream 所有权，实时帧由 compositor cadence 驱动

`Bounds`、camera、document revision、exposed 或 realtime invalidation 只覆盖待发布状态。Studio dock resize 的 queued proposal
cell 同样 latest-wins：同一 drag 的新 pointer delta 只替换尚未开始的 queued successor，不取消正在 preparation/publish 的 active
proposal；显式 cancel、session/endpoint epoch 失效才终止尚未 publish 的 active candidate。active proposal publish 后立即开始准备
当前最新 proposal，而不是回放中间尺寸。未被 owned dock 或 interactive top-level resize capability 捕获的 resize fallback 仍通过一枚
`DispatcherPriority.Render` latch 合并，并在 layout 的 Render dispatcher boundary 提前提交最新 exact-size native request；ready frame
必须在后续 `RequestCompositionUpdate` callback 复验 commit-time extent/generation 后才能更新 fallback surface。非 resize invalidation 继续在
下一次 composition commit 前最多消费一次最新状态；不使用固定 16 ms UI timer，也不把每个 Bounds event 变成 FIFO frame。
`IsRealtime=true` 是 Scene View 默认策略，并由 panel Realtime toggle 显式控制：即使 scene/camera 静止，回调提交 frame N 后仍为下一次 commit 重挂 frame N+1，
并和同一拍消费的 frame N-1 重叠；warm-up 后目标为每个 compositor tick 一个 exact surface update，最低验收为 60 FPS。
新的 candidate 在首帧 prepared 并进入 group `Published` 前禁止自动 Realtime 预填充，只允许真实 target/camera/extent/exposed dirty
覆盖它；publish 并启动旧 front retirement 后才补发 Realtime wake、恢复 steady 三槽流水。这样不会为即将被下一次 proposal
替换的 generation 创建第二、第三个 image/packet。
`IsRealtime=false` 不自动重挂 realtime invalidation。session 从 clean 变 dirty 时发一个 coalesced `RefreshRequested`；control
把它 marshal 到 UI Render priority，下一次 composition callback 消费合并后的 initial/target/camera/extent/exposed 状态。
detach、祖先不可见或 presentation lifetime 暂停时，两种模式都停止 frame admission；新 surface attach、session replacement 或
hidden→visible 会写入 `Exposed`，presentation lifetime replacement/resume 也会留下同一 wake，保证 clean OnDemand session
能恢复一帧。已关闭但尚未解绑的 session 被视作正常 terminal boundary，不再从 property/visibility/completion continuation 发出
invalidation。session identity 移除/替换会先隐藏旧 surface、
清空 active/desired identity 并逐流退役，不能把旧 scene pixel 留到下一会话。native submit 立即返回；managed pump 只在所属 stream
存在 pending/ready/executing 时以短异步让步轮询，没有逐帧 `Task.Run`。native-ready
与 stream-close 的 1 ms poll 使用不捕获 UI context 的异步等待；只有完成帧才恢复到 UI thread 执行 import 和 composition commit。
这避免 resize 期间把 polling continuation 与 Avalonia layout/input/render 排在同一个 dispatcher。普通 steady/fallback commit 使用
当前 front `CompositionDrawingSurface`；transaction preparation 另建独立 candidate surface，避免候选 update 覆盖仍可见 front。
`RequestCompositionUpdate` 仍作为 commit 前 latest-state 门控；prepared resize 的 surface-update success 与 front switch 是两个明确边界。
每个 `StreamPresentationState` 独占一个 `ViewportStreamWorkFence`。candidate close 先封闭该流 admission，再只等待该流自己的
pump 与 presentation tasks，之后才释放 import/slot/stream；新 desired stream 可以同时启动自己的 pump。禁止用全局 pump task
同时承担新流生产与旧流退役，否则旧流三槽加 candidate 一槽会占满全局四槽，而旧流又等待被 candidate 卡住的同一 task，形成闭环。
可恢复 `Backpressure` 只在下一次 composition commit 单次重试，不依赖新的 Bounds event；终端失败仍进入 degraded 状态。

内容 freshness 与 geometry freshness 分开裁决。`TargetRevision` 只表示 SceneDocument revision；`RequestSequence` 是 immutable
request identity。target/camera/exposed 改变时，session 把 `MinimumPresentableSequence` 推到下一条 request，旧内容帧即使 document
revision 相同也不能跨过 surface commit。Realtime 只改变 cadence，extent 只推进 geometry generation；二者都不得推进内容序列下界。
把 extent 同时放进内容 fence 会使高频 resize 的 in-flight frame 永久落后下一次 Bounds，实测会把 resize 从 60+ FPS 降到
1.32 FPS，因此明确拒绝。

### 7. detach 与失败语义

正常 detach 顺序：

```text
invalidate attachment generation
-> remove CompositionSurfaceVisual
-> await removal batch Processed and current/candidate update
-> request stream close
-> dispose persistent imports
-> release slot-import identities
-> poll Closed
-> destroy stream
-> dispose all front/candidate drawing surfaces
```

若 compositor submission 已开始但结果歧义，或 import/native completion 失败，不猜测 completion kind；transaction 进入
`Quarantined`，每个 endpoint owner 保留仍可能被 compositor 引用的 frame、surface、imports 与 stream。该路径不得为了“收口”
发出一个可能永远不 signal 的 consumer wait，也不能只改变 group 状态却遗失具体 resource owner。

## 网格与 resize 的关系

world grid 的 `minorSpacing`/`majorSpacing` 是世界单位，camera projection 使用 exact panel extent 的 aspect。render target 与
panel 使用相同 pixel extent，不以 padding/crop 改变网格世界间距。小窗口中若出现视觉间距变化，应检查 projection 或错误的
旧帧缩放，不能通过按 viewport pixel size 改 shader spacing 修复。

## 结果与限制

- resize 中间 request 被 coalesce，不再按 FIFO 乱序呈现；
- native render、GPU 和 compositor 最多通过三槽重叠；
- 同一 slot 不再逐帧 create/export/import/destroy；
- `Viewport Presentation Transaction` 以 endpoint 为 owner，并用 `SessionId + EndpointEpoch + TransactionId` 拒绝跨会话、重绑与
  proposal 的 stale completion；
- Studio 自有 dock resize 只是 layout proposal adapter；candidate exact surface 成功前不改变 committed layout 或 visible front，
  group publish 时 opacity 始终为 1；
- shared presentation 只定义 platform-neutral interactive top-level resize capability；Main/Floating Window 的 workspace host 通过可选
  factory/attachment/commit 合同接收 proposal，Windows 的 HWND/message/P/Invoke 实现独立留在
  `Asharia.Studio.Presentation.Avalonia.Windows`；
- Windows fixed-DPI 普通装饰边框 drag 保持旧 HWND/workspace/front 在 last accepted exact state，以 active + queued-latest 准备下一枚
  workspace transaction，成功 publish 后才接受新 RECT；`WM_EXITSIZEMOVE` 使所有未接受 proposal stale，并停在最后 Published exact
  RECT，已进入 GPU/consumer 的 work 自然完成后按 owner fence abort/回收；
- candidate 失败、取消、过期或任一真实 extent 不匹配时全组保留旧 front；同一 compositor 的共享 batch `Rendered` 后才退役
  被替换 surface/stream；跨 compositor 明确不原子；
- Scene exact、Game Preview fit 与 Frame Debugger immutable capture 可以作为独立 endpoint policy/participant，不共享可变 front state；
- release-stop 不在鼠标释放后追加一次 native geometry catch-up；它消除该额外 `SetWindowPos` 所造成的 grow gap/shrink crop，但 raw
  accepted final 相对 raw cursor final 允许落后 0–1 candidate，必须输出 raw/accepted RECT 与 pixel/logical lag；
- Snap、maximize/restore、程序化 Window/Bounds、DPI transition、没有 capability 的非 Windows top-level 与其他无法 precommit 的
  geometry source 仍是 exact-only hidden fallback；该边界不变，禁止 crop/stretch 但未达成零闪；
- drag 中 USER32/DWM Window geometry 与 Avalonia batch 仍没有 physical atomicity 保证。Windows-only WGC pixel gate 新增 release
  capture window，要求它实际交付的 release samples 全部匹配 accepted/Published exact extent；WGC 仍不是无损 DWM refresh 记录或 LCD
  scanout evidence，不能标记为 `PhysicalDisplayed`；
- 每个成功 surface commit 都满足 allocation == logical == commit-time panel `PixelSize`；
- Scene View 默认连续渲染；`IsRealtime=false` 则为 dirty-only，两者都由 composition commit 节奏驱动；
- hidden dock tab 停止产帧；重新可见或新 surface attach 通过 `Exposed` 恢复一个 exact frame；
- Avalonia 从 12.0.4 升级到 12.1.0，Windows render cadence 至少取 60 Hz 与显示器最高刷新率中的较大者；
- native owner 按稳定 stream id 从上次成功 lane 后 round-robin，并在任何 render 前全局优先 completion/close；这消除了
  `unordered_map` 首流偏置，但没有扩大 global outstanding/context cap 4；
- 当前 consumer wait 和 producer submit 仍共用 graphics queue。compositor signal 异常延迟时可能造成 queue head-of-line
  blocking；只有 profiling 证明需要时，才增加 dedicated retirement queue。

## 验证

- native smoke 覆盖：ready/pending latest coalescing、三槽上限、第四请求 backpressure、invalid completion 不消费所有权、
  slot reuse、import release、close/destroy、四 cold stream round-robin 以及 completion/close-before-render；
- managed tests 覆盖：V5 ABI layout、自描述 ready frame、exact-once completion、persistent slot identity、stream close；
- managed 行为测试覆盖：transaction phase/exact-once publish、atomic-scope mismatch、pre-publish abort、post-publish quarantine、
  render/retirement barrier、exact extent admission、A→B→A generation 不复活旧 surface、同 extent 不新增 generation、旧流退役不阻止新流 pump、pump 同步重入、
  close failure quarantine、content sequence fence、coalesced OnDemand wake、ancestor re-exposure、clean session replacement、
  lifetime replacement/resume re-wake、closed-session UI boundary、failed legacy render re-wake、candidate Published 前的 Realtime
  admission policy、layout probe 不发布 geometry、prepare failure rollback、queued latest-wins 与显式 stale cancellation、armed exact/mismatch
  commit、surface switch retirement、cadence p95/max、唯一 generation、hidden 区间并集与 ring wrap；
- opt-in `--smoke-studio-viewport-cadence` 只负责前台静态 Scene 的 Realtime 稳态基线：预热后以独立 5 秒窗口硬门控 exact
  surface-update `>= 60 FPS`、p95 `<= 25 ms`、max `<= 100 ms`。它不注入 resize、过载、故障、supersede 或 multi-endpoint 场景；
- 五个独立 Studio GPU smoke family 已落地并由 process acceptance 分别启动真实 apphost：
  `--smoke-viewport-transaction-resize` 驱动真实 owned splitter 的五种轨迹 × 30/60/120/240 Hz、1 physical-pixel lane、0×0 恢复、
  unique exact generation、proposal→`Rendered`、hidden=0 与 panel/visual/surface exact；
  `--smoke-viewport-transaction-overload` 注入 5/15/30/50 ms prepare/Rendered delay，验证 active+queued latest 上界、active 不因同 drag
  新输入取消、candidate Publish 前只有一帧和最终 latest；
  `--smoke-viewport-transaction-faults` 在 13 个 surface/stream/submit/lease/import/update/publish/render/retirement stage 注入真实故障，
  核对 pre-publish old-front、post-publish quarantine 和最终 ownership receipt；
  `--smoke-viewport-transaction-supersede` 覆盖 Published-before-Rendered、B failure/cancel、A→B→A 新 identity/surface 与 committed baseline；
  `--smoke-viewport-multi-endpoint` 当前覆盖同 compositor 两 endpoint 的同文档双 Scene、Scene+Game ownership、validation reject
  和 post-publish group quarantine。3–4 realtime endpoint、公平资源预算与 slow-consumer 隔离仍是明确 blocker，不属于当前通过范围；
- `--smoke-viewport-transaction-flash` 另以 typed V5 diagnostic flag 在 native Scene surface 写四色 corner sentinel，逐 batch 检查
  Bounds/front/candidate/visual/surface/opacity/identity 以及 blank/out-of-bounds/stretch/crop；当前没有可靠 window pixel capture，输出
  `pixelEvidenceAvailable=false`，不声称 PhysicalDisplayed；
- `--smoke-viewport-transaction-window-resize` 使用真实 Win32 HWND、真实 Avalonia compositor 与 Vulkan external surface 驱动
  `WM_SIZING` precommit，并以 `--viewport-window-evidence=` 拆成两个互不替代的证据通道：`performance` 不启动连续 composition-batch
  recorder，以 first `Proposed`→final exact `Rendered` 的纯 resize 窗口计算 unique generation rate；process acceptance 分别运行
  grow/shrink/A→B→A 三个 120 Hz、90-input case，并各自硬门控 `>=60/s`。`continuous` 只用短 ABA 轨迹连续采样
  outer/client/workspace/panel/front/surface composition batch，拒绝结构上的 blank/stretch/crop/gap/mismatch；连续 observer 会改变
  UI/compositor 负载，因此该通道不报告也不门控 FPS。release policy 还必须区分 raw final proposal 与 accepted final：
  `WM_EXITSIZEMOVE` 后不得追赶 stale proposal，并输出 0–1 candidate 的 pixel/logical lag；
- Window 两个证据通道都明确输出 `pixelEvidenceAvailable=false` 与 `physicalDisplayedEvidenceAvailable=false`。它们只能证明
  transaction/`Rendered` 时序或 composition-batch 结构，不能关闭 Scene flash 的 WGC pixel gate；
- `Asharia.Studio.WindowsCapture.Tests` 是独立 Windows-only opt-in pixel gate。设置
  `ASHARIA_RUN_STUDIO_WGC_DWM_ACCEPTANCE=1` 后，它启动真实 Editor/Vulkan smoke，通过 named-event handshake 在 exact baseline 后开始
  resize，并等待最终 accepted Scene extent。证据类型固定为 `wgc-dwm-composited-pixels`：drag capture 继续逐样本分类
  blank/stretch/crop/gap/spill；release capture 从 interaction epoch 关闭起要求每个 WGC-delivered sample 都与最后
  accepted/Published exact extent 一致，不再允许 release gap/crop/stretch/blank/spill。WGC 可能不交付每次 DWM refresh，且
  `PhysicalDisplayedEvidenceAvailable=false`；该 gate 不能外推到未交付的 DWM refresh、LCD scanout 或其他 geometry source；
- unique geometry 速率不可能超过输入速率：30 Hz lane 门控至少 95% 输入覆盖，60 Hz lane 允许 59 Hz 容差，120/240 Hz lane
  硬门控至少 60 unique exact `Rendered` generations/s；Bounds→exact submit p95 保持 `<=25 ms`，相邻 exact completion p95
  只允许 59.94 Hz/QPC 的小于 0.5 ms 采样容差（`<=25.5 ms`），max `<=100 ms`；
- 各 smoke 分层报告 native producer/resource、transaction phase/identity、Avalonia surface/`Rendered` 与 physical display 指标，禁止把
  surface completion 当作 scanout。物理层另用 PresentMon/ETW 验证，不能从 `UpdateWithSemaphoresAsync` 推断；
- distribution 要求全部 V5 exports；旧 V4 create/release exports 缺失是预期硬切结果。

2026-08-09 的 RTX 4060 / 200 Hz exact-extent 实测中，中间版本因旧 active stream 占住三个全局 lane，只完成 29 个 exact frame / 0.75 s
（38.72 FPS，p95 32 ms，15 个 stale candidate reject）。旧的“全部 exact frame”计数还能被同一 generation 的第二、第三帧抬高，
因此不再作为 resize 验收。加入尺寸变化立即 retire、candidate 首帧前禁止 Realtime 预填充、Render-priority early admission 与唯一
generation tracker 后，最终 Release smoke 在 0.77 s 窗口观察 90 个 Bounds generations，完成 85 个 unique exact generations
（110.24/s，coverage 94.4%），相邻 completion p95 13.13 ms、Bounds→submit p95 5.95 ms、Bounds→completion p95
12.39 ms、requested-mismatch opacity hidden duty 43.2%，最终尺寸在额外 1/2 rendered batches 内追上。这组数据是 transaction
request/commit 之前的性能与闪屏基线，只证明 GPU/producer 吞吐充足；43.2% hidden duty 明确不满足新验收。

拆分后的 owned-splitter 代表性运行：sawtooth 120 Hz、240 次输入完成 209/209 observed exact `Rendered` generations，约 106.44/s，p95
15.26 ms、max 31.28 ms、hidden=0、mismatch=0；pixel 120 Hz 为 77/77、107.80/s；jitter 240 Hz 为 50/50、
105.01/s。新增 Window smoke 之前的五族 GPU process acceptance 为 47/47；overload 50+50 ms 保持 active cancel=0、pending<=2、candidate waste=0；
13 个 fault stages、supersede、六个双-endpoint modes 与 flash 8/8 transaction-batch structural checks 均真实 Vulkan exit 0。
5 秒 Realtime steady 的代表值为 219.43
surface-updates/s、p95 5.36 ms。它们证明 application proposal、exact candidate、same-compositor group publish 与 Avalonia
`Rendered`/surface-update cadence，不等于物理 scanout；当前 transaction 的 PresentMon 复采因大量 ETW event loss 且未生成 CSV
被明确作废。这些数据也不是 Win32 outer-Window precommit 的逐帧 pixel evidence。

release-stop 之前 `wait-final` policy 的 Win32 Window `performance` 代表性 ABA 运行在 744.47 ms 内发送 90 次输入；从 first `Proposed` 到 final exact
`Rendered` 的验收窗口为 757.57 ms，得到 50 个 unique exact `Rendered` generations，即 66.00/s。最终 request 的
post-request transaction publish catch-up 为 2/2，耗时 25.44 ms（小于两个 60 Hz composition budget），hidden=0；同轮
grow/shrink/ABA 三个 120 Hz、90-input process case 均通过 `>=60/s`。独立 `continuous` ABA 结构通道采到 24/24
structurally exact sampled composition batches；它不声称捕获了每个 DWM frame，
blank/stretch/crop/gap/mismatch 均为 0；该通道没有 FPS claim。两组数字仍分别止于 transaction `Rendered` 与应用侧连续 batch
采样，`pixelEvidenceAvailable=false`、`physicalDisplayedEvidenceAvailable=false`，没有 WGC 或 physical scanout 证明，也不作为新的
release-stop gate 的通过数据。

2026-08-10 旧 grow-only policy 的独立 monotonic-grow WGC opt-in process acceptance 收到 11 个 samples：10 exact、1 allowed grow gap，
最大 gap 为 right 30 px / bottom 8 px，observer-known drops 为 0；这组历史 `wgc-dwm-composited-pixels` evidence 只关闭当时的 grow
lane，不关闭新增的 release delivered-samples exact gate。额外 shrink
探针的 12 个 samples 中有 2 个 sentinel missing/crop，因此不纳入当时的 PASS；该旧 drag 证据当时没有关闭 shrink，并继续作为
历史失败证据保留，不能与下述严格 release gate 混用。该结果也不改变 `PhysicalDisplayedEvidenceAvailable=false`，不能解释为 LCD
scanout evidence。

2026-08-10 的严格 release-stop WGC opt-in acceptance 同时运行 monotonic grow 与 shrink，并按
`Direct3D11CaptureFrame.SystemRelativeTime` 对齐 `release-imminent` 的 QPC/frequency，从发送 `WM_EXITSIZEMOVE` 前的保守边界开始
筛选，而不按 callback delivery sequence 猜测时序。两条 case 2/2 PASS：grow release window 为 1/1 exact，shrink 为 2/2 exact；
每个 delivered release sample 都同时满足 sentinel exact 与 `SceneBounds == completion accepted extent`，
gap/blank/crop/stretch/accepted-extent mismatch 全为 0。两条 case 都先确定 raw final proposal 尚在 pending，再由 interaction epoch
关闭使其以 `Cancelled` 退役，且 `rawFinalProposalAccepted=false`。该 PASS 关闭 grow/shrink 的 WGC-delivered DWM-composited release
样本，但仍不外推 WGC 未交付的 DWM refresh、ABA 或 LCD scanout。

此前基线在回到初始 A extent 并确认新 exact generation 后，5 秒稳态完成 1120 帧（223.94 FPS；bounded window
223.20 FPS），p95 5.08 ms、max 5.53 ms。同一次 Release smoke 随后证明 OnDemand camera change 只唤醒一个 exact frame、
hidden 期间不产帧、re-expose 与 clean session
replacement 精确恢复，lifetime pause/resume 也只补一个 `Exposed` exact frame。一轮无 ETW event loss 的同 PID/QPC PresentMon 配对采样显示：resize 顶层物理 display 187.03/s、p95
9.99 ms、max 15.04 ms、14.2% presents 未显示；steady 为 197.81/s、p95 5.06 ms、max 10.07 ms、11.7% 未显示。
这只证明 Studio 顶层窗口 display cadence；exact panel generation 仍由应用侧 gate 证明。后续两次 PresentMon 复采因报告 ETW
event loss 被明确作废，不能替代这轮有效数据。

## 资料

- Unreal threaded rendering: https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine
- Godot thread-safe APIs: https://docs.godotengine.org/en/stable/tutorials/performance/thread_safe_apis.html
- O3DE viewport resize/update-later: https://github.com/o3de/o3de/blob/1fe32b68a99b83508bed05a6778fef023ad51c2d/Gems/Atom/Tools/AtomToolsFramework/Code/Source/Viewport/RenderViewportWidget.cpp#L229-L248
- O3DE rejected 150 ms resize debounce: https://github.com/o3de/o3de/pull/19033
- Avalonia GPU interop sample: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/samples/GpuInterop/DrawingSurfaceDemoBase.cs
- Avalonia 12.1 GridSplitter preview/commit source: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/src/Avalonia.Controls/GridSplitter.cs
- Avalonia 12.1 Win32 WndProc hook property: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/src/Avalonia.Controls/Platform/Win32Properties.cs
- Avalonia 12.1 Win32 hook dispatch order: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/src/Windows/Avalonia.Win32/WindowImpl.cs#L959-L969
- Avalonia 12.1 Windows high-refresh fix: https://github.com/AvaloniaUI/Avalonia/pull/21643
- Avalonia multi-image interop rationale: https://github.com/AvaloniaUI/Avalonia/discussions/15948#discussioncomment-9712534
- Win32 `WM_SIZING`: https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-sizing
- Win32 `WM_EXITSIZEMOVE`: https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-exitsizemove
- Win32 `SetWindowPos`: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos
- Win32 `WM_DPICHANGED`: https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged
- Unity Scene View refresh source: https://github.com/Unity-Technologies/UnityCsReference/blob/01963ac2f4a49b1a86c11e812faf472e1fa51db3/Editor/Mono/SceneView/SceneView.cs
- Unity `SceneView.RepaintAll`: https://docs.unity3d.com/6000.2/Documentation/ScriptReference/SceneView.RepaintAll.html
- Vulkan synchronization: https://docs.vulkan.org/spec/latest/chapters/synchronization.html
