# Studio native boundary 审查

状态：Current audit（R0 Studio managed/deployment边已切离；独立C++ session/C ABI hard-cut待真实consumer Slice）

更新日期：2026-08-01

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

R0 Studio现已实际绕开：App/composition/publish均无viewport/frame-debug native consumer，managed viewport bridge、
deployment copy与phantom native receipt已删除，Editor Image拒绝`editor_native.dll`/`slang.dll`。以下P1条目审计的是
仍由独立C++ editor target/smoke持有的实现风险，不代表当前Studio产品能力，也不得用其smoke替代未来接入门禁。

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

### P1-8 viewport request 没有真实 scene/camera 数据

证据：

- request 只有 `hasScene + sceneRevision`；
- panel ID/kind 在 native bridge 硬编码；
- shared viewport producer 使用默认相机与固定轴。

revision 不是 render data。修复要求：

- request 携 immutable `RenderSceneSnapshotHandle` 与 camera/view packet；
- request/result 同时携 session/project/document/viewport generation、frame sequence 和 revision；
- mutable `World*` 不穿过 renderer/thread boundary；
- 无真实 snapshot 时呈现明确 empty/unavailable，不画 production fixture。

### P1-9 scene native DLL 不是当前 Studio runtime dependency

证据：

- `packages/scene-core/CMakeLists.txt:37-63` 生成 `asharia_scene_native`；
- `SceneNativeLibraryApi.cs:105-165` import `asharia_scene_native`；
- root `Editor.csproj` 当前不复制任何native DLL，也不引用`EngineBridge`；
- production 暂无 `SceneWorld.Create()` consumer，所以问题尚被 tests 掩盖。

因此R0不把`asharia_scene_native`加入发行物。第一处real SceneDocument Slice若选择该adapter，必须先建立统一native
runtime manifest、build/publish validation与typed binding availability result；在此前把DLL“补齐”只会制造新phantom dependency。

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

### P2-2 Project IO 与 renderer/Vulkan DLL 绑定（R0已删除）

R0已删除无managed caller的`editor_project_*` exports/self-smoke及`editor-native -> project-core-io`边，纯Project
IO不再进入renderer/Vulkan DLL。`asharia-editor`资产目录仍是真实Project IO consumer并继续直接依赖
`project-core-io`；package-owned smoke而非已删adapter负责descriptor合同证据。未来只有真实Studio ProjectSession
consumer出现且managed IO不能满足需求时，才重新评估窄native adapter。

### P2-3 Project result 丢失完整 descriptor（随旧adapter撤销）

旧native result/managed snapshot只保留root/name/id的双truth风险已随整条无consumer链删除。未来真实
ProjectSession必须直接拥有完整canonical descriptor或immutable descriptor identity/generation，不恢复旧result shape。

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
4. **Viewport slice**：render snapshot + camera -> bounded frame lease -> Avalonia complete；验证 1000 次
   create/close/reopen、query-vs-stop stress、resize/DPI/dock、consumer abandon、device lost、handle exact close。
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
