# 架构设计

本文记录当前已成立的代码事实。目标 Kernel、Host Runtime、Foundation Systems、scope/activation、Settings/Storage/
Tasks/memory/spatial 基线与实施门禁见 [foundation-framework.md](foundation-framework.md)；目标设计尚未落地前，不得把
该文档中的 planned targets/API 写成当前事实。

## 目标

Asharia Engine 当前目标仍是先做一个小而完整的 Vulkan renderer，用最少功能证明 RenderGraph
从声明、编译、同步到执行、present 的完整流程。第一个稳定 frame 比大而全的抽象更重要。

架构原则是 package-first，而不是 app-first。引擎不把所有功能塞进一个 monolithic application；
它提供一个小核心和一组可组合 package，让 sample app、runtime app、editor 和后续工具按需引入能力。

跨系统开发的通用架构思想、分层规范、数据合同和功能进入前检查见
[architecture-principles.md](architecture-principles.md)。当前真实调用链和包依赖以
[flow.md](flow.md) 为准；RenderGraph/RHI 的细边界以 [../rendergraph/rhi-boundary.md](../rendergraph/rhi-boundary.md)
为准。长期 managed plugin / script / Avalonia Studio 方向见
[managed-extension-model.md](managed-extension-model.md)，该 ADR 只锁定未来边界，不改变当前功能主线。

## 当前基线

以下结论必须和根 `CMakeLists.txt`、各 package `CMakeLists.txt`、`asharia.package.json` 及
[flow.md](flow.md) 保持同步：

- CMake target graph 仍是构建真相；manifest 是文档化边界和 future package registry 输入。
- `packages/rendergraph` public API 未暴露 Vulkan type；`asharia::rhi_vulkan` CMake target 只公开链接
  `asharia::core` 和 Vulkan/VMA 依赖；RenderGraph/Vulkan 翻译集中在 `asharia::rhi_vulkan_rendergraph`。
- `asharia::renderer_basic` 未暴露 Vulkan type，Vulkan 命令录制和资源绑定在
  `asharia::renderer_basic_vulkan`。
- `apps/editor` 当前是 host integration 和 smoke harness；panel 通过 backend-neutral request/result
  消费 viewport 服务，不直接创建 pipeline、descriptor 或 command buffer。viewport coordination 已按
  `panelId + EditorViewportKind` keyed slot 保存 request、texture result 和 diagnostics。
- 当前仍有临时实现形状：`renderer_basic_vulkan` 同时承载 sample renderer、RenderView/offscreen viewport、
  debug preview、world-grid/debug-line overlay draw 和 execution event；Frame Debug pass/event preview 会在
  replay graph 中把 debug image copy 放到选中 RenderView pass 之后；`recordViewFrame()` 已为 world grid 增加
  `builtin.render-view-world-grid` fullscreen overlay pass；存在 `BasicDebugWorldLine` 时才插入
  `builtin.render-view-overlay` pass，把 camera/frame/debug-line count 纳入 graph-visible typed params 并绘制可见
  line-list。

## 模块边界

- `engine/core`：日志、错误/result、版本，以及 bounded/atomic/staged file IO 与 cooperative
  exclusive-file-lock 等低层通用设施。不能依赖 Vulkan、GLFW、Slang、editor UI 或 asset importer。
- `engine/platform`：当前是预留 platform abstraction boundary target，依赖 `engine/core`；尚未导出公共
  header 或拥有具体 OS 集成。
- `packages/window-glfw`：GLFW window、输入轮询和 Vulkan surface 创建，依赖 `core` / `platform`，实际
  GLFW/Surface glue 仍归此 package。
- `packages/profiling`：后端无关 CPU scope、frame profile、counter 和 benchmark 输出；当前不依赖
  renderer、Vulkan 或 editor。
- `packages/schema`：稳定 type/field id、value kind 和 typed metadata。
- `packages/archive`：`ArchiveValue` 和 JSON IO facade；不把第三方 JSON 类型扩散到上层 API。
- `packages/cpp-binding`：C++ object/member 与 schema field 的读写绑定。
- `packages/persistence`：组合 schema、archive 和 binding，提供 save/load/default/migration。
- `packages/scene-core`：`asharia::scene_core` 提供 headless World、runtime `EntityId` 和 local `Transform` baseline，
  以及拥有 World、stable scene/object ID、revision/savepoint 的 `SceneDocument`。scene IO 通过 `archive` strict JSON
  facade 读写固定 schema，并以 sibling staging 保存；`asharia::scene_native` 同时保留 package-level World smoke ABI，
  并发布 production SceneDocument ABI：generation-safe opaque token、owner-thread 操作、expected revision、bulk snapshot、
  create/name/Transform/save 与 caller-owned UTF-8 buffer。它尚不提供 hierarchy/world Transform、component reflection、
  undo/redo、render extraction 或 Play World。
- `packages/project-core`：最小 Asharia project descriptor，当前只描述 project identity、asset source roots
  和 asset discovery ignore policy；不拥有 cook/package profiles、editor workspace 或 runtime state。
- `packages/asset-core`：asset GUID、type、handle/reference、metadata、product/cache/dependency/catalog
  的 CPU 数据模型；不拥有 GPU resource 或 editor UI。
- `packages/material-core`：material resource signature、descriptor contract、shader/signature compatibility
  validation 和 pipeline key hash 的 CPU 数据模型；不拥有 `.amat` IO、GPU resource、Vulkan pipeline/cache
  或 editor UI。
- `packages/reflection` / `packages/serialization`：过渡兼容 package，不再承载新 editor、script、asset 或
  migration 语义。
- `packages/rendergraph`：后端无关 graph model、resource/pass/slot/schema、command summary、编译结果、
  diagnostics、abstract image/buffer state 和 transient lifetime。这里不能出现 Vulkan layout、stage/access
  mask、command buffer 或 descriptor。
- `packages/rhi-vulkan`：Vulkan context、device/queue/swapchain、VMA-backed buffer/image、pipeline、
  frame loop、deferred deletion、debug label 和 timestamp 基础设施。
- `asharia::rhi_vulkan_rendergraph`：同一 package 内的适配 target，把 RenderGraph abstract state 翻译成
  Vulkan layout/stage/access/barrier。
- `asharia::renderer_basic`：后端无关 draw item、RenderGraph builtin schema 和 renderer-facing contract。
- `asharia::renderer_basic_vulkan`：basic renderer 的 graph 构建、Vulkan pass callback、descriptor/pipeline
  绑定、offscreen viewport、RenderView diagnostics 和 debug preview。
- `packages/shader-slang`：Slang 编译、SPIR-V validation、metadata 和 reflection JSON。
- `apps/sample-viewer`：sample host 和 runtime smoke harness。它当前可直接创建 Vulkan context/frame loop，
  这是 MVP 验证事实，不是长期 runtime 边界。
- `apps/editor`：Dear ImGui editor host。它组合 `window-glfw`、`rhi-vulkan`、`renderer_basic_vulkan` 和
  ImGui backend，拥有 shell、panel/action/event、viewport coordination、texture registry、Frame Debug 和
  editor smokes；未来 `packages/systems/editor` 内部 `editor_domain` 只能接收 backend-neutral editor state。
- `apps/studio`：Avalonia managed Studio shell。当前产品链是 `App/Shell -> Application ProjectSession -> EngineBridge
  project + scene adapters -> asharia_project_native + asharia_scene_native`。ProjectSession 只有在 canonical descriptor
  与默认 SceneDocument 都打开后才发布 Ready；EngineBridge 用 dedicated owner lane 封装 native document handle，Shell
  只发送 command 并投影 authoritative snapshot。当前 UI 提供单文档 Hierarchy、名称/local Transform Inspector、Create
  Entity、Save 与 dirty。首个可见 Scene View 已按 `StudioScenePanelView -> ViewportCompositionControl -> ViewportSession
  -> EngineBridge ViewportBridge V5 stream -> editor_native bounded scheduler -> process RenderThread -> renderer_basic_vulkan`
  接通；Release image 部署 `editor_native.dll` 与精确 shader closure。Studio 不录制 Vulkan command，也不拥有 native
  handle/GPU resource；完整 Dock、Asset Browser、undo/redo、Play Mode、第二 Viewport 与 viewport input 尚未接入。

  <details>
  <summary>Retired Studio Project Code / viewport 设计记录（非当前产品事实）</summary>

  以下段落保存被 R0 hard cut 删除或后置的旧 Project Code、generation 与 viewport 设计证据，不构成当前实现：

  `Asharia.Studio.Application.Bootstrap.Distribution` 只从外部 owner 已选择的 exact
  `EngineGenerationId` 与 generation root 复验 Editor Image inventory，并签发进程内可撤销 lease；它不负责
  generation selection、完整 Distribution health、repair/install/update 或项目 package graph。current Editor
  Image lease 可以继续投影发行版固定的 `managed/dotnet` host、SDK、hostfxr、runtime、reference pack 与
  Runtime/Editor contracts；Project Code 可从 current projection 进一步复验 exact dotnet-root closure、全部
  selected bytes、SDK/runtime metadata、CLR identities 和 contract/reference closure，签发 Windows x64 semantic
  build credential。Project Code 还能从 current credential 与 caller 已规范化的 project root/projectId 只快照
  exact `Editor/**/*.cs`，复制 source/Host contract bytes 并原子发布 deterministic implicit SDK workspace；
  workspace identity 不含 checkout/cache 绝对路径。isolated SDK build controller 把该 immutable workspace 复制到
  controller-owned 短路径，从 credential exact closure 物化 sealed dotnet execution mirror，以空白 allowlist 环境依次
  执行 SDK probe、explicit restore 和 `build --no-restore`，再只原子发布 implementation DLL、reference DLL、
  portable PDB 与 `.deps.json` 四类 raw output。source/credential/workspace/SDK mirror 漂移、超时、取消或
  supersession 都 fail closed。artifact inspector 只消费 current raw-output lease，使用 BCL
  `PEReader`/`MetadataReader` 无执行复验 implementation/reference identity、MVID/IL flags、credential
  reference closure、exact reference marker、PE-associated portable PDB/canonical documents 与严格
  single-project `.deps.json`，并签发不含绝对路径的 content-addressed metadata report。artifact publisher
  内部重新执行检查，再用 bounded BCL stream copy/hash、staged rehash、exact closed-tree verification 与一次
  directory rename，把四文件及 deterministic `artifact.json` 发布成跨物理根 identity/manifest 稳定的 immutable
  evidence。metadata report 显式绑定 exact Editor contract identity，使没有 Editor reference 的 moduleless
  assembly 仍保留 credential lineage。module indexer 在扫描前后复验 closed publication，并只用 BCL metadata
  对 implementation/reference assembly 的 exact `EditorModuleAttribute`、direct `EditorModule` type shape 与
  declaration surface 建立稳定、path-free、content-addressed in-memory index；空索引允许，但不证明 load
  eligibility。staging candidate admitter 只接受 publication receipt，内部重建 index，要求 non-empty，并在
  签发前再次复验 publication；candidate identity 仅绑定 publication/index identity，absolute root 只作为
  进程内 locator。后继 current check 会重新索引并对证 surface。该 receipt 不证明 managed reload eligibility；
  host policy selector 再只消费 current candidate，并把当前 external-build、缺少 unload evidence 的 v1
  确定性签发为 path-free `Pinned + RestartRequired` policy receipt；所有 activation/handover 组合都不能
  自动升级为 Collectible。pinned load-image builder 再只消费 current policy，在读前/读后复验它，把 exact
  implementation DLL 与 portable PDB 读入每文件不超过 256 MiB 的 owned bytes，并用 BCL PE metadata 拒绝
  global `<Module>` `.cctor`；path-free image identity 绑定 policy 与两文件 evidence，快照只提供不暴露底层
  buffer 的新只读流。pinned assembly loader 再以 loader-owned project reservation 串行首次 load，创建
  path-free、non-collectible custom ALC，并只从 implementation/PDB streams 加载 exact root assembly；
  same image 幂等复用，different image 或 ALC 创建后的失败要求重启。dependency hook 返回 `null` 以共享
  已验证的 Default Host/framework closure，不做 path/private/native probing。pinned module type resolver
  再只按 host 内嵌 exact index 对 root Assembly 做 case-sensitive type lookup，复核 exact Assembly/full name/
  direct public-sealed-non-generic `EditorModule` shape 与 public parameterless constructor presence，并返回绑定
  host/index 的 immutable Type receipt。pinned module constructor 再以显式 per-project reservation 按 index
  顺序调用这些 receipt 的 exact `ConstructorInfo.Invoke(null)`；same lineage 幂等复用同一 objects，failure
  保留 partial objects、禁止重试并要求重启。pinned module configurator 再为 exact objects 建立 builder，
  至多一次 Configure 并冻结只由 index 投影 metadata 的 immutable declarations；failure 同样保留 partial
  receipts、禁止重试并要求重启。definition set 再把 exact metadata/object/declaration 纯内存投影为
  static/dynamic 共用的 shared definitions，不反向持有 static factory registration。scope preparer 只在
  caller 显式提供 ProjectSession scope/host capabilities 后构建不可见、combined-validated candidate，不把
  persistent ProjectId 当 session identity。initial scope committer 只在 captured snapshot 仍有效且目标 scope
  为空时提交 exact candidate，并返回 compare-and-remove registration owner；stale、已有 scope、重复消费与
  successor replacement 均 fail closed。initial scope activator 再复核 runtime capability snapshot 与 Prepare
  时的 capability ID 集合完全一致，把 registration 一次性转交给独占异步 owner；`WaitingForCapability`/
  `Blocked` 是可持有 soft outcome，任一 `Faulted`、取消或 Host failure 都先释放 activation，再退役 exact
  registration。当前仍不创建正式 ProjectSession composition，不推进 contribution/current/active/LKG，
  也不支持 `.asmdef`/Package、replacement、catalog transaction 或前端接线。

  归档 `331824a3` 中剩余独有的一体化 Authoring host、catalog 与旧 generation publisher/contracts 不再是待恢复
  实现：其 build/publication/load/Configure/registry/activation 职责已由上述窄阶段替代，而 replacement、
  revision、catalog commit 与 collectible unload 仍需未来独立合同，所以这些文件只保留为历史设计参考。归档
  中旧 build-environment/workspace/build/artifact contract 变体及配套测试、独立设计稿也已由当前 typed receipts、
  real-chain tests 与 canonical architecture 文档替代。
  归档中的 provider/runtime/Scene 草案也不回灌；tests-only Application/public/Core scene provider/snapshot岛已删除。
  Runtime Scene value ABI、native Scene Core与managed EngineBridge仍是独立边界，不构成当时Studio只读Scene能力。
  Studio 在 Windows 上必须优先配置 `Win32RenderingMode.Vulkan`，再回退到 `AngleEgl` / `Software`，否则 Avalonia
  composition GPU interop 可能只暴露 D3D/ANGLE 共享纹理路径，无法进入 Vulkan opaque NT image/semaphore spike。

  </details>

## 所有权模型

- `VulkanContext` 拥有 instance、debug messenger、surface、physical device 选择结果、logical device、
  graphics queue 和 VMA allocator。
- `VulkanFrameLoop` 拥有 swapchain images/image views、frame command buffer、semaphore/fence、timestamp
  query pool、debug label functions、deferred deletion retirement、acquire/submit/present/recreate 顺序。
- `VulkanBuffer`、`VulkanImage`、`VulkanImageView`、`VulkanRenderTarget` 和 `VulkanSampler` 拥有具体
  Vulkan/VMA 资源；长期资源由 renderer/RHI owner 持有，再通过 binding/import 进入 graph。
- `RenderGraph` 拥有单帧声明；`RenderGraphCompileResult` 拥有单帧编译后的 pass order、dependencies、
  abstract transitions、transient allocations、final transitions 和 diagnostics 输入。
- `renderer_basic_vulkan` 拥有 sample renderer pipeline/descriptor/buffer state，并在 frame callback 内录制
  Vulkan commands；它不拥有 swapchain present。
- `apps/editor` 拥有 editor-only UI state、ImGui context/backend lifecycle、viewport texture descriptor
  registration 和 delayed retirement。Scene View panel 只提交 `EditorViewportRequest`；
  `EditorViewportCoordinator` 才把 keyed request 转换成 sampled RenderView target、keyed diagnostics snapshot
  和 ImGui texture publication。
- `Asharia.Studio.Application.Viewports.ViewportSession` 拥有 UI-neutral session/target/camera/sequence/invalidation
  状态；它发布 latest immutable request，document revision 在途推进时旧 completion 不成为 current。
- `Asharia.Studio.Presentation.Avalonia.Viewports.ViewportPresentationTransactionCoordinator` 以
  每个 participant 的 `SessionId + EndpointEpoch + TransactionId` 编排 Proposal→Preparing→Prepared→Validated→Published→Rendered→Retiring→
  Completed/Aborted/Quarantined。同一 Avalonia compositor scope 可 group all-or-nothing visible publish；跨 compositor 明确不原子。
- `EditorDockStagedGridSplitter`、`EditorDockSplitResizePolicy` 与 `EditorDockSplitResizeCoordinator` 只拥有 splitter drag
  proposal、definition min/max/layout rounding、requested/committed `GridLength` 与同步 layout probe；它们是 transaction 的 layout
  adapter，不拥有 endpoint surface/stream。这些 transient editor layout state 不进入 `SceneDocument` 或 runtime。
- `Asharia.Studio.Presentation.Avalonia.Viewports.ViewportCompositionControl` 是 endpoint owner，拥有 Avalonia compositor capability
  probe、external image/semaphore import、front/candidate drawing surfaces、prepared publish receipt、geometry/content gate、quarantine
  与面板 presentation state；它通过
  `ViewportSession`/EngineBridge 消费 frame lease，不拥有 Vulkan resource，也不把 native handle 交给 Shell/ViewModel。
- `Asharia.Studio.EngineBridge.Viewports.ViewportBridge` 是 managed V5 stream ABI 边界；它复制最多 256 个
  `{objectId, Transform}` debug proxies，异步 submit latest / take ready，并以
  `ViewportFrameLease.Release(NotSubmittedToConsumer | ConsumerAccessed)` exact-once 完成持久 slot 的本轮使用。
  raw external image/semaphore handles 只在 EngineBridge/Presentation handshake 内可见，Shell/ViewModel 与 Application
  不取得句柄；V1–V4 frame exports 与 managed fallback 均已删除。
- `Asharia.Studio.Presentation.Avalonia.Viewports.ViewportPresentationLifetime` 是 managed process composition 的
  admission/drain owner；`StudioCompositionSession` 关闭时先 stop-and-drain presentation，再 dispose Shell ViewModel，
  调用 native viewport shutdown，最后关闭 ProjectSession。
- `Asharia.Studio.Application` 的 Editor Image inventory lease 是只读 Application 层产品策略：实现使用 .NET BCL
  文件 API；Avalonia `IStorageProvider` 只拥有用户文件选择、bookmark 与平台权限 UI，native Core File IO 继续只服务
  C++ engine/runtime 的低层 IO 与事务。
- native `EditorSharedViewportRuntime` 是 process singleton，并拥有唯一 RenderThread、有界 V5 stream scheduler、
  Vulkan context/producer、external image/semaphore、RenderView recording 与 deferred GPU lifetime。每个 stream 最多
  一个 executing、一个 pending-latest、一个 ready frame 和三个持久 full slots；Scene/Game/Preview、camera、
  session/target/revision/sequence 与 bounded Transform proxies 映射到同一 renderer path。managed Studio 只能观察
  lease metadata 并通过 EngineBridge 完成本轮 slot，不能直接关闭、重用或延迟销毁 Vulkan resource。external image
  只有 producer fence 与已声明 consumer 的 consumer-done retirement fence 均完成后才能重用；attach 内 front surface 保持可见，
  transaction preparation 使用独立 candidate surface。same-compositor group switch batch `Rendered` 后才允许各 endpoint 退役
  replaced surface/stream；detach removal
  batch `Processed` 控制仍归 attach 所有的全部 front/candidate surface 安全析构。

销毁顺序：

1. shutdown、swapchain recreate 或已注释 debug 路径中等待 GPU/queue idle。
2. 销毁 editor ImGui descriptors、viewport render targets 和 panel/runtime integration state。
3. 销毁 frame resources、deferred resources 和 transient targets。
4. 销毁 renderer 拥有的 buffer/image/pipeline/descriptor。
5. 销毁 swapchain。
6. 销毁 allocator-backed resources。
7. 销毁 allocator、device、surface、debug messenger、instance。

## RenderGraph 数据模型

核心概念：

- `RenderGraphImageHandle` / `RenderGraphBufferHandle`：逻辑 image/buffer 的 typed handle。
- `RenderGraphImageDesc` / `RenderGraphBufferDesc`：名称、格式/尺寸或 byte size、abstract initial/final
  state、shader stage 和 imported/transient lifetime。
- `RenderGraphPassSchema`：pass type、params type、resource slot schema、allowed command kind、culling 和
  side-effect 语义。
- `RenderGraphCommandList`：后端无关 command summary，例如 set shader/texture/constant、clear、copy、
  draw fullscreen triangle 和 dispatch。
- `RenderGraphCompileResult`：已排序 pass、resource dependencies、culled pass、transient allocations、
  abstract transitions 和 final transitions。
- `RenderGraphDiagnosticsSnapshot`：面向 Live RG View / Frame Debug 的只读观测结果，不是 renderer 下一步
  输入来源。

编译步骤：

1. 校验 pass/resource/schema/slot 声明。
2. 根据 producer/consumer 关系构建依赖并做稳定拓扑排序。
3. 计算 pass culling、side-effect 保留和非法读写诊断。
4. 计算 abstract image/buffer transition 和 final state。
5. 计算 transient resource lifetime 与 allocation plan。
6. 生成 `RenderGraphDiagnosticsSnapshot` 供 editor/debug UI 只读消费。
7. Vulkan adapter 把 abstract transitions 翻译为 `VkImageMemoryBarrier2` /
   `VkBufferMemoryBarrier2`；RenderGraph 本身不保存 Vulkan layout/stage/access。

## 当前渲染路径

当前主路径：

1. host 创建 `VulkanContext` 和 `VulkanFrameLoop`。
2. frame loop acquire swapchain image 并调用 renderer frame callback。
3. renderer/host 把 swapchain image 或 offscreen target import 成 RenderGraph resource。
4. renderer 构建 clear、triangle、MRT、compute、fullscreen texture、RenderView 或 debug preview graph。
5. `RenderGraph::compile()` 产出 pass order、abstract transitions、transient lifetime 和 diagnostics。
6. `renderer_basic_vulkan` 经 `rhi_vulkan_rendergraph` 翻译 barrier，并录制 dynamic rendering、copy、
   dispatch、descriptor/pipeline bind 和 draw commands。
7. frame loop submit、present、retire deferred deletion 和 timestamp/debug state。
8. editor path 将 sampled RenderView target 注册给 ImGui texture registry；Frame Debug / Live RG View 只读
   diagnostics snapshot。

Studio Avalonia `Viewport Presentation Transaction` 的当前路径：

1. production `StudioCompositionSession` 在后台启动 native compatibility warm-up，提前让唯一 RenderThread 创建
   Vulkan device/context；它不阻塞 shell ready，shutdown 在 runtime teardown 前等待 warm-up 收口。随后
   `StudioScenePanelView.axaml` 托管专用 `ViewportCompositionControl`。control attach 时取得 composition visual，
   探测 `ICompositionGpuInterop` 与 native device/handle compatibility；unsupported 能力显式进入 degraded UI。
2. Scene exact、Game Preview fit 或 Frame Debugger immutable capture 先形成 endpoint-owned proposal；身份由
   每个 participant 的 `SessionId + EndpointEpoch + TransactionId` 绑定；group 共享 transaction id，session/epoch 按 endpoint 复验。
   owned dock splitter 只是 Scene resize 的 layout adapter：它把 drag delta 合并为
   latest layout proposal，不立即公开新的 `GridLength`，并在同步 probe scope
   内临时应用 proposal 并 `UpdateLayout`；control 以 `ceil(Bounds * RenderScaling)` 捕获 target `PixelSize`，probe Bounds 不推进
   geometry/presentation。coordinator 在 UI dispatcher yield 前恢复 committed `GridLength`，旧 exact Bounds、front surface 与
   `Opacity=1` 保持可见。
3. 每个 participant endpoint 为冻结的 target policy 创建独立 candidate `CompositionDrawingSurface` 与 stream。managed
   `ViewportSession` 合并 latest request，native stream 原子替换唯一 pending-latest；唯一 RenderThread 异步 record/submit，candidate
   只生产首帧。每个
   stream 最多三个持久 full slots，全局仍硬限四个 frame resources，不为每帧重建 external image/semaphore/import。
4. endpoint 通过 `ICompositionGpuInterop.ImportImage` / `ImportSemaphore` 导入 candidate lease；只有 Scene exact policy 的
   allocation == logical == target `PixelSize`、candidate generation、identity/revision 与单调 sequence 全部匹配才调用
   `UpdateWithSemaphoresAsync`。必须等待该 task 成功才返回 prepared handle；fault/cancel/stale 保留旧 committed layout/front，
   candidate 按 work-fence 或 quarantine 语义收口，不能提前标记 current/presented。
5. transaction 依次经过 Proposal→Preparing→Prepared→Validated。所有 participant 都 validated 且属于同一个 compositor scope 时，
   coordinator 才在同一 UI/composition publish turn 应用可选 `GridLength` mutation 和全部 `visual.Surface`/`Size` switch，opacity
   始终为 1，并共享一个 batch `Rendered` barrier；之后进入 Retiring/Completed。publish 前任一 mismatch/cancel/stale identity 使全组
   Aborted 并保留旧 front；publish 后结果歧义进入 Quarantined。跨 compositor 必须拆分，明确不原子。Scene A→B→A 仍必须为
   第二个 A 独立 prepare；Game fit 和 Frame Debug capture 不复用 Scene geometry state。
6. plain `GridSplitter.ShowsPreview` 或 drag-end debounce 会让拖动期间没有 unique exact geometry，不能满足 `>=60/s`，因此不采用。
   同一 drag 的新 proposal latest-wins 替换 queued successor，不取消正在 preparation/publish 的 active proposal；每个 successful
   switch 后立即准备当时最新 proposal。显式 cancel 或 session/endpoint epoch 失效才终止尚未 publish 的 active candidate。非 owned splitter 的直接
   Bounds/DPI/top-level resize 仍通过 Render-priority early admission 走 exact-only hidden fallback：禁止 crop/stretch，但明确允许短暂
   blank，不能作为 flash-free dock acceptance 证据。
7. typed `Backpressure` 在下一次 composition cadence 重试；Unavailable/device failure 显式降级。`IsRealtime=true` 即使静止也每个
   commit 重挂下一帧并以 exact surface-update `>=60 FPS` 为最低门槛；candidate commit 后才恢复 steady 预填充。`false` 只响应
   dirty invalidation。hidden dock tab/lifetime pause 停止 admission，ancestor visible、新 surface attach 或 lifetime
   replacement/resume 以 `Exposed` 恢复一帧；closed session 不再接受 UI invalidation。camera/target/exposed 通过 request-sequence
   content fence 拒绝旧内容帧，extent 仍由 geometry generation 独占门控。
8. 每轮 frame 通过 `editor_viewport_complete_frame_v5(stream, slot, completionKind)` exact-once 完成；compositor submission 前拒绝
   用 `NotSubmittedToConsumer`，update 完成后使用 `ConsumerAccessed`。submission、disposal 或 completion 结果歧义时对应资源进入
   process-lifetime quarantine。control detach 停止 admission 并等待所有 front/candidate frame/surface cleanup；process shutdown
   再 drain native RenderThread 与 Vulkan owner。`--smoke-studio-viewport-cadence` 只保留前台静态 Scene 的 5 秒 Realtime 稳态基线；
   `--smoke-viewport-transaction-resize`、`--smoke-viewport-transaction-overload`、`--smoke-viewport-transaction-faults`、
   `--smoke-viewport-transaction-supersede` 与 `--smoke-viewport-multi-endpoint` 已拆成独立真实 Studio GPU smoke，
   `--smoke-viewport-transaction-flash` 再做 transaction-batch 结构边界
   检查。最终 GPU process acceptance 为 47/47；它们按 native resource、transaction phase、Avalonia surface/`Rendered` 与 physical display 分层报告；没有 observer 的层输出
   evidence unavailable。代表性 resize 完成 209/209 observed exact `Rendered` generations、106.44/s、p95 15.26 ms、hidden 0，
   steady 为 219.43 surface-updates/s。当前 PresentMon 采样因大量 ETW event loss 且没有 CSV 被作废；multi-endpoint 只通过两
   endpoint 的 group boundary，3–4 realtime lane 与 slow-consumer HOL 仍是 blocker。
9. native runtime 在唯一 RenderThread 上拥有 steady-clock frame snapshot：frame index 是 render-attempt identity（失败允许留 gap），
   time/delta 是真实单调 render 时间而非 `ordinal / 60`。delta 以 runtime 中上一次任意 stream 成功 record 为边界；Avalonia
   cadence、GPU timeline 与 physical present 都不反向定义 editor/world time。

参考模式的取舍记录在 [ADR-0006](../../apps/studio/docs/adr/0006-viewport-interactive-resize.md)：采用 Unreal 的 immutable render
handoff/thread owner boundary、Unity 的 semantic invalidation→repaint 分层与 hidden tab refresh 行为、O3DE 的 viewport-size state 与
render tick 分离及无 drag-end debounce；拒绝复制其品牌 API、模块/widget owner、固定 editor tick 或任何跨 compositor 的伪原子提交。
`Viewport Presentation Transaction` 的 endpoint ownership、identity tuple 和 same-compositor 原子边界是这些公开事实与 Asharia
package-first、Avalonia composition、headless/Vulkan lifetime 约束结合后的本地推论，不声称来自任一引擎的同名 API。

当前建议仍保持：

- dynamic rendering 是主路径，不回退到传统 render pass/framebuffer 抽象。
- 使用单 graphics queue；async compute 和 transfer queue 在明确 queue ownership 与 smoke 前暂缓。
- 每新增一个 RenderGraph resource state，必须同步定义 abstract semantics、Vulkan translation 和 smoke。
- descriptor allocator、bindless/resource table 和 material pipeline cache 仍在后续扩大；当前
  `material-core` 只提供 CPU-side signature/key 合同。

## 同步策略

- CPU/GPU frame pacing 使用 per-frame fence。
- acquire/present 使用 binary semaphore，保持 swapchain 兼容性。
- GPU work submission 使用 `vkQueueSubmit2`。
- graph 内 image/buffer transition 通过 `vkCmdPipelineBarrier2` 和 synchronization2 barrier。
- frame callback 必须返回 acquire semaphore 的 wait stage；无法精确声明时只能短期使用
  `VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT` 并记录待细化问题。
- 不用 `vkDeviceWaitIdle` 或 `vkQueueWaitIdle` 解决正常 render loop 同步；只允许 teardown、swapchain
  recreate、早期 MVP 简化路径或已注释 debug probe。

## Swapchain 策略

- out-of-date 或 surface 不兼容时 recreate swapchain。
- suboptimal 初期作为 warning 路径；后续再决定是否立即 recreate。
- RenderGraph resource 不直接依赖 raw swapchain image index；swapchain image 作为 imported resource。
- swapchain extent 改变时重建 framebuffer-sized transient resources 和 editor viewport targets。

## 后续扩展点

- `renderer_basic_vulkan` 按 RenderView recording、sample scene renderer、debug preview/capture support 继续拆分。
- 在现有 renderer-owned overlay pass input 合同上继续接入 pixel/readback grid smoke、scene mesh、selection、
  gizmo 和更完整 debug line/source diagnostics。
- asset-pipeline / resource upload 把 source asset、product cache 和 runtime GPU resource 分开。
- `material-core` 的 descriptor/resource signature、pipeline key 和 shader reflection JSON 形成可审查合同；
  backend pipeline/cache 实现仍由 renderer/RHI 后续 slice 承担。
- `packages/systems/editor` 内部 `editor_domain` 只保留 selection、commands、undo/redo、workspace 和 backend-neutral viewport state。
- 通用 runtime CPU worker/RenderThread/RHIThread、large job graph、async compute、bindless 和 hot reload 都必须先有
  ownership、fallback 和 smoke；Studio Scene View 已有的单 native RenderThread 仅是 viewport Vulkan owner boundary。
