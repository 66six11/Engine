# 架构健康与能力接入规范

状态：current governance

更新日期：2026-08-02

## 1. 目的与权威边界

本文定义“一个能力何时可以开始、何时必须接入、怎样证明没有破坏整体架构”。它不建立第二套功能路线图：

- 完整系统目录、长期依赖方向和 Foundation Gates 以
  [`system-architecture-roadmap.md`](../planning/system-architecture-roadmap.md) 与
  [`foundation-framework.md`](../architecture/foundation-framework.md) 为准；
- 当前真实依赖、启动和 frame flow 以 [`flow.md`](../architecture/flow.md)、
  [`overview.md`](../architecture/overview.md)、CMake codemodel 和源码为准；
- 近期功能顺序以 [`next-development-plan.md`](../planning/next-development-plan.md) 为准；
- 本文只拥有架构审查方法、能力接入窗口、Slice 准入/退出证据和当前审查结论。

必须明确区分三种状态：

| 状态 | 含义 | 可以作为实现前置吗 |
| --- | --- | --- |
| `current` | 已由当前源码、构建图和测试证明 | 可以 |
| `target` | 已接受的目标合同，但尚未全部落地 | 不可以，除非当前 Slice 正在实现它 |
| `deferred` | 只保留问题和边界，尚无产品 Slice | 不可以 |

架构文档不得在同一段中用完成 Issue 的历史叙述替代 current snapshot。进度、阻塞和 Done evidence 属于
GitHub Issues / Project；长期文档只保留当前事实、目标合同、拒绝理由和可重复验证命令。

## 2. 2026-07-31 审查基线

本轮以工作树当前状态为审查对象，执行并通过：

```text
python tools/check_package_topology.py
  packages=28 targets=76 ownership-roots=12

python tools/check_package_contracts.py
  passed

python -m unittest discover -s tools/tests -p "test_*.py"
  525 tests passed; 6 conditionally skipped
```

这些证据说明 package control plane、Host Runtime 当前已验证的部分和已有硬 target 边界应保留，不应进行全仓推倒重写。
它们不证明 Studio、World、runtime resource 或 observability 已成熟；configured CMake dependency evidence 由下述独立门禁证明。

### 2.1 保留、硬切与延期

| 处理 | 范围 | 原因 |
| --- | --- | --- |
| 保留 | package manifests/resolver/lock/composition、ProcessScope、RenderGraph/RHI/Vulkan 硬边界、CPU/headless data/content/world baseline | 已有明确 owner 与自动化证据 |
| 保留并执行 | manifest 与 configured CMake direct graph 对证；Studio R0/R0.5只读基线 | 28 manifests / 76 targets / 149 direct edges 已由 tests-on codemodel 证明；Studio owner/diagnostics/Host/Pipe/CLI/MCP已有真实纵向证据 |
| 修复 | managed evaluated ProjectReference/public-consumer closure、Platform/Foundation services、runtime resource ownership | 合同方向正确，但 current 实现或门禁不完整；Studio bounded diagnostics的并发commit/cursor修正已关闭 |
| 无兼容硬切 | Studio legacy composition、同步 async lifecycle、editor-side closure transaction、native viewport V1–V4 raw-token compatibility contract | V1–V4 frame exports 已硬切删除；不得为新 V5 production consumer 恢复旧入口或兼容层 |
| 延期 | dynamic native unload、managed generation reload、通用插件 SDK、全面 ECS、通用 runtime RenderThread/RHIThread 与 large job graph、bindless/多队列、外部 registry | 尚无稳定 owner、真实第二 consumer 或性能证据；Studio viewport 已有作用域受限的 native owner thread，不代表通用 runtime threading 已成立 |

### 2.2 当前 Findings

以下是实现优先级，不是要求在一个 PR 中同时修改。

#### P1：Host Runtime 与真实应用仍是两张生命周期图

`engine/host-runtime` 已有 registration、eligibility、ProcessScope 和 synthetic tests，但当前 sample/editor hosts 仍直接聚合
底层 targets，没有经过同一 ProcessScope/application owner（`docs/architecture/flow.md:147`、
`apps/sample-viewer/CMakeLists.txt:28-42`、`apps/editor/CMakeLists.txt:220-235`）。

结论：暂停继续扩建 activation schema/抽象，下一步应让一个真实、很小的 Runtime/Studio application slice 经过固定
Host owner，证明 start → run → quiesce → reverse stop。synthetic control plane 通过不等于产品生命周期已经接通。

#### P1：Studio 仍有多个互相竞争的生命周期和状态 owner

- R0 owner 子 Slice 已将 `App` 设为唯一 `StudioProcessSession` owner；production composition 只有最小
  `StudioCompositionSession(StudioShellViewModel)`，legacy adapter/contracts/catalog 与 built-in Workbench 已删除；
- `StudioProcessSession` 已覆盖start failure/cancellation、managed dispose failure/timeout/late fault observation、
  lifetime callback failure/timeout/late fault observation、lifecycle-gate timeout、caller cancellation与repeated stop，
  cancel/gate/dispose共用一个monotonic budget，并输出只陈述真实managed child的immutable receipt；
- production Studio C# source 已无 `.Result`、`.Wait()` 或 `GetAwaiter().GetResult()`；MainWindow 不再拥有
  process/native teardown；
- 断开的 `StudioCompositionRoot`、aggregate Workbench module、`EditorExtensionComposition`、
  `MainWindowViewModel` 与 legacy session branch 已删除；
- 断开的 Dock graph/hit-test/tab/floating-window、panel registry/cache、View/ViewModel、全局 `ViewLocator`
  与专属 tests 已删除；R0 Shell 不声明 Dock/panel 能力；
- 28-file ProjectCode build/artifact/pinned-load/scope-activation控制面只有friend tests、无production consumer，
  已连同专属tests删除；Application不再拥有ProjectCode/process/filesystem实现；
- ProjectCode删除后成为孤岛的Application-side Editor Image inventory/managed build-environment projection及专属tests已删除；
  当前Editor Image receipt由独立build/release producer从fresh publish与exact runtime闭包生成，Studio进程不重复扫描或签发第二truth；
- ProjectCode/legacy adapter删除后仅由tests实例化的Application module registry/scope transaction/static generation
  host及专属tests已删除；R0没有extension activation owner；
- legacy composition/Workbench/Dock删除后无任何production factory/template/host的root built-in Features及专属
  tests已删除；diagnostic hub仍是未来Console/Problems唯一truth；
- legacy command/menu删除后无request producer、Window host或template入边的Dialog presentation及专属tests已删除；
  随后仅由self-tests/架构库存维持的7个public Dialog data types也整体删除，R0不声明modal capability；
- 无Window/Dock/App producer的public/Application lifecycle-event岛及两侧self-tests已删除；唯一process lifecycle
  继续由`StudioProcessSession`直接拥有，不增加第二个100项事件历史或把它混入diagnostics truth；
- 无真实work/CTS/cancel/join且terminal dictionary无界的public/Application background-task状态岛及self-tests已删除；
  `StudioProcessSession`的真实async teardown保持独立，通用TaskSupervisor等首个真实operation再接入；
- 无ViewportSession/presentation/renderer caller的12-type public DTO与Application纯scheduler岛及tests已删除；
  C++ editor/native smoke保持独立证据，不冒充managed Studio viewport能力；
- 无Document/native mutation producer且异常可丢history的public Editing/Transactions + Application closure service岛已删除；
  R0不声明事务能力，未来只从typed intent、authoritative receipt/revision/inverse与savepoint重新接入；
- 无project-selection intent、App/MainWindow入边或owner的Project launch presentation及其专属dispatcher/text
  projection/tests已删除；Application/Core/public/native Project链留待下一依赖格审计；
- presentation删除后只剩tests自引用的managed ProjectOpenSession parser/source/public records已删除；真实
  headless bootstrap-session producer与fixture归`tools/**`继续验证，未被fixture替代或误删；
- 随后仅由stub tests构造的active ProjectSession/recent store/Application descriptor port/Core gateway与public
  session records也已删除；managed EngineBridge Project/native ABI及真实project-core package保持分格审计；
- Core gateway删除后只剩stub marshalling tests的managed EngineBridge Project adapter也已删除；C++ project
  ABI/smoke保持到独立native Slice，不能用managed fixture冒充双编译器/native证据；
- 无managed caller的C++ `editor_project_*` bridge/self-smoke及`editor-native -> project-core-io`边随后删除；
  `asharia-editor`真实Project IO与package-owned smoke保持，双编译器/target-truth/tidy证据通过；
- 无native consumer的Studio viewport managed bridge/model、deployment target与phantom teardown字段已删除；
  root publish和Editor Image显式拒绝`Runtime.Contracts`、`EngineBridge`、`editor_native.dll`与`slang.dll`；
  AppRelative runtime只携完整选定hostfxr/Core runtime树，SDK template仅作build-time qualification，
  无reader的`dotnet.exe`、SDK/reference pack与ProjectCode metadata不进入产品；无consumer的managed/public
  Frame Debugger P/Invoke、snapshot合同和专属scheduler分支也已删除，当前不存在Studio capture能力；
- R0 disposable-child纵向证据已由test/observability owner启动构建产物中的真实`Editor.exe`：Ready后正常Window close
  进入`App -> StudioProcessSession -> StudioCompositionSession`并以OS exit `0`结束；forced fatal、owner deadline与
  observer cancel均只在目标进程外kill process tree，并在独立5秒deadline内确认reap。`Program.Main`直接返回
  Avalonia classic-desktop lifetime的`int`；production Studio没有新增subprocess owner、fault mode、artifact或第二diagnostics truth；
- 无record/hub/consumer且仅由public自证测试维持的`Asharia.Editor.Diagnostics.EditorDiagnosticSeverity`已删除；当前severity
  只存在于App-owned bounded diagnostic/log完整合同中，public reflection gate拒绝旧Diagnostics namespace复生；
- 无registry/host/lease或任何production引用的root-App `EditorExtensionId`已删除；原架构测试从强制其存在反转为
  source-directory absence gate，不把孤立identity record冒充extension capability；
- legacy panel/window callback host删除后仅自调用、无任何consumer的`CallbackExceptionBatch`已删除；当前process
  teardown owner继续直接记录typed failure/receipt，不保留无owner、无界的`List<Exception>`聚合器；
- 无任何code/test consumer且文档声称的compatibility adapter实际不存在的Core provider declarations先行删除；随后
  仅相互引用并由self-tests维持的public Scene snapshot、Application provider host与Core in-memory provider SCC也已
  整体删除。当前App/composition没有Document/World、provider registry、Scene snapshot或只读Scene能力；
- 仅由Application内存service/self-tests维持的public Selection岛已删除；distribution fixture中唯一越界引用是
  `typeof(IEditorSelectionService)`合成锚点，现已连同test工程直接public ProjectReference移除，且未换绑其他public type。
  当前App/composition没有selection producer、reader、subscription或Document/World scope；
- 无Window/Dock/Control/panel instance producer的public lifecycle/frame callback与Application scheduler岛已删除；旧文档声称的
  Presentation timer调用链实际已随legacy UI消失。Application及其test工程现不再引用`Asharia.Editor`，只保留App-owned
  bounded diagnostics source；panel descriptor/declaration仍留到Extensions/Contributions SCC的下一依赖格；
- 最后24个public Extensions/Contributions/Panel declaration source只形成无Host/loader/registry/content consumer的内部SCC，
  现已连同9个self-test文件整体删除；随后空`Asharia.Editor`/test project、App/Architecture/solution edges与distribution
  `EditorContract`/identity/deps/receipt/CLI image合同也已整体删除，且publisher显式拒绝旧`Asharia.Editor` artifact；
- 当前 hard-cut 目标和未完成门禁已记录在
  [`studio-frontend-hard-cut.md`](../../apps/studio/docs/architecture/studio-frontend-hard-cut.md)。

结论：process owner、legacy composition/Workbench/Dock/Dialog presentation source 删除、bounded diagnostics/log、真实 Headless/accessibility baseline、Code-first source hard-cut、phantom native deployment closure与managed/public Frame Debugger幻影岛删除
、真实disposable-child exit/reap及旧Diagnostics/extension identity/callback/provider/public Dialog/lifecycle-event/task-state/managed viewport/transaction/Scene snapshot/Selection/Panel runtime、最终public SDK SCC和空project/image合同删除已形成自动化证据；R0总门禁已由
`studio-frontend-hard-cut.md` 4.39关闭；R0.5只读Protocol/Host/current-user Pipe/typed CLI/UI Probe/MCP及其并发、部分启动失败、completed-window/monotonic cursor、writer-gate取消、typed retention、owner/discovery与Codex标准生命周期互操作修正已由
`studio-development-observability.md` 2.1→2.16关闭；官方Codex fresh app-server thread已在process cwd与worktree root刻意分离时，以thread runtime fallback取得ready、精确六tool catalog和一次真实只读调用。恢复既有失败task仍须通过官方MCP配置reload后的下一active turn或新task刷新其旧catalog，不能据此否定已关闭的fresh-thread宿主证据。下一能力格只能从当前执行顺序重新过I0→I6，仍不得新增panel framework、
extension/reload、第二套UI authoring、新Shell service、Capture/Mutate或任意远程控制。

2026-08-04 当前更新：后续 #352/#353 已恢复 authoritative ProjectSession/SceneDocument 编辑闭环，#359/#361 又从
UI-neutral `ViewportSession` 和 EngineBridge lease 接入一个专用 Avalonia Scene View consumer。Release image 因而有明确 reader
与 lifecycle owner，现要求 `Runtime.Contracts`、`EngineBridge`、`Presentation.Avalonia`、project/scene/viewport native 与精确
viewport shader bundle；R0 当时“拒绝这些无 consumer artifact”的删除证据保留为历史事实，不再代表当前产品 closure。

#### P1：SceneDocument mutation 与 native session 尚未接入可靠事务

- 原`EditorTransactionService`在Undo/Redo成功前移动history，且只有string descriptor/closure compensation；该无consumer
  public/Application岛已整体删除，不再作为待修补基础；
- R0没有Document scope、authoritative revision/receipt、inverse change set或savepoint，不声明mutation/undo能力；
- `scene-core::World` 暴露可因 container mutation 失效的 mutable transform pointer：
  `packages/scene-core/include/asharia/scene/world.hpp:20-49`；
- native ABI 只有逐 entity 同步操作，没有 `DocumentId + expectedRevision + atomic batch + receipt/change set`。

结论：旧closure transaction已删除。第一条真实写闭环必须是 typed mutation batch → authoritative receipt → revision/dirty →
undo/redo → savepoint。

#### P1：native viewport 边界仍不是可重启、显式分代的 session

- `EditorSharedViewportRuntime::instance()` 仍通过永久分配实现 process singleton；第一次 device/render/release
  请求可启动唯一 RenderThread，`editor_viewport_shutdown()` 之后 runtime 永久进入 draining/terminal 状态；
- Vulkan context、producer、outstanding/retiring packet 与析构只由 RenderThread 访问。V5 stream 请求复制 owning
  packet 进入有界 render/control/release mailbox；ABI 已有显式 stream lifecycle，但仍没有 process-level native
  session handle、generation 或 device epoch；
- `stats()` 现在只读取 owner 发布的 diagnostic snapshot，加上有界 live queue/lifecycle/atomic counter；它不
  启动或 join RenderThread、不轮询 retirement，也不创建、退休或销毁 Vulkan 资源：
  `apps/editor/src/editor_shared_viewport_runtime.cpp:397-421`；
- exported ABI 直接进入可能分配、加锁和构造 C++ error/string 的实现，没有统一 `noexcept`/catch 边界；异常不能跨 C ABI。

结论：owner thread、V5 stream lifecycle 与 bounded mailbox 基线已经成立；下一次 contract hard-cut 目标是显式
`NativeSessionHandle + epoch + bounded lease`。查询继续保持只读 snapshot；V1–V4 frame exports 已删除，
不得重新建立 compatibility path。

#### P1：两个 Editor host 仍同时进入默认构建

`apps/editor/CMakeLists.txt` 同时构建 `editor-native` 和 Dear ImGui `asharia-editor`；后者又编译一部分相同的 viewport/frame-debug
源码并链接 `editor-native`。Studio 是目标产品前端时，旧 host 只能作为明确、限时的 smoke harness，不能继续拥有 production
asset/document/world truth。

#### P2：当前事实与门禁存在漂移

- 审查开始时系统路线图仍写 26 个 manifests，实际为 28 个 manifests，且
  `engine/host-runtime`、`packages/project-bootstrap` 已存在；本轮已修正手写计数，但尚未消除再次漂移的可能；
- `check_package_topology.py` 验证 manifest shape、owner、DAG 与直接 target 声明；本轮新增
  `check_target_dependency_truth.py`，已在 CMake 4.4 / codemodel 2.11 的 `msvc-debug-tests` configured graph 上精确对证
  76 个 manifest targets 与 149 条 direct edges；
- 审查时 `apps/editor/asharia.package.json` 漏列当时存在的 `editor-native -> asharia-project-core-io` 和
  `asharia-editor -> editor-native`，并误列 `asharia-editor -> asharia-shader-slang`；前者随无caller native Project
  bridge删除而从CMake/manifest同步撤销，后两项修正保持；按
  `review.md` 执行 conditional local truth gate 时会阻止同类漂移，CI enforcement 留给独立 toolchain Slice；
- 原 `pre-pr.ps1` 与 `check-doc-sync.ps1` 使用 `--diff-filter=ACMRT`，删除文件不会进入门禁；原 pre-PR 又把任意
  `apps/` 改动误判为全量 rendering smoke。

结论：native manifest ↔ configured codemodel direct graph 已建立自动证据；F0 仍需补 managed evaluated
`ProjectReference` graph 与 public-consumer closure 对证。工具不得报告比它实际验证范围更强的保证。

#### P2：Foundation observability 和 runtime resource 仍只是局部 baseline

- Studio R0/R0.5 已建立由 `App` 唯一持有的 `StudioDiagnosticHub`：diagnostic/log 分别使用预分配的 2,048/8,192
  slot ring，记录 stable code、UTC timestamp、process scope/generation、operation/correlation、cursor/drop/truncation；
  subscriber 上限为 64，异常与 publisher 隔离。Avalonia 通过进程级、线程安全、非阻塞的自有 `ILogSink` 映射到同一 hub，
  Console/Problems、CLI与MCP只是同一真值的只读适配器；reservation/commit洞、factory failure tombstone、部分Pipe启动回滚和
  MCP semantic request ID均有确定性negative证据；
- `packages/profiling` 是单线程 frame vector，热路径持有动态 string/vector，满时 `erase(begin)`；
- runtime resource registry 线性查找并返回内部 pointer/span，mutation 后可失效，尚无 IO、budget、residency、eviction 或 owner thread。

结论：Studio diagnostics/log 的 R0/R0.5只读边界已完成；runtime resource 与共享 Foundation observability仍不是成熟
service。后续能力只能复用现有ring或在Foundation F3显式迁移，不能建立第二真值，也不得先做远程万能控制面。

其他 Foundation 边界也仍需收敛：`engine/platform` 是空 interface；`window-glfw` public API 暴露 GLFW raw pointer 和
Vulkan surface；`core::ErrorDomain` 反向枚举上层领域。应由 Platform/F3 与 typed package-owned error code Slice 分别修正，
不能把它们一次性重写成巨型平台框架。

#### P2：backend-neutral renderer target 泄漏工具依赖

审查时 `asharia-renderer-basic` 的 public headers 只需要 core/rendergraph，但 CMake `INTERFACE` 传播
`asharia::shader_slang`；实际 Slang reflection 只在 Vulkan implementation TU 使用。本轮已将该依赖收回 Vulkan implementation
target；当前 direct truth gate 会防止 backend-neutral `asharia-renderer-basic` 重新声明该边，但尚未证明整个 shipping frontend
closure 不携带 shader compiler，public/shipping closure gate 仍待补齐。

#### P3：组合根和 smoke harness 过大

`apps/sample-viewer/src/main.cpp` 已超过 7,000 行；`asset_browser_panel.cpp`、`basic_renderers.cpp` 也承担过多独立场景。
原 `ProjectCodeSdkBuildController.cs` 已随无consumer控制面整体删除，不再列入拆分候选。拆分触发条件不是行数本身，而是已有多个独立 owner/error/test boundary。
后续只在相关 Slice 中提取 registry/fixture/owner，不进行无语义的批量搬文件。

## 3. 完整模块覆盖检查

系统目录已经覆盖 Data、Content、Memory、Storage、Settings、World、Tasks、Input、Desktop Platform、Scripting、Rendering、Physics、
Animation、Audio、Navigation、AI、Runtime UI、Gameplay、Networking、Online、Localization、Media、XR、Editor、Project Product 和
Observability。不得把这些尚未实现的条目误报成“路线图遗漏”，也不得为每一行提前创建空 package。

还必须显式跟踪以下跨系统能力；它们不一定需要独立 System Package：

| 跨系统能力 | 当前归属 | 最早必须固定的合同 | 何时才建立完整实现 |
| --- | --- | --- | --- |
| stable identity、version、migration | Data + Content + World + Project Product | ID、schema/product version、unknown-field/missing-type 保留 | 首个持久化 SceneDocument 前 |
| save、checkpoint、replay、determinism | World + Gameplay + Product | revisioned snapshot、clock/input/event identity；不承诺 lockstep | 出现真实 save/replay/network rollback Slice 时 |
| capability、trust、secrets、privacy | Package Runtime + Host + Product | least-authority grant/deny、secret 不入 manifest/log/artifact | Studio-only 最小 Observe 在 R0.5；共享 Foundation 实现在 F3；外部 registry/provider 在 Phase 11 |
| accessibility | Studio Presentation；未来 Runtime UI | stable semantic ID/name/pattern，不以坐标或本地化文本定位 | 新增复杂 Studio 控件前；runtime UI 实现时再建运行时部分 |
| device lost / process crash / recovery | RHI + Host + Observability + Editor | typed fault、epoch invalidation、process clean-exit/teardown receipt 与 disposable-child exit-code evidence | 扩大 GPU/authoring surface 前先有无 artifact store 的最小 negative smoke；Document clean-close marker 专属 R2 后 recovery journal；共享 crash collector/capability 到 F3/R5 |
| automation / evidence retention | Observability + workflow | R0.5 先冻结 test/job/artifact ID、retention、hash 与 capture context 合同 | job/artifact store 随首个 Capture 或 process-acceptance Slice 实现 |
| install/update/patch/DLC | Project Product + Package Runtime | immutable stage/generation、atomic publication/rollback | 本地 standalone stage 稳定以后 |
| source-control integration | Editor + Document/Content adapters | status snapshot、checkout/add/delete/move intent、conflict diagnostic | authoritative savepoint 与 asset rename/move 成立后；不是 Foundation System |
| autosave / crash-session recovery | Editor + Project Product | recovery journal、clean-close marker、document generation、quota | authoritative save/reload 通过 I3 后，复杂 authoring 前 |
| GPU fault evidence | RHI + Rendering + Observability | validation/device-lost/hang/reset 分类、marker/breadcrumb、driver/device/build identity | FrameContext/queue owner 成立后；可用扩展按 capability 采集 |
| source-change coordinator | Content tool owner | source event coalescing、dependency invalidation、job/cancel/generation | deterministic import/cache 和 explicit worker owner 完成后 |
| process/UI acceptance coordinator | test/observability infrastructure | process session、ready/heartbeat/timeout/cancel、artifact/result | R0 Headless/UIA baseline 后；不进入 production UI service |

## 4. 两级门禁模型

### 4.1 全局 Foundation Gates

全局继续使用 `F0` 至 `F6`，不再创建竞争编号：

```text
F0 current facts
-> F1 package plan
-> F2 host runtime
-> F3 foundation services
-> F4 data/content
-> F5 world baseline
-> F6 authoring host
```

大型新系统、脚本 VM、复杂 renderer extension 不能绕过 F2/F3。现有产品路径的 bugfix、删除兼容层、收窄 public API 和
建立 headless evidence 属于 boundary repair，可以继续，但不能借此扩大功能面。

### 4.2 每个能力的 Integration Gates

每个 Slice 只推进一格；没有真实需要时可以停在当前格，禁止一次性铺到 `I6`。

| Gate | 必须回答 | 退出证据 |
| --- | --- | --- |
| I0 Problem | 哪个真实 workflow/故障/测量需要它；成熟引擎 precedent 是什么 | 本地文件/调用路径/触发场景 + 一手资料；明确 non-goals |
| I1 Contract | owner、scope、lifetime、thread、input/output、error、budget、remove/update 是什么 | 最小 public value contract 与 negative contract tests |
| I2 Headless | 不依赖 UI/GPU/具体 backend 时能否创建、使用、失败、停止 | package/Application tests；zero leak/outstanding work |
| I3 Product Slice | 是否有一条真实 source/data → owner → output → consumer 闭环 | 一个端到端 smoke，使用真实 contract，不用 production fixture |
| I4 Host/Authoring | Editor/CLI/CI/UI 是否只是 adapter，是否共享同一 use case | parity、disconnect/restart、close/recovery tests |
| I5 Scale | 何处有 profile 证据需要 jobs/thread/cache/batching | before/after measurement、bounded back-pressure、stress/shutdown |
| I6 Ecosystem | 是否有第二个真实 consumer、版本/权限/卸载需求 | compatibility matrix、capability denial、remove/update/recovery |

“先设计”只要求完成当前 Gate 所需的 contract，不授权提前实现后续 Gate。尤其不得以未来插件、未来多 backend、未来 ECS 或未来
远程工具为理由增加当前没有 consumer 的抽象。

## 5. 能力接入窗口

“最早”防止过早建框架，“最迟”防止系统在错误路径上积累后再补地基。

| 能力 | 最早安全接入 | 最迟必须接入 | 当前决定 |
| --- | --- | --- | --- |
| bootstrap error/log/time/ID | 进程启动第一步 | package/distribution 解析前 | 保持最小 Kernel，不继续塞领域类型 |
| Host scope/lease/rollback | package plan 能给出固定 factory order 后 | 新增第二个完整 System owner 前 | 继续完成 F2，不重写已通过的 ProcessScope |
| Platform lifecycle、Storage、Settings、Memory、Tasks cancel/join | Host Runtime 可 headless 激活后 | Content/World/Scripting 分别自建第二套生命周期前 | F3 当前最高优先级 |
| bounded diagnostics/local failure evidence | bootstrap 就保留最小 sink；Host 后升级 router | 首个复杂 authoring、后台 worker 或 Play Session 前 | R0/R1 先有 bounded ring、process teardown receipt 与 disposable-child fatal/exit negative；共享 crash collector 到 F3/R5，高成本 trace 后置 |
| canonical schema/persistence | F2/F3 owner 成立后 | 新增更多持久化 Scene/Asset/Script schema 前 | 冻结旧 reflection/serialization 扩张，完成单一路线 |
| Content/resource runtime | Storage + schema + budget contract 可用后 | real SceneDocument/material/render consumer 前 | 先闭合 artifact → handle → resident owner，不先做 watcher farm |
| World spatial/snapshot/mutation | Data/Content ID 与 Host phases 成立后 | Scene Authoring、多 view extraction、Physics 前 | 先 headless World 和 revisioned snapshot |
| Input | Platform facts、Settings 和 update phase 成立后 | Play、Gameplay、Physics character control 前 | logical action/context 必须先于 Play，不把 raw GLFW 扩散出去 |
| Studio hard cut | 当前即可，属于错误边界删除 | 新 panel/extension/authoring feature 前 | 完成 R0，再做唯一 SceneDocument R1 |
| read-only DevTools/UI Probe | R0 全部门禁成立后的 R0.5 | R1 authoring surface 扩大和 Play/worker 并发前 | 无 Avalonia Plus；R0 完成壳状态、Headless 与 accessibility baseline，R0.5 暴露只读壳状态，R1 关联真实 Document revision |
| write commands/automation | Document transaction + expected revision + capability 已通过 I3 后 | 需要外部驱动真实编辑 workflow 时 | 不进入 DevTools v1，不允许任意反射/脚本/shell |
| minimal Build/Cook/Stage/Standalone | 一个真实 Data/Content/World/Render vertical slice 后 | Scripting 和 Phase 10 大量系统固化 Editor-only 假设前 | 应早于广泛插件/系统波次；不要等全部 Editor 功能完成 |
| Scripting implementation | schema、World safe point、capability、Host scope、standalone closure 后 | 首个 script-authored gameplay/asset rule 前 | 先窄 facade，再做 .NET adapter；不先做 hot reload |
| Physics/Animation/Audio | F3 + World/Resource/frame schedule 成立，且有真实产品 Slice | Gameplay Feature 开始依赖该能力前 | 按 vertical wave 逐个进入，不建空系统框架 |
| 通用 runtime RenderThread/RHIThread 与 large job graph | immutable extraction、cancel/drain 和 profile 证据后 | 已测 CPU/back-pressure 证明单线程不达标时 | Studio viewport 已有作用域受限的 native owner thread；通用 runtime Tasks baseline 早，平行执行晚 |
| dynamic reload / external packages | first-party add/update/remove 和 restart-required 路径稳定后 | 只有真实外部 consumer 才构成需求 | Phase 11；当前删除 speculative framework |
| bindless/async compute/multi-queue | material/resource lifetime 与 GPU timeline 稳定且 profile 证明需要 | 目标硬件/内容规模要求时 | deferred |

## 6. 所有 Slice 必填的架构合同

### 6.1 Owner card

```text
Current evidence:
Problem / trigger:
Owner and scope:
Create -> active -> quiesce -> destroy order:
Owner thread / safe points:
Input values and output values:
Stable identity and revision/generation:
Error / cancellation / timeout / recovery:
Memory, queue and artifact bounds:
Diagnostics without control-side effects:
Add / remove / update behavior:
Foundation prerequisite:
Earliest safe / latest required integration point:
Non-goals:
Exit evidence:
```

任何一项写成“由 manager/context 处理”都不算答案，必须指出具体 owner 和销毁责任。

### 6.2 首选数据结构

| 语义 | 首选结构 | 禁止替代 |
| --- | --- | --- |
| 跨边界身份 | strong typed ID + generation/epoch | pointer、UI object、connection ID、数组下标作为长期身份 |
| 可读状态 | immutable snapshot + revision + capturedAt/source | 暴露 mutable object graph 或 getter 触发生命周期变化 |
| 修改请求 | typed intent/command + expected revision | 字符串字段路径、任意反射写入、closure 跨边界 |
| 修改结果 | typed receipt/change set/inverse data | 仅返回 bool 或依赖 log 文本判断成功 |
| 生命周期借用 | move-only/owner-scoped lease | bare owning pointer、未注册 callback、GC finalizer 作为 GPU 释放 |
| 高频流 | fixed ring/slot + sequence/cursor/drop count | 无界 list、每帧对象树、`erase(begin)` 热路径 |
| 长操作 | JobId + state/progress/heartbeat/cancel + artifact | 长时间阻塞 RPC/UI thread 或把大型捕获塞进 JSON response |
| 确定性产物 | sorted vector/manifest + hash/version | 依赖 unordered iteration、绝对开发路径或作者自报成功 |

### 6.3 允许的设计模式

- **Composition Root**：只在 Host/App 顶层装配 concrete adapters；不得承载领域策略。
- **State Machine**：用于 Project/Document/Job/Session 的有限状态与合法转换；失败状态必须可诊断。
- **Command + Transaction**：只表达单 owner 的原子 mutation、receipt、undo/savepoint；不保存任意可执行 closure。
- **Snapshot + Invalidation Event**：事件只通知事实变化，consumer 按 revision 重读 snapshot；subscriber 失败与 owner 隔离。
- **Scoped Registry + Lease**：Host 已验证的 typed contribution；reverse revoke，generation-safe，不是 service locator。
- **Adapter**：隔离 Avalonia、Vulkan、OS、decoder/provider；第三方类型不进入 domain contract。

拒绝默认采用全局 EventBus/Store、`EngineContext`/Service Locator、无边界 `Manager`、通用 ABI、反射式远程对象控制、
先有 interface 后找 consumer，以及为目录整齐而做的大规模搬迁。

## 7. 规范开发流程

1. **Search first**：先找现有 Issue/ADR/current owner，再查 Unreal 对应 owner/lifecycle，至少用 Godot、O3DE 或 Bevy
   的一个开源实现交叉检查；Unity 用公开合同校准产品行为。
2. **Freeze current facts**：记录文件、行号、调用路径、configured target graph 和可复现失败。proposal 不能代替证据。
3. **Classify the change**：bugfix、boundary repair、Foundation、vertical feature、performance 或 ecosystem；只选一个 primary class。
4. **Fill owner card and integration window**：说明当前处于 `I0-I6` 哪一格以及 Foundation 前置；前置不满足则缩小 Slice。
5. **Write failure evidence first**：contract/negative test、smoke fixture 或测量基线必须先能证明问题。
6. **Implement one vertical slice**：一个 owner、一个状态变化、一个真实 consumer；不得同时重命名、搬目录、换 API、加并发和兼容层。
7. **Run scoped gates, then repository gates**：使用 `tools/pre-pr.ps1` 发现受影响门禁；提交前遵守
   [`review.md`](review.md) 和 AGENTS.md 的完整要求。
8. **Cut over and delete**：新路径通过后删除旧 production path、双写、fixture 和 compatibility adapter；失败则保持旧路径，
   不发布半完成 generation。
9. **Synchronize facts**：更新 current docs、目标 ADR 和 Issue Done evidence；临时执行日志不进入长期路线图。

### 7.1 Ready 条件

- 有本地 current evidence 和官方 precedent；
- owner card 完整；
- prerequisite 与 non-goals 明确；
- 一个 PR 可以交付的成功/失败验收；
- 不要求未实现的后续 Gate 才能测试当前 Slice。

### 7.2 Done 条件

- public contract 和真实 implementation 同时存在；
- success、failure、cancel/timeout、shutdown 路径有证据；
- owner-owned instances/jobs/requests/subscriptions/leases 可归零；
- runtime closure 不含 Editor/tool/source-only implementation；
- diagnostics 带 owner/scope/generation/context，且查询无控制副作用；
- current docs 与 configured dependency evidence 同步；
- 旧 production path 已删除，或有一个明确且阻塞后续功能的删除 Slice。

## 8. 当前执行顺序

当前只允许两类工作并行：Foundation 主线，以及缩小错误边界的 Studio hard-cut。不得再开启第三条大型功能主线。

1. **Architecture facts/tooling**：删除文件和 Studio docs 门禁、manifest ↔ configured CMake direct dependency gate、
   canonical `Asharia.Studio.sln` 已覆盖当前全部 17 个项目（8 production + 9 tests）且旧 `Editor.sln` 与空public project/test已删除；下一小 Slice补 managed evaluated
   `ProjectReference` graph 与 public-consumer closure 对证。
2. **Studio R0 owner cutover（closed baseline）**：移除 legacy production composition 和 UI-thread sync wait；App 成为唯一 async process/session owner；
   建立唯一 bounded diagnostics ring、process clean-exit/teardown receipt、disposable-child exit-code negative、真实 Headless 壳状态
   和最小 accessibility semantics；删除没有真实consumer的App级UI registry/font/package closure，App只保留framework基础主题。
3. **Observability R0.5 Core v1（closed baseline）**：按
   [`studio-development-observability.md`](../../apps/studio/docs/architecture/studio-development-observability.md)
   暴露 session/state/diagnostics/logs 与真实壳 UI 的只读闭环；不依赖 Avalonia Plus，不做 metrics/trace/crash/Capture/远程写入。
4. **SceneDocument R1 headless read slice**：typed `DocumentId`、revisioned immutable snapshot、open/close/stale revision，
   以 in-memory adapter 验证成功与失败；不含 intent/receipt/undo。
5. **Native R1 session/read slice**：显式 handle/epoch/owner thread、C ABI exception boundary、bulk snapshot、typed native fault
   projection 和 stale-handle negative smoke。
6. **Studio R1 real read slice**：open → revisioned snapshot → Hierarchy/Inspector 同 revision 只读投影 → close；production 无 fixture。
7. **R2 headless mutation contract**：typed intent + expected revision → receipt/change set → savepoint/undo journal，先证明失败原子性。
8. **Native R2 mutation slice**：validate-all/commit-all atomic batch、inverse data、uncertain outcome 与 failure injection。
9. **Studio R2 real write slice**：edit Transform → receipt → undo/redo → save/reload/close；随后删除旧 transaction/public SDK surface。
10. **真实 Host owner slice**：让一个最小产品 application 经过现有 ProcessScope/Host owner 完成 start → run → quiesce → reverse
   stop，不再只用 synthetic provider 证明 F2。
11. **Foundation F3 closure**：Platform/Storage/Settings/Memory/Tasks/Observability 的 Minimal/Runtime/Server/Tool headless smoke；
    此时才加入共享 metrics/crash baseline。
12. **最小 standalone product slice**：在 real Content/World/Render 闭环后完成 Build → Cook → Stage → Ready → graceful stop，
   再进入 Scripting 和 first-party system waves。

## 9. 成熟引擎依据与 Asharia 取舍

- Unreal Subsystems 把系统实例绑定到 Engine、Editor、GameInstance、World、LocalPlayer 等明确 lifetime；Asharia 采用 scope-owned
  lifecycle，但不采用全局 `GEngine/GEditor` 访问：
  <https://dev.epicgames.com/documentation/unreal-engine/programming-subsystems-in-unreal-engine>
- Unreal Modules 明确 build dependency、host type 和 loading phase；Asharia 采用 target/module/Host Profile 分离，但只在依赖要求时
  提前加载，不把 loading phase 当任意作者字符串：
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules>
- Godot 将 Core、Main、Server、Scene、Driver/Platform 分层；Asharia 采用最小 Kernel + Host lifecycle + systems/adapters，
  不复制 singleton server API：
  <https://docs.godotengine.org/en/stable/engine_details/architecture/godot_architecture_diagram.html>
- O3DE System Components/Gems 提供 activate/deactivate 与 dependency order；Asset Pipeline 区分 source/product/runtime；Atom RPI 区分
  Scene、Pipeline、View、Feature Processor 和 RHI。Asharia 采用这些 owner/data separation，不复制 EBus/global RPI singleton：
  <https://docs.o3de.org/docs/user-guide/programming/components/system-components/>
  <https://www.docs.o3de.org/docs/user-guide/assets/>
  <https://docs.o3de.org/docs/atom-guide/dev-guide/rpi/rpi/>
- Bevy `App/SubApp` 和 render extraction 证明 simulation/render 可以用显式 read-only extraction 与独立 schedule 解耦；Asharia 保留
  C++ package-first/immutable snapshot 路线，不因此提前重写 ECS：
  <https://docs.rs/bevy/latest/bevy/app/struct.SubApp.html>
  <https://docs.rs/bevy/latest/bevy/render/struct.Extract.html>
- Unity PlayerLoop 证明 Input、Physics、Script、Animation、UI、Render、Profiler 需要明确 phase，且替换默认 loop 会遗漏已有系统；
  Asharia 因此采用插入式 Host phase/safe point 和完整 closure 验证，不允许每个 package 自建 update loop：
  <https://docs.unity3d.com/6000.4/Documentation/Manual/player-loop-customizing.html>
- Unity/O3DE 的 Profiler、Asset Processor 和 product asset 经验支持“常驻低成本计数器早接入，高成本捕获/后台 farm 后接入”：
  <https://docs.unity3d.com/6000.4/Documentation/Manual/Profiler.html>
  <https://www.docs.o3de.org/docs/user-guide/assets/asset-processor/>
  <https://www.docs.o3de.org/docs/user-guide/assets/pipeline/product-assets/>
