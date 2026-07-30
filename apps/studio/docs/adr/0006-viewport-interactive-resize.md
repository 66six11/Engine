# ADR-0006：面板交互 Resize 采用连续呈现与最新尺寸收敛

状态：Accepted

日期：2026-07-30

## Context

Studio 的嵌入式 Viewport 由 native Vulkan renderer 生成外部图像，再交给 Avalonia
compositor 显示。面板布局、native producer、GPU 与 compositor 是四个独立进度域。
拖动 Dock 分割条时，`CompositionHost.Bounds` 可以连续变化，而任一已开始的资源创建、
命令录制、GPU 提交或 surface update 都可能晚于最新布局。

旧实现已经具备 latest pending request、generation、双槽和有界退役，但仍会形成确定性
刷新饥饿：

- 面板 Bounds 变化先排队到下一次 composition callback，回调执行时才采样尺寸；
- completion 必须精确匹配当前 generation、sequence 和 Bounds 才允许提交；
- 连续拖动若快于 producer/import/commit，所有中间完成帧都会被当作 stale 丢弃；
- 最后成功帧保持原始尺寸并居中，面板放大时新区域只能等待 exact-size 帧；
- 每个 generation 首帧又会自动创建第二个 slot，短命 resize generation 因而多做一次
  GPU 资源创建；
- native packet release 的析构路径可能无限等待 fence，并与 create/render 共用 producer
  gate。

结果不是显式“停止拖动才刷新”，但在持续拖动时会表现得完全相同。固定 debounce、提高
轮询帧率或仅在停止拖动后重建都不能解决该所有权问题。

## Reference analysis

- Avalonia Win32 backend 会持续处理 `WM_SIZE`，`WindowBase` 随后执行布局；前端框架没有
  “拖动结束才给尺寸”的限制。`RequestCompositionUpdate` 的语义是下一次计划中的
  composition commit，不是立即布局通知，因此不能作为最新尺寸进入状态机的前置门槛。
- Unreal Engine 的 `FSceneViewport` 区分窗口几何、buffered frame 与 render-resource
  更新边界。旧资源可以继续显示，新资源异步准备，不在 UI resize 回调中等待 GPU。
- Vulkan 允许旧 presentation resource 与新资源并存并延迟销毁；Khronos 的 swapchain
  recreation 示例同样使用完成信号回收历史资源，而不是在交互热路径等待 device idle。
- Godot 的 `SubViewportContainer` 明确支持容器变化期间缩放已有 viewport texture。
  对编辑器交互而言，短暂重采样优于空白、停刷或输入延迟；exact-size 帧到达后再恢复
  精确像素映射。
- Unity Scene View 默认按需重绘。持续刷新是交互、动画或 Play 等显式活动策略，不应以
  固定 idle tick 掩盖 resize 调度问题。

## Decision

### 1. 布局完成后立即观察面板尺寸

`CompositionHost.SizeChanged` 是 Scene View 面板 resize 的主尺寸入口。回调读取同一次
布局后的 `CompositionHost.Bounds.Size` 与当前 `RenderScaling`，立即写入 presentation
的 latest state；它只启动异步工作，不在 UI 线程创建 Vulkan 资源。

顶层窗口 DPI 变化、scene revision 和显式工具交互复用同一观察函数。首次 capability
probe 若早于有效布局，只缓存 capability，并在首个有效 host size 到达时继续配置。

`RequestCompositionUpdate` 只承担两类职责：

- 在 compositor 边界提交 external surface update；
- native lane 暂不可用时提供无定时器的下一次重试节拍。

它不再阻挡 Bounds 进入 latest state。

一次观察同时捕获不可变值：

```text
ViewportFrameRequest
    ViewportId
    DisplaySizeDip
    RenderExtentPixels
    HasScene
    SceneRevision
    SessionEpoch
    SurfaceGeneration
    FrameSequence
```

禁止从 pixel extent 反推 DIP size。

### 2. Requested、ready 与 presented 分离

presentation 维护三个概念：

- `CurrentRequest`：当前布局和内容真正需要的最新请求；
- ready frame：已经完成 native/import、具备提交条件的帧；
- `LastPresentedSequence`：最后成功提交到 surface 的单调序号。

生产取消仍使用严格的 `IsCurrent`：尚停留在 producer gate 前的旧工作可以取消，避免
历史请求排队。

显示资格使用独立的 `CanPresent`。一个 ready frame 满足以下条件时可以作为 resize
过渡帧：

```text
仍在同一 SessionEpoch
同一 ViewportId
HasScene 与当前请求一致
SceneRevision 与当前请求一致
FrameSequence > LastPresentedSequence
```

仅尺寸 generation 过期不再使 ready frame 自动失去显示资格。这样 A→B→C 连续面板
resize 时，已经完成的 A 或 B 可以单调前进地显示，C 到达后再收敛到最终 exact extent。
场景 revision 在进入 surface update 前必须仍匹配；已经获准进入单一 drawing surface
更新的在途帧可能再落地一次，但它不会再次被接受，也不能覆盖序号更大的已接受帧。若 C
已先提交，序号较小的 A/B 也绝不回写。需要做到“revision 改变后旧像素绝不落地”时，
必须改用每槽独立 staging surface 后再原子切换，不能用一次异步调用后的检查伪造保证。

ViewModel 的成功状态仍只由严格 current frame 更新，过渡帧只改善可见连续性。
`MarkPresented` 必须在 surface update gate 内、成功 update 之后且释放 gate 之前执行；
否则较新帧的调用方 continuation 尚未记账时，较旧 waiter 可能抢到 gate 并回写 surface。

### 3. 最后成功 surface 始终覆盖当前面板

`CompositionSurfaceVisual.Size` 始终等于当前 `CompositionHost.Bounds.Size`，offset 为
零，并由 host 的 `ClipToBounds` 裁剪。

新 exact-size 图像尚未到达时，compositor 临时把最后成功 surface 重采样到当前面板。
这是交互 resize 的 latency-first 过渡，不是 renderer 的最终分辨率策略。新帧到达后，
surface 内容原子替换，最终尺寸继续由最新请求精确收敛。

surface update 在单个异步 gate 内串行执行。成功 completion 不再额外等待第二次
composition callback；失败保留 drawing surface 中上一份成功 snapshot。

### 4. 双槽按真实需求懒建立

每个 allocation generation 最多两个持久 slot，但首帧成功后不再自动 warm 第二槽。
同一 generation 出现第二次真实 render invalidation 时，才按需创建第二槽；之后两个
slot 可以轮换。

因此：

- 静止且内容不变时，首帧后没有额外 native frame；
- 短命 resize generation 不为永远不会使用的第二槽分配资源；
- 动画、相机拖动或 Play 等真实连续活动仍能自然形成双缓冲。

active、retiring、create reservation 与 quarantine 合计继续受四个 native
frame-resource lane 的硬上限约束。latest pending request 只有一个，不建立历史 Bounds
队列。

### 5. 资源采用非阻塞退役

compositor imported image 与两枚 semaphore 的 `DisposeAsync` 同时发起，并只发起一次。
失败资源进入 quarantine，禁止重用或重复释放。

consumer release 后，native packet 从 active ownership 移入 retirement queue：

```text
Active
  -> ConsumerReleased
  -> poll vkGetFenceStatus
       -> VK_NOT_READY: 保留并立即返回
       -> VK_SUCCESS: 完成 frame epoch，销毁并释放 lane
       -> error/device lost: quarantine，并保留到进程终止
```

create/render/stats/shutdown 进入 runtime 时先轮询 retirement queue，再判断可用 lane；
release 只查询本次释放的 packet 一次，未完成时移入 retirement queue 后立即返回。
交互 resize 路径禁止 `vkQueueWaitIdle`、`vkDeviceWaitIdle` 和无限 fence wait。
Vulkan 规定引用 fence 的 queue submission 完成前不得销毁该 fence，因此未完成 packet
必须连同 image、semaphore、command pool 和 fence 一起保留。

runtime owner 使用 process-lifetime storage。正常 idle shutdown 仍释放 producer/context；
未确认的 quarantine/retirement 不提前拆 device，由进程终止兜底。

### 6. 按需渲染不变

Scene View 默认 `OnDemand`：

- attach 首帧；
- 面板 Bounds/DPI；
- scene snapshot；
- 相机、选择、gizmo 或显式工具交互；
- 动画/Play 声明的连续活动。

没有上述 invalidation 时不产生 panel tick、retry timer 或 native frame。resize 的“持续
可见”来自真实 SizeChanged 事件、ready-frame 前进和 compositor 过渡显示，不来自固定
FPS 空转。

## Consequences

Positive：

- 拖动 Dock 分割条时，面板尺寸立即进入 latest state，不再多等一次 composition；
- producer 较慢时仍能显示单调前进的中间帧，不会稳定饥饿到停手；
- 大幅放大面板时，最后成功 surface 立即填满区域，exact-size 帧随后收敛；
- scene revision 在提交入口隔离旧内容，frame sequence 防止历史尺寸回跳；
- resize 热路径不因旧 packet release 无限等待 GPU；
- 双槽只为真实连续工作建立，保持按需渲染和有界资源。

Negative：

- exact-size 帧到达前会短暂重采样，快速改变宽高比时可见轻微瞬时变形；
- exact extent 的外部图像创建成本仍需通过 telemetry 量化；若它成为下一瓶颈，再独立
  引入 requested extent 与 allocation capacity/bucket，不把猜测性池策略塞进本修复；
- external-image composition 仍需要跨 UI、producer、GPU 与 compositor 的显式退役状态。

## Validation

自动测试必须证明：

- A→B→C 连续 resize 中，A/B/C 可按完成顺序单调呈现，C 最终收敛；
- C 先呈现后，A/B 不能回写；
- presentation accept 在 surface gate 释放前完成，不能依赖调用方 continuation 顺序；
- scene revision 变化后，尚未进入 surface update 的旧 revision completion 不能提交；
- Bounds observation 不由 composition callback 延迟，主入口来自
  `CompositionHost.SizeChanged`；
- host visual 始终使用当前 Bounds，不再比较 exact frame DIP size；
- 首帧成功不会自动 warm，第二次真实 invalidation 才形成第二槽；
- imported wrappers 并行释放且不重复；
- native release 不等待 fence，retirement poll 完成后 lane 可复用；
- 静止且无 pending request 时不产生 tick、timer 或 native frame。

手工 GPU 验证必须覆盖：

- 连续拖动 Scene View 与相邻面板之间的 Dock 分割条，包括大幅放大和缩小；
- 拖动期间画面持续覆盖面板并有可见更新，停止后 1–2 个 producer/composition 周期内
  收敛到最终清晰尺寸；
- 快速交替改变宽度和高度，不出现历史帧回跳、空白条或长时间冻结；
- 100%、125%、150%、175%、200% DPI；
- 0 size、minimize/restore、hide/show、Dock/float/reattach；
- Vulkan validation 下无 synchronization、lifetime、layout 和 handle 错误；
- resize 热路径无 queue/device idle wait、无限 fence wait，slot 和显存保持有界。

## References

- Avalonia `Compositor.RequestCompositionUpdate`：
  <https://api-docs.avaloniaui.net/docs/M_Avalonia_Rendering_Composition_Compositor_RequestCompositionUpdate>
- Avalonia Win32 `WM_SIZE` handling：
  <https://github.com/AvaloniaUI/Avalonia/blob/12.0.4/src/Windows/Avalonia.Win32/WindowImpl.AppWndProc.cs>
- Avalonia `WindowBase` resize/layout：
  <https://github.com/AvaloniaUI/Avalonia/blob/12.0.4/src/Avalonia.Controls/WindowBase.cs>
- Avalonia GPU interop sample：
  <https://github.com/AvaloniaUI/Avalonia/tree/12.0.4/samples/GpuInterop>
- Unreal Engine `FSceneViewport`：
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FSceneViewport>
- Godot `SubViewportContainer`：
  <https://github.com/godotengine/godot/blob/master/scene/gui/subviewport_container.cpp>
- O3DE `RenderViewportWidget`：
  <https://github.com/o3de/o3de/blob/development/Gems/Atom/Tools/AtomToolsFramework/Code/Source/Viewport/RenderViewportWidget.cpp>
- Unity `SceneViewState.alwaysRefresh`：
  <https://docs.unity3d.com/6000.0/Documentation/ScriptReference/SceneView.SceneViewState.html>
- Vulkan synchronization：
  <https://docs.vulkan.org/spec/latest/chapters/synchronization.html>
- Khronos swapchain recreation sample：
  <https://docs.vulkan.org/samples/latest/samples/api/swapchain_recreation/README.html>
