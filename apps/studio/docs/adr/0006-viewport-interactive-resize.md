# ADR-0006：视口交互 Resize 采用最新请求合并与代际提交

状态：Accepted

日期：2026-07-30

## Context

Studio 的嵌入式 Viewport 由 native Vulkan renderer 生成外部图像，再交给 Avalonia
compositor 显示。布局、native renderer 和 compositor 属于三个不同进度域：

- Avalonia Bounds 可以在一次拖拽中连续变化；
- native 图像创建、命令录制和队列提交可能晚于当前布局；
- compositor 仍可能持有上一帧图像。

当前 Windows Scene View 原型已经证明共享图像路径可行，但 resize 尚有四个结构性问题：

- native 创建、录制和提交由 UI frame callback 同步触发，可能阻塞布局与 Dock 拖拽；
- 请求没有 `SurfaceGeneration` 和 `FrameSequence`，旧尺寸完成结果可以覆盖新布局；
- logical size 由向上取整后的 pixel extent 反推，分数 DPI 下会出现亚像素偏差；
- presenter、lifecycle 和 native runtime 分别限制在途数量，却没有唯一的 latest pending
  request。

这不是增加毫秒防抖可以修复的问题。必须先明确尺寸、线程、帧槽和异步完成的所有权。

## Reference analysis

### 采用

- Unreal Engine 的 `FSceneViewport` 区分 UI resize 状态、强制 viewport size、buffered
  frames、game/render-thread buffer index 和显式 RHI 更新点。Studio 同样分离 UI 几何、
  请求的 render extent 与已经显示的 extent，不在 Bounds 回调中同步重建 GPU 资源。
- Blender 在 viewport draw context 忙时跳过普通绘制并标记稍后更新，只让必须立即得到
  结果的查询阻塞。Studio 将 `Busy`/`Unavailable` 视为正常 backpressure，保留最新请求并
  自动重试。
- O3DE 的 frame-in-flight 上限和延迟释放队列表明，旧 GPU 资源必须在显式完成边界后
  退役。Studio 同时等待 producer fence 与 compositor release。
- Godot 的容器 resize 直接提交最新尺寸，并快速跳过完全相同的尺寸。Studio也不把固定
  debounce 或“两次相同尺寸”当作正确性条件。
- Avalonia 的 GPU interop sample 在同一个 composition update 中使用同一份 Bounds
  snapshot 计算 pixel extent、更新 surface 和 visual size；`SwapchainBase` 明确保留多个
  同尺寸 image，避免单 image 造成 UI lockup。Studio 保留两个持久 present slot。
- Unity Scene View 默认只在场景或编辑状态变化时重绘；固定间隔刷新由
  `SceneViewState.alwaysRefresh` 显式开启。Studio 同样默认按需渲染，只有动画或
  Play Session 等调用方明确声明后才进入连续刷新。

### 不采用

- 不采用 Godot stretch 模式把旧 viewport texture 非等比拉伸到当前容器；
- 不采用 O3DE native swapchain resize 路径中的即时 GPU pipeline flush、无限 acquire
  wait 或旧资源销毁前 `WaitIdle`；
- 不推测或照搬 Unreal Engine 未公开的 debounce 时间和具体 buffered-frame 数量；
- 不为每个 Bounds 事件创建一份 GPU generation；
- 不用固定 30 FPS panel tick 轮询 Scene View 场景变化；
- 不把普通 backpressure 写入 Problems 或永久错误状态。

### 本地适配

成熟引擎通常直接拥有窗口或 render target；Studio 还跨越 Avalonia external-image
composition。新尺寸帧未到达时，Scene View 保留最后成功帧，保持原始 logical size，
只按当前 Bounds 居中并裁剪或露出中性背景。它不缩放，因此不会产生相机缩放、网格变形
或分数 DPI 重采样。新帧到达后再原子切换到精确尺寸。

## Decision

### 1. 一个不可变的尺寸快照

一次 Bounds/DPI 观察必须同时捕获：

```text
ViewportFrameRequest
    DisplaySizeDip
    RenderExtentPixels
    RenderScale
    SurfaceGeneration
    FrameSequence
    SceneRevision
```

- `DisplaySizeDip` 直接来自当次 `CompositionHost.Bounds.Size`，是显示尺寸唯一来源；
- `RenderExtentPixels` 由同一份 `DisplaySizeDip` 和 `RenderScale` 派生；
- 禁止用 `RenderExtentPixels / RenderScale` 反推 `DisplaySizeDip`；
- pixel extent、render scale、attach 或 device epoch 改变时递增
  `SurfaceGeneration`；
- 同一 generation 的每次 render attempt 递增 `FrameSequence`。

### 2. 最新请求覆盖旧请求

Bounds、DPI、scene revision 或显式 repaint 只标记一次待处理失效，并合并到下一次
composition update。每个合成周期至多采样一次最新 Bounds、render scale 与 scene revision，
再覆盖 presentation 的 `LatestPendingRequest` 并唤醒串行 producer；不为每个 UI 事件直接
启动 native，也不建立无界请求队列。

新请求还会取消仍停留在 producer gate 前、尚未开始 native 调用的旧工作。取消只作用于
native admission：已经开始 native create/render、已经提交 GPU 或已经交给 compositor
的工作不可取消，必须走现有 completion 与代际退役。

```text
Ready
  └─ request
       → Pending(latest request)
       → TryProduce
            ├─ Busy
            │    → 保留 Pending，slot 释放后重试
            └─ Submitted
                 ├─ stale
                 │    → Draining，只完成资源释放
                 └─ current
                      → Presenting
                      → 原子提交 image + exact visual geometry
                      → Ready
```

completion 只有同时匹配以下值才可以更新 composition surface 或 ViewModel：

```text
EngineEpoch
ViewportId
SurfaceGeneration
FrameSequence
```

旧 completion 不能回写 UI；它只负责把自己的 slot 安全退役。已经提交成功的
`FrameSequence` 必须单调递增。

### 3. UI 与 native producer 分离

- UI/compositor dispatcher 独占 Bounds 观察、composition visual/surface、external
  object import/dispose、`RequestCompositionUpdate` 和
  `UpdateWithSemaphoresAsync`；
- UI 失效通过单一 queued composition update 合并；回调执行时才捕获最新观察，Bounds
  事件本身不直接启动 native producer；
- 一个串行 producer worker 执行 native slot create/render、RenderGraph command
  recording 和 queue submit；
- UI dispatcher 不等待 Vulkan fence、queue idle 或 device idle；
- worker 不直接修改 Avalonia composition object 或 ViewModel；
- worker 每次取任务前重新读取最新 request；一个 slot 完成或被 compositor 归还时主动
  唤醒重试，不依赖固定帧率 tick 碰巧到达；
- Scene View 默认不加入 panel frame scheduler。初始帧、Bounds/DPI、scene snapshot
  change 和显式相机/工具交互触发按需请求；动画预览必须显式选择连续策略并在结束后退出。
- 公共 `ViewportScheduler` 的交互 burst 结束后回到 dirty-only，不生成定时 idle 帧；
  `VisibleExposed` 必须来自真实 expose/resize 失效。

当前切片只建立 Scene View 私有的 presentation session，不提前引入尚无第二个调用者的
公共 `ViewportService`。

### 4. 持久双槽与有界退役

每个已分配 extent 使用两个持久 slot：

```text
Free
  → NativeRendering
  → ReadyToPresent
  → CompositorOwned
  → Free

任意状态 -- generation 过期/detach --> Retiring --> Retired
```

- 当前 generation 的首个成功帧会自动排入一次 slot warmup：先建立第二个同尺寸 slot，
  再进入静止按需状态，不依赖下一次外部 invalidation 才形成双槽；
- 两个 slot 都忙时返回 `Busy`，不等待；
- active present chain 只保存当前 generation，最多两个 slot；
- 过期 slot 一旦完成在途工作便移交独立 retirement backlog，不再占 active chain 或
  per-generation 配额；
- active、retiring、create reservation 与 quarantine 合计仍受四个 native frame-resource
  lane 的硬上限约束；backlog 可以包含多个历史 generation，但不能无界增长；
- 新 desired generation 在总容量已满时仍只保存在 `LatestPendingRequest`；retirement
  completion 释放容量后立即唤醒；
- slot extent 创建后不可原地改变；
- image、imported image 和两枚 external semaphore 跨帧复用；
- slot 只有在 producer fence 完成且 compositor present/release 完成后才可复用或销毁；
- 交互 resize 路径禁止 `vkQueueWaitIdle`、`vkDeviceWaitIdle` 和无限 fence wait。必要的最终
  阻塞清理只允许发生在后台 shutdown/drain 边界。

### 5. Scene View 的过渡显示

Bounds 改变后，host 立即用最后一次成功帧更新临时显示几何：

- 保留该帧自己的 `DisplaySizeDip`，不改变宽高比；
- 以当前 Bounds 中心对齐；
- 缩小时从四周等量裁剪，放大时从四周露出中性背景；
- `ClipToBounds` 始终开启；
- 不缩放旧帧，不显示历史中间尺寸；
- 当前 generation 的首帧准备好时，在同一个 composition update 中提交 external image、
  设置精确 visual size、清除临时 offset。

无已提交帧时只显示中性背景。Scene View 不显示伪造 demo frame。

### 6. Renderer frame resources

native present slot 的 command pool、command buffer 和 fence 由 slot 独占。renderer 的
descriptor、upload buffer 等 per-frame 资源必须通过显式 `FrameResourceIndex` 或等价的
frame-resource context 选择，并由相同 fence 保护。

禁止用“把全局 cursor 设置为 slot 下标”的方式模拟 lane；一帧多次获取同类资源时会与
相邻 slot 重叠。若实现过程中出现此类原型，必须删除或替换为有明确容量和所有权的
frame-resource set。

### 7. 错误语义

| 结果 | 行为 |
| --- | --- |
| `Busy` / `Unavailable` | 正常背压；保留 latest request，由资源 completion 或下一次 compositor cadence 重试 |
| stale generation/sequence | 不提交；安全退役 |
| import failure | 退役该 slot；记录可恢复故障并尝试重建 generation |
| device mismatch / unsupported ABI | presentation 进入 Unsupported |
| device lost | 提升 engine epoch，停止新帧并进入恢复流程 |

上一帧的错误 snapshot 不能作为永久兼容性门禁。capability probe 只在 attach、compositor
变化或 device recovery 时执行，不进入 resize 热路径。若 probe 成功时 Bounds 尚无有效
extent，presentation 缓存 capability，并在首个有效 Bounds 到达时只重试 extent-dependent
配置；Unsupported/失败不会因 resize 重复 probe。

## Consequences

Positive：

- UI 拖拽不再同步承担 Vulkan 录制和提交；
- 历史异步帧无法让画面从新尺寸跳回旧尺寸；
- 旧帧保持像素几何和相机中心，不发生非等比拉伸；
- active 双槽与退役 backlog 分开计数，总 native 资源仍有硬上限；
- 静止且场景不变时不再产生 native frame；
- 同一 composition 周期内的多次 Bounds 变化只产生一个最新尺寸观察；
- 尚未进入 native 的旧尺寸工作不会在 gate 队列中阻塞 latest resize；
- 分数 DPI 的 logical/pixel size 不再互相反推；
- 设计可继续提升为多 Viewport presentation service，但当前实现范围保持小。

Negative：

- 新 extent 首帧到达前，边缘可能短暂露出中性背景；
- native producer 与 compositor dispatcher 之间需要一次明确的线程切换；
- detach、import failure 和 device recovery 必须覆盖双边完成的 drain；
- 现有 presenter/lifecycle 与 renderer cursor 原型需要重做，不能只增加 debounce。

## Implementation order

1. 用一个 Scene View presentation session 合并 presenter 与 lifecycle 的 pending、
   generation、backpressure 和 drain 状态。
2. 先写纯状态测试，覆盖 A→B→C stale completion、latest retry、双槽 warmup、
   active/retirement 分离和四 lane 总上限。
3. 让 Bounds/DPI 产生不可变 request，并实现 host 的居中原尺寸过渡。
4. 把 native create/render/submit 移到串行 worker；composition import/commit/dispose
   保留在 UI dispatcher。
5. 建立真实双槽，修复 native/renderer frame-resource ownership。
6. 补 native reusable-slot smoke 与实际交互 resize 验证。

## Validation

自动测试必须证明：

- A→B→C 连续 resize 后，A/B completion 不能更新 surface 或 ViewModel；
- `DisplaySizeDip` 等于捕获的原始 Bounds，不由 pixel extent 反算；
- 同一 extent 的首个成功帧会自动 warm slot 1，完成后静止；
- 两个 slot 都忙时 latest request 保留，任一 slot 释放后自动重试；
- retiring slot 不占 active-chain 配额，active + retiring + reservation + quarantine
  总数始终小于等于四；
- 同一 composition 周期的多个 Bounds 事件只启动一次 latest producer request；
- 静止且无 pending request 时不产生 panel tick、retry timer 或 native frame；
- detach/import failure/device lost 都不会泄漏 imported wrapper、handle 或 native slot；
- renderer frame resources 在两个同时在途 slot 间不重叠；
- normal backpressure 不产生 warning。

手工与 GPU 验证必须覆盖：

- 以 30–60 ms 间隔快速交替拖动宽度和高度；
- 停止拖动后 1–2 个 producer/composition 周期内收敛到最终尺寸；
- 使用圆形、正方形网格和中心十字检查无拉伸、无中心跳动和无历史尺寸回跳；
- 100%、125%、150%、175%、200% DPI；
- 0 size、minimize/restore、hide/show、Dock/float/reattach；
- validation layers 下无同步、lifetime、layout 和 handle 错误；
- resize 热路径没有 queue/device idle wait，slot 总数和显存稳定。

## References

- Unreal Engine `FSceneViewport`：
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FSceneViewport>
- Unreal Engine `FSlateRenderer`：
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/SlateCore/FSlateRenderer>
- Unreal Engine `FSlateThrottleManager`：
  <https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/SlateCore/FSlateThrottleManager>
- Blender viewport busy handling：
  <https://github.com/blender/blender/commit/cc6db8921b>
- Godot `SubViewportContainer`：
  <https://github.com/godotengine/godot/blob/master/scene/gui/subviewport_container.cpp>
- O3DE `RenderViewportWidget`：
  <https://github.com/o3de/o3de/blob/development/Gems/Atom/Tools/AtomToolsFramework/Code/Source/Viewport/RenderViewportWidget.cpp>
- O3DE `SwapChain` resize contract：
  <https://github.com/o3de/o3de/blob/development/Gems/Atom/RHI/Code/Include/Atom/RHI/SwapChain.h>
- O3DE Vulkan `ReleaseQueue`：
  <https://github.com/o3de/o3de/blob/development/Gems/Atom/RHI/Vulkan/Code/Source/RHI/ReleaseQueue.h>
- Avalonia `SwapchainBase`：
  <https://github.com/AvaloniaUI/Avalonia/blob/12.0.4/src/Avalonia.Base/Rendering/SwapchainBase.cs>
- Avalonia GPU interop sample：
  <https://github.com/AvaloniaUI/Avalonia/tree/12.0.4/samples/GpuInterop>
- Unity Scene View 默认按需重绘说明：
  <https://issuetracker.unity3d.com/issues/mesh-disappears-when-drawmesh-is-called-from-the-editorwindow>
- Unity `SceneViewState.alwaysRefresh`：
  <https://docs.unity3d.com/6000.0/Documentation/ScriptReference/SceneView.SceneViewState.html>
- Unity C# reference `SceneView.UpdateAnimatedMaterials`：
  <https://github.com/Unity-Technologies/UnityCsReference/blob/master/Editor/Mono/SceneView/SceneView.cs>
- Vulkan synchronization：
  <https://docs.vulkan.org/spec/latest/chapters/synchronization.html>
- Vulkan external memory and synchronization：
  <https://docs.vulkan.org/guide/latest/extensions/external.html>
