# Studio native boundary 审查

状态：Current audit（SceneDocument consumer 已建立；#359 建立 viewport foundation，presentation/session teardown 风险仍开放）

更新日期：2026-08-04

目标架构和实施顺序见 [Studio 前端硬切架构](studio-frontend-hard-cut.md)。本文只记录
`apps/studio <-> apps/editor/packages/scene-core` 的当前事实、触发条件、风险和新合同门禁。

## 1. 结论

问题是系统性的，主因不是当前 Vulkan semaphore handshake，而是：

```text
process singleton
+ managed/native 多套 static state
+ 可复制的裸地址 ownership token
+ 无 session/document/device generation
+ C ABI failure/size/ownership contract 不完整
```

旧 ABI 不具备兼容保留价值。新前端可以先绕开它；恢复真实 Scene/Viewport 时应 hard-cut 到显式 session、
typed generational handle、immutable snapshot、bounded lease 和统一 error contract。

R0 Studio 删除了旧 App/composition/publish viewport consumer 与 deployment copy。#359 后 Application 已有新的
UI-neutral `ViewportSession`，EngineBridge 已有 V4 request / typed frame lease，但 App/Shell/publish 仍未消费或部署
`editor_native.dll`。以下 P1 条目同时审计新的 foundation 与仍由独立 C++ runtime 持有的风险；native smoke 只能证明
render/lease 边界，不能替代下一步 Avalonia presentation 接入门禁。

## 2. 阻断级问题

### P1-1 `ensureContext()` 指针逃锁，可与 shutdown 形成 UAF

处理状态：**2026-07-31 immediate fix 已完成。**

`ensureContext() -> VulkanContext*` 已替换为锁内复制的
`EditorSharedViewportDeviceSnapshot { vendorId, deviceId, identity }`；两个 compatibility ABI query
只消费值快照。MSVC/ClangCL `asharia-editor` build 与
`--smoke-editor-viewport-native` 已覆盖 snapshot roundtrip。显式 session/context lease 仍属于后续 ABI
hard-cut，本修复不代表 process singleton/shutdown 合同已完成。

原始证据：

- `apps/editor/src/editor_shared_viewport_runtime.cpp:75-87` 在 mutex 内取得
  `VulkanContext*`，返回后锁已释放；
- `apps/editor/src/native_bridge/viewport_native_api.cpp:351-362` 与 `:416-430` 随后解引用；
- `apps/editor/src/editor_shared_viewport_runtime.cpp:380-388,429-442` 的并发 shutdown 可以移走并销毁 context。

触发：

```text
thread A: ensureContext -> raw pointer -> unlock
thread B: shutdown -> takeContextForShutdownIfIdleLocked -> destroy
thread A: pointer->deviceInfo()
```

结果是 use-after-free。修复要求：

- 不允许 owner object pointer 逃出锁；
- compatibility query 使用锁内复制的 immutable device snapshot，或持有显式 session/context lease；
- 加入 query/acquire 与 stop/destroy 并发 stress 和 sanitizer gate。

### P1-2 Frame Debugger 与 Studio shared viewport render lane 断开

证据：

- `apps/editor/src/native_bridge/frame_debugger_native_api.cpp:20-28` 创建独立 function-static
  `EditorFrameDebugger` 与 mutex；
- capture/resume/select/snapshot export 在 `:113-183` 只访问该实例；
- shared viewport producer/runtime 中没有向它调用 `beginFrame/captureRecordedView/endSubmittedFrame`；
- ImGui editor 另有 `apps/editor/src/editor_app_services.hpp:35` 的 `frameDebugger` owner。

触发：Studio 点击 Capture。静态 ABI 实例只进入 requested 状态，却没有实际 viewport frame feed。

修复要求：

- 在接通前禁用 production Frame Debugger，不能展示伪 capture；
- `EditorNativeSession` 唯一拥有 debugger；
- 同一 render lane 记录并发布 frame；
- smoke 必须证明 capture 的 `frameSequence/documentRevision` 与实际呈现 viewport 一致，不只验证 JSON schema。

R0处理：production consumer、native deployment及无consumer public/managed snapshot/P/Invoke surface均已切除，
专属managed scheduler分支也已删除。C++ debugger保留在独立target中，未来接回前仍必须满足上述真实render-lane证据。

### P1-3 C++ 异常可逃出 P/Invoke C ABI

证据：

- viewport exports 位于 `apps/editor/src/native_bridge/viewport_native_api.cpp:397-724`；
- frame debugger exports 位于
  `apps/editor/src/native_bridge/frame_debugger_native_api.cpp:113-183`；
- export 没有 `noexcept` + catch-all wrapper，内部存在 string/container/allocation/Vulkan create 等可抛操作；
- 已删除的历史project native曾有catch-all；当前仓库内可采用的正例是scene C header的`noexcept`。未来新C ABI
  仍必须在真实consumer Slice重新证明catch-all与typed status，不能引用已删实现。

C++ 异常跨 C/P/Invoke boundary 会终止进程或产生未定义行为。修复要求：

- 所有 export 使用统一 `noexcept` wrapper；
- catch-all 映射为稳定 `Status + ErrorInfo`；
- `bad_alloc`、unknown exception 与 native fault injection 都不能越界；
- ErrorInfo 保留 domain、native code、operation 和必要 context。

### P1-4 ABI 输出容量未验证，版本协商可越界写

证据：

- `viewport_native_api.cpp:112-151` 的 `clearCompatibilityResult/clearPresentPacket`
  无条件写完整当前 struct；
- query/acquire 在 `:397-496` 只校验 request header，不校验 caller output capacity；
- 只有 render-v3 在 `:505-507` 校验 packet header；
- viewport/frame-debugger header 使用 C++ `<cstdint>`/typed enum，不能证明是 C compiler 可 include 的 C ABI；
- 缺少 native/managed size 与 offset 双端契约测试。

修复要求：

- header 使用 `<stdint.h>` 和真正 C-compatible declaration；
- output 显式 `outCapacity/writtenSize`，或 caller 初始化固定前缀 header 并先验证；
- 增加 C compiler include smoke、native `static_assert(sizeof/offsetof)`、managed
  `Marshal.SizeOf/OffsetOf` 对照；
- 老/小 buffer 返回 `UnsupportedAbi/BufferTooSmall`，不写出容量。

### P1-5 可复制的裸所有权令牌会 double free，并存在 ABA

证据：

- viewport packet/message、project result buffer、frame snapshot 都把 native pointer 放入按值 struct；
- release export 按值接收；managed 对应也是 freely-copyable value struct；
- `viewport_native_api.cpp:539-544` release packet 后直接释放 message；
- native runtime 的 outstanding set 能挡住一部分重复 packet release，但挡不住复制后的 message double free；
- allocator 重用相同地址时，旧 token 还可能被 `outstandingPackets_.contains(pointer)` 误认成新对象。

修复要求：

- ABI 外只暴露 opaque index + generation handle；
- `destroy/release(handle*)` 成功后把 caller token 置 invalid；
- managed 使用 sealed lease/`SafeHandle` 或等价 exact-once owner；
- duplicate/stale/ABA handle 返回 `StaleGeneration`，不能 free 当前对象；
- variable text 优先 caller-owned buffer 或 session diagnostics ring。

### P1-6 viewport 是不可重启的 process-global runtime

证据：

- `editor_shared_viewport_runtime.cpp:64-72` 有意 heap-leak singleton；
- `:380-388` 把 shutdown flag 永久置 true；
- `:429-442` 只有 outstanding/retiring 全空时才释放 context；
- 历史managed `ViewportNativePresentDrain` 与bridge已在R0删除；当前不可重启事实只属于独立C++ runtime；

影响：

- 同进程 close/reopen Project、device recovery 或第二个 session 无法可靠重建；
- shutdown owner 与 timeout/quarantine 分散；
- terminal packet 可能只能留到进程退出。

修复要求：

```text
EditorNativeSession:
Created -> Running -> Stopping -> Stopped
                         \-> Faulted
```

Stopping 期间 release/ack 仍有效；只有 `Stopped` 才能 destroy。每次 create 有新的 session/device epoch。
process-exit quarantine 只是可诊断终极回退。

### P1-7 `DeviceLost` 只存在于 enum，错误被抹平

证据：

- viewport ABI 定义 `DeviceLost`；
- runtime error kind 只有 render failed/backpressure；
- `viewport_native_api.cpp:380-387,529-533` 把 Vulkan failure 归约为 generic RenderFailed。

修复要求：

- 保留 `asharia::Error` 的 Vulkan domain 与原始 `VkResult`；
- `VK_ERROR_DEVICE_LOST` 使 session latch fault、`deviceEpoch++` 并停止旧 frame submit；
- reusable-slot failure 也返回 ErrorInfo；
- 注入 DeviceLost 的测试必须观察 `Faulted`、epoch change、旧 lease drain 与可控 recreate。

### P1-8 viewport request 没有 production render snapshot（camera/debug foundation 已由 #359 修复）

历史证据：

- request 只有 `hasScene + sceneRevision`；
- panel ID/kind 在 native bridge 硬编码；
- shared viewport producer 使用默认相机与固定轴。

#359 当前事实：V4 request 已携 session/target/revision/sequence、真实 camera snapshot 与最多 256 个来自
`SceneDocumentSnapshot` 的 `{objectId, Transform}` debug proxies；native producer 将 Scene/Game/Preview 映射到同一
RenderView path，Scene View 使用这些 Transform 生成调试轴，且双 session/slot smoke 验证 metadata 真被消费。

Transform proxies 足以证明当前编辑场景可驱动真实 GPU 画面，但不是 production mesh/material render data。因此剩余要求：

- request 携 immutable `RenderSceneSnapshotHandle` 与 camera/view packet；
- request/result 同时携 session/project/document/viewport generation、frame sequence 和 revision；
- mutable `World*` 不穿过 renderer/thread boundary；
- 无真实 snapshot 时呈现明确 empty/unavailable，不画 production fixture。

### P1-9 scene native DLL runtime dependency（已由 #353 关闭）

当前 production `ProjectSession` 在 descriptor 成功后通过 EngineBridge 打开默认 SceneDocument；EngineBridge 为每个连接
建立 dedicated owner lane，并通过新增 SceneDocument ABI 使用 generation-safe handle、expected revision、bulk snapshot、save
与 caller-owned buffer。root `Editor.csproj` 精确复制 `asharia_scene_native.dll`，distribution producer 验证 PE/DLL identity、
固定路径和七个 document exports，并拒绝同 stem 副产物或嵌套位置。

旧 World ABI 仍可用于 package smoke，但不是 writable document contract；Avalonia 不引用 native import，也不持有句柄。
设计与拒绝项见 [ADR-0009](../adr/0009-authoritative-scene-document.md)。

## 3. 重要设计缺口

### P2-1 Scene ABI 无法支撑 Document transaction

当前 `world_native_api.h:107-172` 明确：

- World 是 create-thread owner；
- Entity ID 只在 World 内有效；
- ABI v1 无 hierarchy/world transform；
- mutation 是逐 entity 同步 call；
- 没有 expected revision、validate-all/commit-all、change set、bulk snapshot 或 scene load/save。

这套 ABI 可保留为 package smoke，但不能成为 production editor write contract。首个 writable slice 需要：

- persistent `SceneObjectId` 与 transient `EntityId` 映射；
- engine-owner lane；
- revisioned packed SceneSnapshot；
- tagged POD `SceneMutationBatch(expectedRevision)`；
- authoritative committed revision、failed command index、forward/inverse change set；
- scene load/save 与 content identity。

### P2-2 Project IO 与 renderer/Vulkan DLL 绑定（R0已删除；专用adapter已接入）

R0已删除无managed caller的`editor_project_*` exports/self-smoke及`editor-native -> project-core-io`边，纯Project
IO不再进入renderer/Vulkan DLL。`asharia-editor`资产目录仍是真实Project IO consumer并继续直接依赖
`project-core-io`。2026-08-03 的真实 Studio ProjectSession consumer 通过 project-core package 自有的
`asharia-project-native` 专用 DLL 接入；它只依赖 `project-core-io`，不恢复 `editor-native -> project-core-io`
或 Vulkan/Slang 闭包。C ABI 使用 caller-owned bounded buffer、ABI header、typed status、C compiler include smoke、
native size/offset static assertions 与 managed layout tests；Release producer 复验 DLL name/PE/export identity。

### P2-3 Project result 丢失完整 descriptor（以canonical identity最小化）

旧native-owned result/managed snapshot的复制释放与双truth风险没有恢复。当前 v1 adapter 在 native
`openAshariaProject()` 完整读取并验证 descriptor 后，只向 Application 投影 immutable canonical identity
`root/name/projectId`；managed 侧不解析、不修改也不回写 descriptor。未来任一 consumer 需要 asset roots/cache/discovery
字段时，必须版本化扩展 immutable descriptor snapshot 或 generation，不得让 C# 拼装第二份 JSON truth。

### P2-4 shared viewport global mutex 是未来多视口瓶颈

当前全局 mutex 覆盖 record/queue submit。正确性上可以暂时接受，但多 viewport 应转为 bounded
MPSC intent queue + single render consumer，不要直接并行 Vulkan queue。

### P2-5 retirement overflow 不能 `std::terminate`

固定四 lane 是合理上限，但产品路径 overflow 应 latch session fatal fault、停止新 submit 并进入 recovery，
不能终止进程。

## 4. 已核对且当前不应误报

- Win32 external image/binary semaphore handshake 的 wait/signal 顺序未发现逻辑错误；
- native 在 consumer 完成后关闭 Vulkan 导出的 Win32 NT handle，符合该 handle 的所有权要求；
- managed 在 native packet release 前等待 imported object `DisposeAsync`，方向正确；
- render loop 未发现 `vkDeviceWaitIdle`；
- Bridge 作为 backend-specific adapter 可以链接 `renderer_basic_vulkan/rhi_vulkan`；
  未发现 `rhi_vulkan` 反向依赖 Editor 或 RenderGraph。

这些正确部分可以搬用，但不能因此忽略 session/ABI/handle lifetime 缺陷。

## 5. 最小新 native contract

### 5.1 Session

```text
ash_editor_session_create(CreateInfo, SessionHandle*) noexcept
ash_editor_session_request_stop(SessionHandle, StopTicket*) noexcept
ash_editor_session_poll_stop(SessionHandle, StopState*) noexcept
ash_editor_session_destroy(SessionHandle*) noexcept
```

Session 唯一拥有 Vulkan context、render producer、Frame Debugger、device epoch 与 render lane。

### 5.2 Scene

```text
scene_document_open(SessionHandle, OpenRequest, DocumentHandle*, SnapshotHandle*) noexcept
scene_apply_batch(DocumentHandle, MutationBatch, MutationReceipt*) noexcept
scene_snapshot_acquire(DocumentHandle, RevisionCursor, SnapshotHandle*, ByteSpan*) noexcept
scene_snapshot_release(SnapshotHandle*) noexcept
scene_document_save(DocumentHandle, SaveRequest, SaveReceipt*) noexcept
```

SceneSnapshot 使用 flat/SoA arrays + UTF-8 string table。MutationBatch 使用 tagged POD command array +
UTF-8 blob；不传 CLR object、delegate 或 mutable World pointer。

### 5.3 Viewport/frame lease

```text
viewport_create(SessionHandle, ViewportCreateInfo, ViewportHandle*) noexcept
viewport_submit(SessionHandle, ViewportHandle, FrameRequest,
                FrameLeaseHandle*, FrameLeaseDesc*) noexcept
frame_complete(SessionHandle, FrameLeaseHandle*, ConsumerCompletion) noexcept
viewport_destroy(SessionHandle, ViewportHandle*) noexcept
```

`FrameRequest` 至少包含：

```text
session/device epoch
ProjectSessionId / DocumentId
ViewportId + generation
request/frame sequence
document/render-snapshot revision
extent/flags
camera packet
overlay mask
RenderSceneSnapshotHandle
```

`FrameLeaseDesc` 明确 image/semaphore handle family、ownership、wait/signal value、format/color space 和 terminal
status。Win32 handle 必须标注 `BorrowedUntilComplete` 或 `Transferred`。

### 5.4 Error contract

所有 ABI 只使用 fixed-width POD：

```text
AbiHeader { version, structSize }
Status
ErrorInfo {
  domain, nativeCode, severity,
  operationId, messageId, requiredBytes
}
```

文本通过 caller-owned buffer/two-call copy 或固定有界 UTF-8，不返回可复制 malloc pointer。

## 6. Native 采用的结构与模式

| 结构/模式 | 用途 | 理由 |
| --- | --- | --- |
| RAII inside + opaque generational handle outside | Session/Document/Viewport/Frame | 跨语言 exact-once 与 stale/ABA detection |
| Single-owner render/engine lane + bounded queue | World/Vulkan mutation | 明确线程 owner，无 callback reentrancy |
| Immutable snapshot + revision | UI/render read | 读方不碰 mutable World，支持 optimistic conflict |
| Explicit state machine | Session/Viewport/FrameLease | stop/fault/recovery 可证明 |
| Lease | external image/semaphore | consumer lifetime 与 native resource lifetime绑定 |
| Flat/SoA snapshot | hierarchy/render scene | 减少 P/Invoke 与对象分配，连续迭代 |
| Tagged POD batch | scene mutation | ABI 稳定、一次 crossing、validate-all |
| `std::expected` inside + Status/ErrorInfo outside | failure | 保留 error/VkResult，异常不越界 |

## 7. 实施顺序与门禁

1. **Managed Project Shell**：绕开旧 viewport/scene ABI；旧 native 冻结，不扩展。
2. **ABI foundation**：真正 C header、session/handle/error；C include、双端 size/offset、catch-all、
   bad-alloc/fault injection、duplicate/stale handle smoke。
3. **Scene slice**：owner lane、mutation batch、immutable snapshot；验证 revision conflict、failure、undo/save。
4. **Viewport slice**：#359 已完成 `SceneDocument -> UI-neutral session -> camera/bounded Transform proxies ->
   typed frame lease -> native Scene/Game/Preview` foundation；下一 Slice 接 Avalonia complete/surface generation，再验证
   1000 次 create/close/reopen、query-vs-stop stress、resize/DPI/dock、consumer abandon、device lost、handle exact close。
5. **Frame Debugger**：最后接入同一 session/render lane；capture identity 必须与 presented frame 一致。

关键验收项：

- `ensureContext` pointer escape 有并发回归测试；
- injected DeviceLost 进入 Faulted 且 device epoch 变化；
- stop ticket 只在全部 frame lease terminal/quarantined 后变 Stopped；
- stale copied token 返回 `StaleGeneration`，不释放新对象；
- binding failure 统一为 typed `Unavailable`，UI command 不抛 `DllNotFoundException`；
- 全部 render smoke 继续满足“render loop 无 `vkDeviceWaitIdle`”。

## 8. 参考资料

- [Unreal Threaded Rendering](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine)
- [Godot RenderingServer](https://docs.godotengine.org/en/stable/classes/class_renderingserver.html)
- [O3DE Atom Scene and RenderPipeline](https://docs.o3de.org/docs/atom-guide/dev-guide/rpi/working-with-scene-and-rendering-pipeline/)
- [Vulkan vkGetSemaphoreWin32HandleKHR](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSemaphoreWin32HandleKHR.html)
- [Avalonia 12 GpuInterop sample](https://github.com/AvaloniaUI/Avalonia/tree/12.0.4/samples/GpuInterop)
