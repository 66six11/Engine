# Studio native boundary 审查

状态：Current audit（SceneDocument ABI v3、catalog query ABI v1 consumer 与 V11 async viewport stream 已建立；device epoch/recovery 仍开放）

更新日期：2026-09-05

目标架构和实施顺序见 [Studio 前端硬切架构](studio-frontend-hard-cut.md)。本文只记录
`apps/studio` 与 `apps/editor`、`packages/scene-core`、`packages/editor-content` 之间 native boundaries 的当前事实、
触发条件、风险和新合同门禁。

## 1. 结论

问题是系统性的，主因不是当前 Vulkan semaphore handshake，而是：

```text
process singleton
+ managed/native 多套 static state
+ 可复制的裸地址 ownership token
+ 无 session/document/device generation
+ C ABI failure/size/ownership contract 不完整
```

V11 ABI 现在承担 production presentation consumer：显式 stream handle、immutable latest request、bounded full-slot
lease、异步 ready acquire 与 close/poll/destroy，并携带 authored mesh snapshots、per-view raster mode、Scene mesh receipt
与 optional discriminated Transform Gizmo packet；V11 Gizmo payload 增加 normalized object rotation，支持 renderer-owned
local-axis Scale handles 而不引入 scene/runtime pointer。
V1–V10 stream exports 均不导出，不提供 managed 或 deployment fallback；历史 runtime-stats diagnostics 版本链不是 stream
compatibility surface。
后续多 Viewport fairness 与 device recovery 仍需引入 device epoch 和统一 recovery contract。

R0 Studio 删除了旧 App/composition/publish viewport consumer 与 deployment copy。#359 重建 UI-neutral
`ViewportSession`；#361 已让 App/Shell/Release publish 成为真实 consumer，
部署 `editor_native.dll` 与 renderer-basic shader closure，并通过专用 `ViewportCompositionControl` 和
process-owned `ViewportPresentationLifetime` 完成 Avalonia import/presentation/drain。native
`EditorSharedViewportRuntime` 现在拥有一条 process-level RenderThread 和 V11 bounded latest-wins scheduler；每个 stream
最多一个 executing、一个 pending latest、一个 ready frame 与三个持久 full slots。这关闭了“同步 caller 等帧”、
“每帧重建 presentation resource”和“caller 线程直接拥有 Vulkan”三个旧事实，但尚未解决 process singleton、
device epoch/recovery 或跨 stream weighted fairness。

普通 Studio build 现在也在实际部署边界 fail closed。root `Editor.csproj` 仍只消费
`build/cmake/$(StudioNativeBuildPreset)`，不会运行 Conan/CMake 或自动构建 `editor-native`；native ABI 或 checkout 变化后必须先
native-first 重建。随后 `editor_native.dll` 以 `CopyToOutputDirectory="Always"` 和
`CopyToPublishDirectory="Always"` 覆盖 managed output，避免 `PreserveNewest` 让历史 sibling 留在 `TargetDir`。
`ValidateStudioViewportNativeRuntimeContract` 在非 design-time `Build` 完成后执行最终
`$(TargetDir)\Editor.exe --verify-native-contract`；`Program` 将该窄入口交给 EngineBridge 内部的
`ViewportNativeRuntimeContract`，后者只加载并检查最终 `$(TargetDir)\editor_native.dll` 的 exports，不启动 Avalonia 或
Viewport runtime。

当前普通 build 与 Release Editor Image 共同要求以下 V11 production exports：

```text
editor_viewport_query_composition_compatibility
editor_viewport_release_compatibility_result
editor_viewport_open_stream_v11
editor_viewport_submit_latest_v11
editor_viewport_try_take_ready_v11
editor_viewport_complete_frame_v11
editor_viewport_release_slot_import_v11
editor_viewport_close_stream_v11
editor_viewport_poll_stream_v11
editor_viewport_destroy_stream_v11
editor_viewport_shutdown
```

并共同拒绝以下 legacy V1--V9 entry-point set：

```text
editor_viewport_acquire_present_packet
editor_viewport_release_present_packet
editor_viewport_acquire_present_packet_v2
editor_viewport_create_present_slot_v3
editor_viewport_render_present_slot_v3
editor_viewport_create_present_slot_v4
editor_viewport_open_stream_v5
editor_viewport_submit_latest_v5
editor_viewport_try_take_ready_v5
editor_viewport_complete_frame_v5
editor_viewport_release_slot_import_v5
editor_viewport_close_stream_v5
editor_viewport_poll_stream_v5
editor_viewport_destroy_stream_v5
editor_viewport_open_stream_v6
editor_viewport_submit_latest_v6
editor_viewport_try_take_ready_v6
editor_viewport_complete_frame_v6
editor_viewport_release_slot_import_v6
editor_viewport_close_stream_v6
editor_viewport_poll_stream_v6
editor_viewport_destroy_stream_v6
editor_viewport_open_stream_v7
editor_viewport_submit_latest_v7
editor_viewport_try_take_ready_v7
editor_viewport_complete_frame_v7
editor_viewport_release_slot_import_v7
editor_viewport_close_stream_v7
editor_viewport_poll_stream_v7
editor_viewport_destroy_stream_v7
editor_viewport_open_stream_v8
editor_viewport_submit_latest_v8
editor_viewport_try_take_ready_v8
editor_viewport_complete_frame_v8
editor_viewport_release_slot_import_v8
editor_viewport_close_stream_v8
editor_viewport_poll_stream_v8
editor_viewport_destroy_stream_v8
editor_viewport_open_stream_v9
editor_viewport_submit_latest_v9
editor_viewport_try_take_ready_v9
editor_viewport_complete_frame_v9
editor_viewport_release_slot_import_v9
editor_viewport_close_stream_v9
editor_viewport_poll_stream_v9
editor_viewport_destroy_stream_v9
```

这不是兼容探测或 fallback：任一 required export 缺失、任一 legacy export 存在、DLL 无法加载或架构不匹配，都使普通 build
失败。`msvc-debug-tests` 可以额外导出 `editor_viewport_open_stream_v11_for_test`，只用于显式 GPU/fault-injection 验收；普通本地
admission 允许这个当前版本的 test-only 扩展，但不把它当 production dependency。发行资格由
`StudioEditorImageProducer` 对全新 publish tree 执行静态 PE identity、required/forbidden exports、固定位置与 closed-tree 检查，且明确
拒绝该 test-only export。两道门禁不能互相替代。

#385 另以独立 `asharia_editor_content_native.dll` 接入只读 project asset catalog query；它不进入
`editor_native.dll`/Vulkan closure。ABI v1 使用 C11 header、fixed-width POD、caller-owned response buffer、typed status、
catch-all exception containment、native/managed size-offset tests 与 strict closed JSON parser。默认 10,000 files、8 GiB
source bytes、10,000 diagnostics、16 MiB JSON 和 64 KiB string/message；超限不返回 partial ABI payload。它没有 native
session handle，因为调用不产生需跨 call 持有的 native object；Application generation 只解决异步发布 supersession，不冒充
native cancellation。设计和拒绝项见 [ADR-0014](../adr/0014-catalog-backed-resource-browser.md)。

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

- `EditorSharedViewportRuntime::instance()` 仍是 process-lifetime singleton；
- 第一次 device/render/release 请求可启动唯一 RenderThread，`editor_viewport_shutdown()` 使 runtime 进入
  永久 Draining/Stopped/Faulted 路径，不能在同进程重新创建；
- Vulkan context、producer、outstanding/retiring packet 与 shutdown 析构已收口到 RenderThread，但 ABI 仍没有
  explicit runtime/session handle 或 device epoch；
- managed `ViewportPresentationLifetime` 和 `ViewportRuntimeBridge` 现在负责 production drain/shutdown 顺序，
  但它们不把 process singleton 变成可重启 native session。

影响：

- Project close/reopen 只能在不停止 process-level native runtime 的前提下重建 logical
  `ViewportSession`；调用 native shutdown 后的 device recovery 或 runtime restart 仍要求重启进程；
- 多 logical session 可共享当前 runtime，但没有显式 native session lifetime、generation 或公平调度合同；
- process shutdown owner 已集中到 `StudioCompositionSession`，但 native session timeout/recovery 与 quarantine
  receipt 仍没有统一合同；
- terminal packet 可能只能留到进程退出。

修复要求：

```text
EditorNativeSession:
Created -> Running -> Stopping -> Stopped
                         \-> Faulted
```

Stopping 期间 release/ack 仍有效；只有 `Stopped` 才能 destroy。每次 create 有新的 session/device epoch。
process-exit quarantine 只是可诊断终极回退。

### P1-7 `DeviceLost` ABI 值尚未由 runtime 产生

证据：

- viewport ABI 定义 `DeviceLost`；
- runtime error kind 已有 `Unavailable`/`RenderFailed`/`Backpressure`，但仍没有 `DeviceLost`；
- `mapRenderFrameStatus(...)` 只能映射上述三类，Vulkan failure 仍归约为 generic `RenderFailed`。

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

#359 后续由 V11 保留的当前事实：request 携 session/target/revision/sequence、真实 camera snapshot、view-local FOV axis 与最多 256 个来自
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

#385 不扩张 `asharia_project_native` descriptor ABI。需要 asset roots/cache/discovery 的 Resource Browser 通过专用
`editor-content` query 在 native 内重新读取同一 canonical descriptor，并只返回 catalog-facing immutable snapshot；C# 不解析、
修改或回写 descriptor。因此 project open identity 与 catalog snapshot 仍是两个窄 adapter，而不是把完整 descriptor 泄漏给 UI。

### P2-4 shared viewport mailbox 仍缺 per-session 公平调度

owner-thread Slice 已把 Vulkan context create、record/queue submit、fence polling、retirement 与析构移出
mutex。`queueMutex_` 只保护有界 render/control/release mailbox 与 lifecycle condition；单一
RenderThread 仍是 Vulkan queue 的唯一 consumer。V11 已按 stream 提供 pending-latest 覆盖和 ready-frame acquire；
当前缺口是多个 active stream 之间没有 deadline、weight 或 round-robin fairness 合同。未来多 Viewport 应在同一
owner thread 内增加有界 cross-stream admission/scheduling，不应直接并行操作 Vulkan queue。

### P2-5 retirement overflow 已改为 terminal quarantine，仍缺 session recovery

当前固定 retirement 容量溢出不再调用 `std::terminate`。owner thread 会记录错误、latch
`terminalQuarantine_`、关闭新 admission 并请求 shutdown；GPU completion 未知的 packet 与 Vulkan context
保留到进程退出，避免不安全析构。这个 emergency containment 符合现有 process-lifetime runtime，但还不是
session 级 recovery；在显式 session/device epoch 合同落地前，恢复仍需要重启 Studio 进程。

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

1. **Managed Project Shell（已建立）**：Project 使用 dedicated ABI v2，Scene 使用 package-owned Document ABI v3；Viewport 使用 V11 async
   stream，不保留旧 frame compatibility path。
2. **ABI foundation**：真正 C header、session/handle/error；C include、双端 size/offset、catch-all、
   bad-alloc/fault injection、duplicate/stale handle smoke。
3. **Scene slice（首个编辑闭环已建立）**：dedicated owner lane、generation-safe document handle、expected
   revision、immutable bulk snapshot 与 save/reopen 已接通；后续真实 undo 需求再引入 atomic mutation batch/change set/savepoint。
4. **Viewport slice（V11 当前基线）**：`SceneDocument -> UI-neutral session -> camera/FOV axis/bounded Transform proxies
   -> submit latest / take ready -> persistent full-slot lease -> Avalonia composition` 已形成可见闭环；native Vulkan owner
   thread、三槽上限、exact geometry generation、尺寸变化立即逐流 retire 与 exact close 已接入。下一次 contract Slice 集中在 device epoch/recovery、
   cross-stream fairness，并补多 viewport、device lost 与真实交互 resize profiling。
5. **Frame Debugger**：最后接入同一 session/render lane；capture identity 必须与 presented frame 一致。

关键验收项：

- 普通 Studio build 必须先重建所选 `StudioNativeBuildPreset` 的 `editor-native`，再由
  `ValidateStudioViewportNativeRuntimeContract` 对最终 `TargetDir` sibling 验证完整 V11 required exports 与 legacy V1--V9
  forbidden exports；不得自动构建 native、回退 V6 或从历史输出猜选 DLL；
- `ViewportNativeRuntimeContractTests` 必须逐项覆盖每个缺失 required export、每个出现的 legacy export 与 probe failure；
  Release Editor Image 继续以独立静态 PE inspector 复验同一 export policy；
- `ensureContext` pointer escape 有并发回归测试；
- injected DeviceLost 进入 Faulted 且 device epoch 变化；
- stop ticket 只在全部 frame lease terminal/quarantined 后变 Stopped；
- stale copied token 返回 `StaleGeneration`，不释放新对象；
- binding failure 统一为 typed `Unavailable`，UI command 不抛 `DllNotFoundException`；
- 全部 render smoke 继续满足“render loop 无 `vkDeviceWaitIdle`”。

## 8. 参考资料

- [Unreal Threaded Rendering](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine)
- [Unreal Asset Registry](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-registry-in-unreal-engine)
- [Unity Asset Database](https://docs.unity3d.com/Manual/AssetDatabase.html)
- [Godot Import process](https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/import_process.html)
- [O3DE Asset Cache](https://docs.o3de.org/docs/user-guide/assets/pipeline/asset-cache/)
- [Godot RenderingServer](https://docs.godotengine.org/en/stable/classes/class_renderingserver.html)
- [O3DE Atom Scene and RenderPipeline](https://docs.o3de.org/docs/atom-guide/dev-guide/rpi/working-with-scene-and-rendering-pipeline/)
- [Vulkan vkGetSemaphoreWin32HandleKHR](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSemaphoreWin32HandleKHR.html)
- [Avalonia 12 GpuInterop sample](https://github.com/AvaloniaUI/Avalonia/tree/12.0.4/samples/GpuInterop)
