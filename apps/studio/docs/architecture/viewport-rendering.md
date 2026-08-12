# Studio viewport rendering

最近更新：2026-08-12

## 当前 production 链路

```text
SceneDocument snapshot
  -> Application ViewportSession invalidation + coalesced RefreshRequested
Viewport presentation proposal (Scene exact / Game fit / Frame Debug immutable capture)
  -> optional dock probe or platform-neutral interactive top-level resize capability
  -> Windows integration maps fixed-DPI WM_SIZING to that shared capability
  -> workspace host keeps one active request + one queued latest; committed layout/front stay visible
  -> ViewportPresentationTransactionCoordinator
  -> ViewportSession.TryPublishLatest immutable snapshot
  -> EngineBridge ViewportRenderStream (V7)
  -> editor_native per-stream pending-latest
  -> process-level EditorSharedViewportRuntime RenderThread
  -> renderer_basic_vulkan offscreen external image
  -> per-stream ready frame
  -> per-endpoint candidate CompositionDrawingSurface.UpdateWithSemaphoresAsync
  -> same-compositor group validate/publish; optional GridLength or Window layout + all visual Surface/Size
  -> shared batch Rendered; endpoint-owned retirement
  -> explicit frame completion
```

Studio Scene View 渲染深灰背景、analytic XZ world grid、原点 XYZ 轴和每个 debug proxy 的 XYZ 轴。它还会把同一
`SceneDocumentSnapshot` revision 中显式 authored mesh 引用交给 `scene-rendering` 做 immutable extraction，并通过
`BasicRenderViewSceneDesc::drawItems` 记录真实 indexed Scene draw。当前唯一 Ready product binding 是 #366 的 directional-wedge
validation mesh；未知或未就绪 asset 逐项 no-draw，不能替换成默认几何，也不代表通用 mesh importer/runtime resolver 已完成。

authored mesh request 原样复制 local `TransformValue`：position、单位 quaternion `(x,y,z,w)` 与逐轴 scale；Studio
不在 request path 做 normalize、Euler conversion 或 scale fallback。Inspector 在该 request path 之前以 X/Y/Z degree
编辑 local rotation，固定 YXZ 顺序，并在 project mutation boundary 转成 authoritative quaternion。`--smoke-studio-scene-mesh` 使用 normalized
non-identity quaternion 与 non-uniform scale，并把发布时 request 与 acquired lease 按 sequence 精确关联，验证
`SceneId`、document revision、stable object id、runtime `EntityId`、asset id、完整 Transform 和
`MinimumPresentableSequence` 均来自同一个 authoritative snapshot。superseded request 必须严格落在新 fence 之前，
最终 request 必须精确等于该 fence。该 smoke 的 `PixelEvidenceAvailable=false` 仍是事实：它证明真实 Vulkan
indexed-draw/receipt 与 composition presentation closure，不冒充 WGC pixel、DWM refresh 或 physical scanout 证据。

Inspector 不直接暴露 quaternion XYZW；它显示三个 degree 字段，并在当前选择会话保留 Euler hint、按轴 dirty 状态与
来源及成功/失败均可识别的 project edit。自身成功回执和同姿态 snapshot 不重新分解或改写文本；no-op 成功允许保持
base revision，失败回执清理 pending 并将草稿重基，Apply 期间继续修改的轴由 per-axis edit version 防止旧回执覆盖。真正外部的不同 quaternion 才在
重组验证姿态等价后选择最接近 hint 的 YXZ 表示，`q` / `-q` 视为同一姿态，奇异区也由 hint 选择连续解。该
presentation 不增加 runtime scene 字段、viewport ABI 字段或第二个 rotation truth；hint 的跨会话持久化、gizmo、snapping、
multi-selection、连续多圈 animation history 与可配置 Euler order 仍 deferred。

## 模块职责

| 模块 | 责任 | 禁止 |
| --- | --- | --- |
| `ViewportSession` | document/camera/extent/exposed invalidation；coalesced refresh signal；发布 immutable request；维护内容呈现序列下界 | 持有 native/GPU handle；等待 frame completion 才允许新 request；把 geometry revision 冒充内容 revision |
| `ViewportBridge` / `ViewportRenderStream` | V7 ABI 映射、typed status、view-local FOV axis、authored-mesh snapshot、slot/frame lease 与 request-correlated mesh receipt | 调 Vulkan；猜测 stale native metadata；把 asset GUID 替换成 backend resource key |
| `ViewportPresentationTransactionCoordinator` | 以 `SessionId + EndpointEpoch + TransactionId` 协调 Proposal→Completed/Aborted/Quarantined；同 compositor group barrier | 假定跨 compositor 原子；拥有 endpoint surface/stream；把 dock policy 写进通用状态机 |
| `EditorDockStagedGridSplitter` / `EditorDockSplitResizePolicy` / `EditorDockSplitResizeCoordinator` | latest splitter layout proposal、min/max/layout-rounding、同步 probe、requested/committed `GridLength`；作为 transaction adapter | 直接写 GPU handle；拥有 transaction/resource lifetime；把 drag event 变成 FIFO；只在 drag-end resize |
| `EditorDockPresentationLayoutHost` / `Asharia.Studio.Presentation.Avalonia.Windowing` capability | Main/Floating Window 共用 workspace layout owner；以 `IInteractiveTopLevelResizeAdapterProvider`、`IInteractiveTopLevelResizeAdapterFactory`、`IInteractiveTopLevelResizeAttachment`、`IInteractiveTopLevelResizeSink`、`IInteractiveTopLevelResizeCommit` 与 `InteractiveTopLevelResizeProjection` 接收可选 outer-layout proposal，并以 active + queued-latest 驱动 workspace transaction | 引用 HWND、WM message、USER32 或 P/Invoke；拥有 native hook；假定每个平台都有 precommit seam |
| `Asharia.Studio.Presentation.Avalonia.Windows` / `Win32InteractiveTopLevelResizeAdapterFactory` | 把 Win32 fixed-DPI 普通装饰边框 drag 的 native proposal、RECT projection、commit 与 interaction epoch 映射到 shared capability | 在 WndProc 中等待/渲染/遍历 visual tree；把 Snap、maximize、程序化 resize 或 DPI transition 冒充 `WM_SIZING` precommit；宣称 USER32/DWM 与 Avalonia batch 物理原子 |
| `ViewportCompositionControl`（endpoint owner） | persistent import cache、front/candidate drawing surface、exact geometry/content gate、publish receipt、detach/quarantine/drain | 创建 renderer thread；逐帧 `Task.Run`；把 surface/stream ownership 交给 Shell/ViewModel |
| `EditorSharedViewportRuntime` | stream registry、latest/ready/slot scheduler、唯一 owner thread、retirement | 引用 managed/SceneDocument object |
| `EditorSharedViewportRenderProducer` | full slot Vulkan resources、显式 product binding、scene-mesh extraction、record/submit、grid/debug overlay | composition API；UI layout policy；从 source path 或 Avalonia 猜 mesh product |

## V7 ABI

Frame path 只使用：

```text
open_stream_v7(compatibility) -> streamId
submit_latest_v7(streamId, owning request snapshot)
try_take_ready_v7(streamId) -> optional self-describing frame
complete_frame_v7(streamId, slotId, completionKind)
release_slot_import_v7(streamId, slotId)
close_stream_v7(streamId)
poll_stream_v7(streamId)
destroy_stream_v7(streamId)
```

V1–V6 frame symbols 不导出，也没有 managed fallback。`query_composition_compatibility` 是一次性 device/handle control plane；
`query_runtime_stats_*` 是 diagnostics，不属于 frame ownership。

request 复制：session id、target id/revision、sequence、kind、logical/allocation extent、camera、最多 256 个 debug proxy、
有界 authored mesh snapshots 与 per-view Scene raster mode。每个 authored mesh 只携 canonical object/asset UUID、transient
runtime `EntityId`、expected Mesh type 与 Transform；managed pointer、SceneDocument/World pointer 和 GPU resource key 都不跨 ABI。
ready frame 除 session/target/revision/sequence、kind、extent、slot、external handles、format、memory size 与 native frame index 外，
还携 request-correlated Scene mesh receipt。只有显式 diagnostic flag 才收集 indexed draw evidence；receipt 的 scene revision、
resolved/rejected counts 与 indexed draw count 是整批证据，`Representative*` source/asset/product/resource 字段只标识首个已解析 draw，
不得外推为每个 mesh 的逐项 receipt；这些字段必须自洽，否则整帧 fail closed。

## 视图局部的投影轴策略

`ViewportCameraSnapshot` 以 `FieldOfViewRadians + ViewportFieldOfViewAxis` 明确投影合同；这两个字段属于
viewport session/request 的 transient view state，不写入 `SceneDocument`。Scene View 默认使用 90°
`MaintainHorizontal`：宽度不变而只调整 dock 高度时，水平和垂直方向的像素尺度保持稳定，变化只增加或减少上下可见内容，
不会通过 camera dolly、auto-focus 或 FOV 插值制造第二次构图跳变。Game View 与 Preview 默认使用 60°
`MaintainVertical`，保留 authored/runtime camera 常见的垂直 FOV 语义；多个 endpoint 即使指向同一 document，也不能共享可变
camera 或投影策略。

采用依据：Unreal 的公开 `EAspectRatioAxisConstraint` 与 Editor viewport preference 证明编辑器需要显式选择保持水平或垂直
FOV；Godot `Camera3D` 的 `KEEP_WIDTH` / `KEEP_HEIGHT` 证明同类 axis constraint 可作为相机投影合同；Unity `Camera.fieldOfView`
则明确是垂直 FOV，因此支持 Game/Preview 保持垂直语义。Asharia 采用“显式、per-view axis constraint”，但不复制外部枚举/API，
也暂不实现 `MajorAxisFOV`、physical camera/gate fit 或 UI preference：当前只有 Scene 与 Game/Preview 两个已证实需求。

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

## Viewport Presentation Transaction

事务系统是 Scene/Game Preview/Frame Debugger 共用的可见提交边界，不是 dock resize helper。每个 participant 由自己的 endpoint
持有 front/candidate surface、stream、import cache 和 retirement；coordinator 只编排：

```text
Proposal -> Preparing -> Prepared -> Validated -> Published -> Rendered -> Retiring -> Completed
Proposal/Preparing/Prepared/Validated -> Aborted (publish 前可恢复失败/cancel)
Published/Rendered/Retiring -> Quarantined (结果歧义)
```

每个 participant membership 都复验 `SessionId + EndpointEpoch + TransactionId`：前两者分别绑定该 endpoint 的内容会话与一次
attach/compositor presentation lifetime，`TransactionId` 在 group 内共享并绑定 proposal；三者与
request sequence、target revision、geometry generation 共同拒绝 stale completion。所有 participant 都 Prepared/Validated 后，只有
`PresentationAtomicScope` 为同一 Avalonia compositor 的 group 才能在一个 UI turn 写入 layout/state 与全部 visual switch，并共享一个
composition batch `Rendered` barrier。跨 compositor 没有共同 commit barrier，必须拆分为独立 transaction，明确不提供
all-or-nothing。

Scene endpoint 的 policy 是 exact extent；Game Preview endpoint 可以冻结自己的 fit policy 和目标 extent，不得借 Scene geometry
generation；Frame Debugger immutable capture 使用独立 endpoint/capture identity，不覆盖实时 Scene/Game front。Dock splitter 仅是
proposal adapter：它提供同步 layout probe 和可回滚 layout mutation，surface/stream lifecycle 仍归 participant endpoint。

## Scene exact resize policy

control 以 `ceil(Bounds * RenderScaling)` 采样当前 panel `PixelSize`，并把同一个 extent 同时写入 request 的 logical/allocation
字段。V7 保留双字段以维持自描述 ABI，但 Studio surface presentation 的硬约束是：

```text
external-image allocation == frame logical extent == commit-time panel PixelSize
```

RenderGraph target、render area、viewport、scissor、camera projection、export/import 和 composition surface 全部使用这个 exact extent。
allocation padding + crop、旧 image stretch 和“最终再收敛”都不是可接受过渡帧；control 的 `ClipToBounds` 只防止越界绘制。

Studio 自有 dock resize 不先公开新的 `Bounds`。`EditorDockStagedGridSplitter` 把 drag delta 合并为一枚 latest proposal；
`EditorDockSplitResizePolicy` 计算符合 min/max 与 layout rounding 的 definition lengths；`EditorDockSplitResizeCoordinator` 临时应用 proposal
执行同步 layout probe，`ViewportCompositionControl` 在 probe scope 内只捕获目标 `PixelSize`，不推进可见 geometry generation、
不隐藏 front，也不走普通 Bounds admission。probe 在 UI dispatcher 返回前恢复 committed `GridLength`，所以 compositor 始终看到
旧 exact `Bounds` 与旧 exact front。

endpoint 为目标 extent 打开 candidate stream，并创建独立 `CompositionDrawingSurface`。candidate 只提交首帧；frame 经过 extent、
candidate generation、presentation identity/revision 与 sequence gate 后，先更新 candidate surface。必须等待
`UpdateWithSemaphoresAsync` 成功，才能返回 prepared handle；此时 candidate 仍不可见，也不能推进 current/presented diagnostics。
fault、显式 cancel 或 session/endpoint epoch 失效时，旧 committed layout、front surface 与 `Opacity=1` 保持不变，candidate 按
work-fence/quarantine 语义退役；同一 drag 的更新 proposal 只替换 queued successor，不取消 active preparation。

group 中每个 prepared handle 先 arm，然后 coordinator 应用 requested `GridLength`。所有真实 `Bounds` callback 再验证 exact
`PixelSize`：全部匹配时
推进预留 geometry generation，并在同一 UI/composition commit turn 设置 `visual.Surface`、`visual.Size` 与新布局；opacity 从不降为
0。任一不匹配时同一 UI turn 恢复旧 `GridLength`，abort 全组 candidate，所有 front 不变。surface/size switch 对应共享
composition batch 的
`Rendered` 完成后，才开始退役 replaced front stream 并 dispose replaced drawing surface；old completion 永远不能再写 front。

可见性同时要求 surface extent 与 geometry generation 匹配，所以 A→B→A 的第二个 A 必须独立 prepare，不能复活第一个 A 的
snapshot。每个 stream 的 `ViewportStreamWorkFence` 只等待本流 pump/presentations；全局 outstanding frame-resource 上限仍为 4：
旧 steady front 最多三槽，尚未 Published 的 candidate 只占首帧一槽。成功 `Published` 后恢复新 active stream 的 Realtime
预填充；旧 front 的实际 retirement/dispose 仍由共享 batch `Rendered` 门控。

Main Window 与 Floating Window 的 workspace 都由 `EditorDockPresentationLayoutHost` 包裹。shared host 实现
`IInteractiveTopLevelResizeSink`，从 application composition root 提供的可选 `IInteractiveTopLevelResizeAdapterFactory` 获得 attachment；
provider/factory/attachment/sink/commit 与 `InteractiveTopLevelResizeProjection` 全在 platform-neutral
`Asharia.Studio.Presentation.Avalonia.Windowing`。shared host、transaction coordinator 与 endpoint control 不携带 HWND、WM message、
USER32 或 P/Invoke。

Windows native ownership 独立位于 `Asharia.Studio.Presentation.Avalonia.Windows`。在 fixed-DPI epoch 内，普通装饰边框 drag 由
`Win32InteractiveTopLevelResizeAdapterFactory` attach：`WM_ENTERSIZEMOVE` 记录 last-accepted exact HWND `RECT`、render scaling 与
client→workspace delta；`WM_SIZING` 热路径只复制 proposed screen-space `RECT`、把 lParam 改回 last-accepted `RECT` 并合并一个
`DispatcherPriority.Render` drain。drain 在 WndProc 外投影 target workspace size，host 保持一个 active request 与一个 queued latest，
probe 所有当前可见且 exact 的 viewport；异步 prepare 期间 HWND、workspace layout 与旧 front 都停留在上一个 committed state。

所有 candidate prepared 后，workspace transaction 在同一 UI publish turn 经 `IInteractiveTopLevelResizeCommit` 应用 outer geometry；
Windows integration 才调用 `SetWindowPos`，host 再执行 `TopLevel.UpdateLayout`，重验 render scaling、client→workspace delta、真实 endpoint
集合/Bounds/extent，并切换全部 same-compositor surface。publish 前失败回滚 Window/workspace 并保留旧 front；只有成功 `Published`
才把实际 HWND `RECT` 记为下一枚 accepted state。

`WM_EXITSIZEMOVE` 关闭当前 interaction epoch：所有尚未 `Accept()` 的 commit 立刻 stale，queued successor 被丢弃，shared host 通过
`IsCurrent()==false` 拒绝 outer apply。已进入 native render、GPU 或 consumer 的 active candidate 不被同步强杀；它完成后走普通
pre-publish abort 与 work-fence retirement。窗口停在最后 Published exact RECT，不在 release 后追赶 raw cursor final。raw proposed
final 因而允许领先 accepted final 0–1 candidate；smoke/diagnostics 必须分别输出 raw proposal、accepted RECT 与 pixel/logical lag。
这消除了 release 后额外 native `SetWindowPos` 造成的 grow gap/shrink crop，但不承诺窗口边缘精确停在尚未接受的 raw proposal。

这个 bounded native integration 只覆盖 Windows fixed-DPI 普通装饰边框 drag。Snap、maximize/restore、程序化 Window/`Bounds`
resize、DPI/跨屏 transition、没有 interactive resize capability 的非 Windows top-level，以及其他无法 precommit 的 geometry source
仍走原有 exact-only hidden fallback：新 Bounds 已公开后立即隐藏不匹配 front，只在新的 exact frame 成功后恢复可见。它禁止
padding/crop/stretch，但允许短暂空白，因此这些路径尚未达到零闪，并且必须与 owned dock 和 capability-backed precommit 分开。

拖动中的 `SetWindowPos`/USER32/DWM 与 Avalonia composition batch 没有共享的公开 physical commit fence；“同一 UI publish turn”只
描述应用/Avalonia 事务顺序，不等于同一扫描帧的物理原子性。`RequestCompositionUpdate` 只安排 composition callback，batch
`Rendered` 只证明 Avalonia batch 已由 compositor 处理，都不是 DWM/LCD scanout receipt。Windows-only opt-in WGC acceptance 从真实 HWND 捕获
`wgc-dwm-composited-pixels`，并在 release capture window 要求它实际交付的每个样本都匹配最后 accepted/Published exact extent，拒绝
gap/crop/stretch/blank/spill。它不承诺无损取得每个 DWM refresh，更不观察 LCD scanout；因此即使 pixel evidence 可用，
`PhysicalDisplayedEvidenceAvailable` 仍必须为 `false`。

## Managed pump

没有固定 UI render timer：invalidations 只覆盖最新状态。dock adapter proposal 也只有一个 queued-latest cell；同一 drag 的新 pointer
delta 只替换尚未开始的 queued successor，不取消正在 preparation/publish 的 active proposal；显式 cancel、session/endpoint epoch
失效才终止尚未 publish 的 active candidate。一次 prepared generation publish 后继续准备当时最新 proposal，不回放每个 drag event。plain
`GridSplitter.ShowsPreview=true` 只把 resize 推迟到 drag completed，无法在交互期间产生 `>=60/s` unique exact generations；固定
drag-end debounce 同样被拒绝。未被 owned dock 或 interactive top-level resize capability 捕获的 Bounds/DPI fallback 继续通过一枚
`DispatcherPriority.Render` latch 在 layout 的 Render
boundary 提前发布最新 exact-size native request，ready frame 由后续 `RequestCompositionUpdate` 在 composition commit 前复验；
其他 invalidation 由下一次 composition callback 最多发布一次 request。`IsRealtime=true` 是 Scene View 默认值，并由 panel 的 Realtime toggle 显式控制；即使 scene/camera 静止，
每次 callback 仍只为下一次 composition commit 重挂一次，因此 native frame N+1 与 surface update N 并行，warm-up 后目标为每
compositor tick 一个 exact frame，最低验收为 60 FPS。`IsRealtime=false` 不自动产生下一条 realtime invalidation；session 从
clean 变 dirty 时只发送一次 `RefreshRequested`，control 将它 marshal 到 UI Render priority 并请求下一次 composition update。
后续 camera/target 修改在已有 pending request 上继续合并，不把 burst 变成 FIFO。

尚未 Published 的 candidate 只允许首个 request；自动 Realtime 预填充必须等 candidate surface update 成功、armed exact Bounds
commit 与 old-front switch 后才恢复。真实 dirty 仍可 latest-wins 覆盖候选，避免以性能优化牺牲内容 freshness。fallback 尺寸变化
会在排下一次 admission 前清空 active/desired identity 并按原 work fence 退役旧流；零尺寸也保留 `ExtentChanged`，因此 OnDemand
面板恢复到相同像素尺寸时不会永久空白。

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

Unity Scene View 公开源码/API 证明“语义 invalidation → repaint request → 实际 render”三段分离，以及隐藏 dock tab 不运行
持续 refresh；Asharia 采用这两个行为，不采用 Unity 默认静止 dirty-only 和 Always Refresh 约 30 Hz 的门限，因为本项目把前台
Realtime exact surface-update `>=60 FPS` 定义为硬验收。Unreal threaded rendering 的 immutable snapshot/owner boundary 被采用，
但不复制 Unreal 模块/API；O3DE 的 viewport-size state 与 render-tick 分离、拒绝 150 ms resize debounce 被采用，但不复制 Qt
widget ownership。已检查的 Unreal public threaded-rendering contract 与 Unity public SceneView source/API 都没有公开 native top-level
geometry 与 editor viewport surface 的物理原子提交先例；platform-neutral capability 及其 Windows `WM_SIZING` integration 因而是
Asharia 为 exact Scene、Avalonia boundary 与本地 no-crop/no-stretch/no-blank 目标定义的合同，不是对其他引擎 API 的复制。将 native
hook 留在独立 integration assembly、而非 shared transaction owner，是 package-first 与跨平台 Studio 边界的本地取舍。OnDemand 是显式节能模式，
不是默认模式。这些 Asharia 选择是基于引用行为和本地约束作出的 inference。

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

selection 与 native overlay intent 当前还没有进入 V7 immutable request；现有 selection 只属于 shell，grid/axes 仍是 producer
固定策略。FOV axis、authored mesh snapshots 与 Scene raster mode 已进入 V7，但这些都是 per-view policy，不写回 SceneDocument。未来 selection
接线必须增加显式 view-state snapshot/revision，并纳入内容门禁，不能复用 `TargetRevision` 或只设置 managed flag。

## Close 与 quarantine

正常关闭：取消尚未 arm 的 preparation，停止 submit，完成/拒绝已 take frame，移除 surface visual 并等 `Processed`，等待 front/
candidate update，dispose imports，release slot import，close/poll/destroy stream，最后 dispose 所有 front/candidate drawing surfaces。
正常 group switch 则先等共享 batch `Rendered`，再由各 endpoint 退役 replaced stream/surface。runtime shutdown 在所有 presentation lifetime
drain 之后。

submission 已开始但 completion 歧义、import disposal 失败或 native completion 失败时，endpoint lifetime 先用最多 4 个 frame
槽保留 lease/wrappers；post-publish 歧义或 endpoint detach 时再 exact-once 转交 process-lifetime quarantine registry。registry 不会
猜测资源已安全而主动回收，也不声称自身具有独立的 item-count 上界；歧义会把 endpoint 置为 degraded，正常路径与 soak 必须保持
quarantine 为 0，进程退出才是最终回收边界。不把歧义解释为 NotSubmitted 或 ConsumerAccessed。

## 诊断计数

native stream poll 暴露 submitted、coalesced、rendered、slot count、presented count、pending/ready/executing 和 lifecycle；runtime
stats 继续暴露 frame epochs、external image pool、renderer creation、owner thread 和 retirement。smoke 不共用一个模糊总 FPS，至少按
以下四层分别采集：

| 指标层 | 最低记录 |
| --- | --- |
| native producer/resource | offered/submitted/coalesced/rendered、pending/ready/executing、每流/全局 slot count、backpressure、frame epoch 与 retirement |
| transaction identity/phase | `SessionId`/`EndpointEpoch`/`TransactionId`、participant/atomic scope、Proposal→Prepared→Published→Rendered→Completed latency、Completed/Aborted/Quarantined、unique exact generation 与 coverage；top-level release 另记 raw proposal、accepted final、stale commit 与 pixel/logical lag |
| Avalonia surface | `UpdateWithSemaphoresAsync` latency、每 endpoint surface identity/extent/opacity、group batch `Rendered`、requested-mismatch hidden duty、full-window/transient commit、failed/cancelled/stale presented |
| WGC DWM-composited pixels | WGC-delivered sample count、corner sentinel shape/alignment、Scene bounds、drag 中的 gap 分类、blank/stretch/crop/spill；release window 的 delivered samples 必须全部匹配 accepted/Published exact extent；证据类型固定为 `wgc-dwm-composited-pixels`，不得标记 `PhysicalDisplayed` |
| physical display | 同 PID/QPC present/display-change FPS、p95/max interval、dropped/not-displayed、ETW event loss；不得由 surface 层计数推断 |

`Editor.exe --smoke-studio-viewport-cadence` 只定义前台静态 Scene 的 Realtime 稳态基线：预热后使用独立 5 秒窗口，门控 exact
surface-update `>=60 FPS`、p95 `<=25 ms` 和 max `<=100 ms`。cadence 不注入 resize、过载、fault、supersede 或 multi-endpoint，
避免一个长 smoke 的失败原因与指标窗口互相污染。

以下独立 Studio GPU smoke family 已在 2026-08-09 落地。入口各自启动真实 Studio、Avalonia compositor 与 Vulkan
shared-viewport，不再由一个长 cadence smoke 混合所有窗口；这些应用内入口的成功边界止于 `Rendered`。表末另列的 Windows-only
WGC observer 才提供 DWM-composited pixel evidence，但同样不能冒充物理 scanout：

| 入口 | 当前证据、唯一职责与关键 gate |
| --- | --- |
| `--smoke-viewport-transaction-resize` | 真实 owned splitter；grow/shrink/A→B→A/sawtooth/jitter × 30/60/120/240 Hz，加 1 physical-pixel lane 与 0×0 恢复。每个成功 `Rendered` generation 都要求 panel/visual/surface exact、hidden=0、crop/stretch=0、最终最多 2 个 composition batches 追上；Bounds→exact submit p95 `<=25 ms`，相邻 exact completion p95 `<=25.5 ms`（仅给 59.94 Hz/QPC 小于 0.5 ms 的采样容差），max `<=100 ms`。完整 100/125/150/200% DPI 当前只有 pure-policy 证据；真实 host 只报告实际 OS scaling，不能伪装成 DPI 注入。 |
| `--smoke-viewport-transaction-overload` | 注入 5/15/30/50 ms prepare 与 `Rendered` 延迟；同一 drag 只保留一个 active candidate + queued latest，不取消已经开始的 candidate，candidate Publish 前只生产一帧，最终 latest 收敛。 |
| `--smoke-viewport-transaction-faults` | 13 个真实阶段覆盖 surface/stream/create-submit、lease 后取消、partial import、surface update 已提交、prepare/publish/finalize/Rendered/retirement；pre-publish 保留旧 front，post-publish 只 quarantine，不错误 rollback，`RetirementCompletion` 单独给出最终资源 receipt。 |
| `--smoke-viewport-transaction-supersede` | A 已 Published 未 Rendered 时接收 B；B failure/cancel 后以最新 Published A 为 committed baseline；A→B→A 使用新 transaction/generation/surface，不复活旧 bitmap。 |
| `--smoke-viewport-multi-endpoint` | 同 compositor 两 endpoint：同文档双 Scene 与 Scene+Game ownership；从实际发布的 immutable request 断言 Scene `MaintainHorizontal`、Game `MaintainVertical`，防止 endpoint policy 串扰；覆盖 all-prepared→同 batch publish、validation reject 的 0 publish、post-publish finalize ambiguity 的整组 quarantine。3–4 endpoint steady、不同速率公平、单 endpoint slow/fault/detach 后其他 endpoint 继续推进，以及 slow-consumer queue 隔离尚未通过，见下文 native blocker。 |
| `--smoke-viewport-transaction-flash` | typed V7 diagnostic flag 把四色 corner sentinel 写入同一 native Scene external image；逐个成功 transaction 的共享 group composition batch 输出 Bounds/front/candidate/visual/surface/opacity/全部 identity，并拒绝结构上的 out-of-bounds、blank、stretch、crop、extent mismatch。它不是“每个物理显示帧”的采样，也尚未为 Win32 outer-Window precommit 提供逐帧像素证据；当前明确输出 `pixelEvidenceAvailable=false`。 |
| `--smoke-viewport-transaction-window-resize` | 真实 Win32 HWND、Avalonia compositor 与 Vulkan external surface；以 `WM_SIZING` 驱动 last-accepted RECT precommit。`--viewport-window-evidence=performance` 不启动连续 batch recorder，以 first `Proposed`→final exact `Rendered` 计算 unique rate；`continuous` 连续采样 outer/client/workspace/panel/front/surface composition batches 并拒绝 blank/stretch/crop/gap/mismatch。`--viewport-window-pattern=height-aba` 是 Scene projection 验收：outer/client/Scene/surface 宽度固定，至少两个 exact rendered 高度，把实际 request、native lease、呈现 sequence、extent/revision、90° `MaintainHorizontal` 与推导的 x/y pixel scale 写入 JSON，要求 scale drift `<=1 px`。release policy 在 `WM_EXITSIZEMOVE` 关闭 epoch，不追赶 stale raw final；height lane 还要求 final exact projection request 唯一、最终呈现 sequence 匹配，且 release 后 request/camera/projection mutation 均为 0。所有通道都输出 `pixelEvidenceAvailable=false` 与 `physicalDisplayedEvidenceAvailable=false`，不能关闭 WGC pixel gate。 |
| `Asharia.Studio.WindowsCapture.Tests` | Windows-only、显式 opt-in 的外部像素 observer；设置 `ASHARIA_RUN_STUDIO_WGC_DWM_ACCEPTANCE=1` 后启动真实 Editor/Vulkan Window smoke。drag 样本继续分类 blank/stretch/crop/gap/spill；release handshake 从 interaction epoch 关闭起要求每个 WGC-delivered sample 都与 child 报告的 accepted/Published exact extent 一致，不允许 release gap/crop/stretch/blank/spill。该 gate 不覆盖 WGC 未交付的 DWM refresh，也不证明 LCD scanout。 |

unique geometry 不可能快于 geometry input；因此 30 Hz lane 门控至少 95% 输入覆盖，60 Hz lane 允许 59 Hz 显示/调度容差，
120/240 Hz lane 才硬门控至少 60 unique exact `Rendered` generations/s。所有 lane 都继续要求最终 exact、hidden=0 与无 crop/stretch。
Window `performance` lane 的计时边界专门是 first `Proposed`→final exact `Rendered`，不包含随后 retirement、recorder shutdown 或
evidence serialization；`continuous` lane 的 observer 会主动请求并采集 composition batches，所以它只门控结构，不与性能 lane
混算吞吐。

每个入口把指标分为 native producer/resource、transaction identity/phase、Avalonia surface/`Rendered`、WGC DWM-composited pixels
和 physical display；没有接入某层 observer 时输出 `evidenceAvailable=false`/`null`，不能用 0 冒充证据。专用 GPU lane 仍需无 ETW event loss 的同 PID/QPC
PresentMon/ETW 才能门控 physical display；2026-08-09 两次当前 transaction 复采分别丢失约 253k/261k ETW events 且未生成 CSV，
因此没有被计入通过证据。

Window-resize 的 WGC gate 已有独立 Windows-only opt-in test project；它关联 WGC-delivered HWND pixels、accepted Scene extent 与 corner
sentinel，而不把交付样本数解释为 DWM refresh 数。drag 与 release 是不同窗口：drag 可分类报告中间 gap，release delivered samples
则必须全部 exact，不能保留 gap/crop/stretch/blank/spill 例外。PresentMon 只能证明顶层窗口 cadence，不能单独证明某个 Scene
generation 已显示，也不能排除这些 Scene-only 像素错误；WGC 同样不能证明 LCD scanout。

2026-08-10 旧 grow-only policy 的 monotonic-grow opt-in process acceptance 共收到 11 个 WGC samples：10 exact、1 allowed grow gap，
最大 gap 为 right 30 px / bottom 8 px，observer-known drops 为 0，因此只关闭当时的 grow lane。额外 shrink 探针的 12 个 samples 中有
2 个 sentinel missing/crop；该历史 drag 结果当时没有关闭 shrink，继续作为失败证据保留，但不替代下述严格 release gate，也不得
外推 ABA 或任意 resize pattern 已取得 WGC pixel acceptance。

2026-08-10 的严格 release-stop WGC opt-in acceptance 同时覆盖 monotonic grow 与 shrink，并以
`Direct3D11CaptureFrame.SystemRelativeTime` 对齐 `release-imminent` 的 QPC/frequency，从发送 `WM_EXITSIZEMOVE` 前的保守边界开始
筛选。两条 case 2/2 PASS：grow release 为 1/1 exact，shrink 为 2/2 exact；每个 delivered release sample 的 sentinel 与
`SceneBounds` 都分别通过 exact 和 completion accepted-extent gate，gap/blank/crop/stretch/accepted-extent mismatch 全为 0。两条
case 还都确定 raw final proposal 在 EXIT 前 pending、随后以 `Cancelled` 退役且 `rawFinalProposalAccepted=false`。该结果只证明 WGC
实际交付的 DWM-composited grow/shrink release 样本，不证明未交付 refresh、ABA 或 LCD scanout。

2026-08-09 的 RTX 4060 / 200 Hz 历史 exact-only Release 基线：0.77 s resize 窗口观察 90 个 Bounds generations，完成 85 个 unique
exact generations（110.24/s，coverage 94.4%），相邻 completion p95 13.13 ms、Bounds→submit p95 5.95 ms、
Bounds→completion p95 12.39 ms、requested-mismatch opacity hidden duty 43.2%，最终尺寸在额外 1/2 rendered batches 内追上。
这是 transaction request/commit 之前的基线：吞吐证明 native/GPU 路径有能力超过 60/s，但 43.2% hidden duty 明确不满足
无闪屏 gate，不能沿用为通过证据。

拆分后的代表性真实运行：sawtooth 120 Hz、240 次输入完成 209/209 observed exact `Rendered` generations，约 106.44/s，
p95 15.26 ms、max 31.28 ms、hidden=0、mismatch=0；pixel 120 Hz 为 77/77、107.80/s；jitter 240 Hz 为
50/50、105.01/s。新增 Window smoke 之前的五族 GPU process acceptance 为 47/47；overload 50+50 ms 保持 active cancel=0、pending<=2、candidate
waste=0；13 个 fault stages、supersede、六个双-endpoint modes 与 flash 8/8 transaction-batch structural checks 均真实 Vulkan
exit 0。5 秒 Realtime steady 的代表值为 219.43
surface-updates/s、p95 5.36 ms。所有这些仍是 application/Avalonia `Rendered` 或 surface-update 证据，不是物理 scanout。

release-stop 之前 `wait-final` policy 的 Win32 Window `performance` ABA 代表运行在 744.47 ms 内发送 90 次输入；first `Proposed`→final exact `Rendered`
持续 757.57 ms，共 50 个 unique exact `Rendered` generations（66.00/s），最终 request 的 post-request
post-request transaction publish catch-up 为 2/2，耗时 25.44 ms（小于两个 60 Hz composition budget），hidden=0。同轮
grow/shrink/A→B→A 三个 120 Hz、90-input case 均通过 `>=60/s`。独立 `continuous` ABA lane 采到 24/24 structurally exact
sampled composition batches，blank/stretch/crop/gap/mismatch 全为 0，并且明确
没有 FPS claim。两条 lane 都仍报告 pixel/PhysicalDisplayed evidence unavailable；这些历史结果不是逐帧 WGC 或 physical scanout
证据，也不作为新的 release-stop gate 的通过数据。

历史基线回到初始 A extent 并确认新 exact generation 后，稳态 5 秒 1120 帧（223.94 FPS；bounded window 223.20 FPS）、p95 5.08 ms、
max 5.53 ms。随后切换 OnDemand：camera change 只唤醒一个 exact frame，稳定后不再自发产帧；ancestor hidden 期间 frame
count 不变，重新可见、lifetime resume 和 clean session replacement 都由 `Exposed` 产生 exact frame
（sequence 1331/1332/1333/1334）。中间失败基线为 29 个 exact frame / 0.75 s
（38.72 FPS），其旧 active stream 占用三个全局 lane；旧的 total-exact 指标会被同 generation 重复帧污染，已由 unique-generation
gate 取代。一轮无 ETW event loss 的同 PID/QPC PresentMon 配对采样显示：resize 顶层物理 display 187.03/s、p95 9.99 ms、
max 15.04 ms、14.2% presents 未显示；steady 为 197.81/s、p95 5.06 ms、max 10.07 ms、11.7% 未显示。它只证明顶层窗口
display cadence；exact panel generation 由应用侧 unique gate 证明。后续两次复采报告 ETW event loss，已作废。
超过刷新率的 surface updates 会被 compositor 合并或丢弃，不能按 surface-update 数量推断物理帧数。

## 后续边界

- 用 authoritative asset catalog/resource runtime 替换当前 validation-only product binding，并补 imported mesh hot-reload；V7 presentation ownership 不变；
- selection outline/gizmo 继续作为 per-view editor feedback 独立接线，不写入 Scene schema 或 mesh material；
- 多 Scene/Game/Preview/Frame Debugger endpoint 共用 runtime 与 transaction contract；stable round-robin 已成立，但 global cap 4
  只够四 cold first slots，3–4 realtime 的 slot/context/显存预算仍须单独设计和实测；
- compositor stall 若证实造成 graphics queue head-of-line blocking，再评估 dedicated retirement queue；
- 材质/动画 preview 创建独立 `ViewportSession`/endpoint/stream，不复制 renderer 或 native thread；fit policy 留在各 preview proposal。

## 参考

- [ADR-0006](../adr/0006-viewport-interactive-resize.md)
- [ADR-0011](../adr/0011-native-shared-viewport-render-thread.md)
- [ADR-0012](../adr/0012-viewport-field-of-view-axis.md)
- Unreal threaded rendering: https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine
- Unreal Editor viewport FOV axis preference: https://dev.epicgames.com/documentation/unreal-engine/unreal-editor-preferences?lang=en-US
- Unreal `EAspectRatioAxisConstraint`: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/EAspectRatioAxisConstraint
- Unity `Camera.fieldOfView`: https://docs.unity3d.com/cn/6000.0/ScriptReference/Camera-fieldOfView.html
- Godot `Camera3D.KeepAspect`: https://docs.godotengine.org/en/stable/classes/class_camera3d.html
- Avalonia GPU interop sample: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/samples/GpuInterop/DrawingSurfaceDemoBase.cs
- Avalonia 12.1 GridSplitter source: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/src/Avalonia.Controls/GridSplitter.cs
- Avalonia 12.1 Win32 WndProc hook property: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/src/Avalonia.Controls/Platform/Win32Properties.cs
- Avalonia 12.1 Win32 hook dispatch order: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/src/Windows/Avalonia.Win32/WindowImpl.cs#L959-L969
- Avalonia compositor scheduling: https://github.com/AvaloniaUI/Avalonia/blob/12.1.0/src/Avalonia.Base/Rendering/Composition/Compositor.cs
- Avalonia Windows high-refresh fix: https://github.com/AvaloniaUI/Avalonia/pull/21643
- Win32 `WM_SIZING`: https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-sizing
- Win32 `WM_EXITSIZEMOVE`: https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-exitsizemove
- Win32 `SetWindowPos`: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos
- Win32 `WM_DPICHANGED`: https://learn.microsoft.com/en-us/windows/win32/hidpi/wm-dpichanged
- Unity Scene View refresh source: https://github.com/Unity-Technologies/UnityCsReference/blob/01963ac2f4a49b1a86c11e812faf472e1fa51db3/Editor/Mono/SceneView/SceneView.cs
- Unity `SceneView.RepaintAll`: https://docs.unity3d.com/6000.2/Documentation/ScriptReference/SceneView.RepaintAll.html
- O3DE viewport resize/update-later: https://github.com/o3de/o3de/blob/1fe32b68a99b83508bed05a6778fef023ad51c2d/Gems/Atom/Tools/AtomToolsFramework/Code/Source/Viewport/RenderViewportWidget.cpp#L229-L248
- O3DE rejected 150 ms resize debounce: https://github.com/o3de/o3de/pull/19033
- Vulkan synchronization: https://docs.vulkan.org/spec/latest/chapters/synchronization.html
