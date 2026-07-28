# ADR-0005：采用隔离构建、generation reload 与 last-known-good

状态：Accepted

日期：2026-07-11

依赖：[ADR-0004：采用统一 Editor Extension Framework](0004-unified-editor-extension-framework.md)

## Context

统一 Editor Framework 允许项目和 Package 中的 C#、Avalonia/XAML 在 Studio 进程内运行。生产路径必须解决：

- Windows、Linux、macOS 使用同一构建入口；
- 编译/restore 不能阻塞 UI thread；
- 插件私有依赖可以隔离，共享 Editor/Avalonia contract 保持同一 CLR type identity；
- 新代码编译、加载或激活失败不能破坏当前可工作的编辑器；
- reload 后不能残留 Panel、Control、delegate、task、provider 或 native resource；
- `.NET` 程序集卸载是 cooperative，而不是强制安全边界。

直接在默认加载上下文加载 DLL 无法卸载且会全局化 dependency conflict；在原 module object 上 patch 行为无法证明生命周期正确；先卸载旧版本再构建新版本会让一次普通编译错误破坏当前 workspace。

## Decision

### Isolated build

- `.asmdef` 或隐式项目模型转换为 cache 中的 SDK-style `.csproj`；
- 每个 Package 额外生成 aggregate host project，统一产生 `.deps.json` 与 private/native asset table，作为唯一 `AssemblyDependencyResolver` 入口；
- 使用独立 `dotnet build` process，参数通过 `ProcessStartInfo.ArgumentList` 传递；
- artifact path 以 Package generation、目标 RID 和完整 input fingerprint 分代；
- fingerprint 包含 source、`.asmdef`、Package lock、Editor API、SDK、analyzer/source generator 和 RID；
- generated project 使用 canonical source root、`PathMap` 和 deterministic/CI build setting，不把 checkout/cache 绝对路径写入 artifact；
- raw build product 在 candidate publication 或任何 assembly load 前，必须通过不执行代码的 PE/reference/PDB/dependency metadata inspection；检查只接受 build credential 和 current raw-output lease 已声明的文件与 identity；
- 通过检查的四个 product 只能由 publisher 从 current raw-output lease 重新检查后，以 BCL bounded stream 复制并复验到全新、互不重叠的 staging root；`artifact.json` 与四个 product 构成 exact closed tree，完成后以一次 directory rename 发布；
- 该 immutable publication 是 path-free、content-addressed build evidence，不是 loadable generation，不拥有 module index、`current`/`latest`、active/LKG 或 ALC；
- 独立 module indexer 只消费 typed publication receipt，在扫描前后复验 exact closed tree，并用 BCL metadata 同时读取 implementation/reference assembly；它只接受 exact `Asharia.Editor` `EditorModuleAttribute` 与受限 direct `EditorModule` type shape，要求两份 declaration surface 一致，输出 path-free、content-addressed in-memory facts；空 index 合法但不代表 load eligibility；
- staging candidate admitter 不接受 caller-supplied index；它从 publication receipt 重建 index，只为 non-empty current surface 签发 content-addressed receipt，并提供重新索引的 current check。publication root 只作为进程内 locator，不参与 candidate identity；receipt 不选择 ALC host，也不证明 managed reload eligibility；
- host policy selector 只消费 current staging candidate，并在任何 load/ALC 创建前签发 path-free policy receipt。当前 v1 是 external-build、缺少 resource/native/global-side-effect 与 cooperative-unload evidence，因此所有 activation/handover 组合都固定为 `Pinned + RestartRequired`；`Handover` 只表达替换时序，不能单独升级为 Collectible。selector 不加载或执行 assembly，后继 loader 仍须重新验证 policy/candidate currentness；
- pinned load-image builder 只消费 current `Pinned + RestartRequired` policy，在读取前后复验 policy，并把 publication 中 exact implementation DLL 与 portable PDB 读入有界、快照自有的只读字节；image identity 只绑定 policy 与两文件 size/hash，不绑定 locator。builder 用 BCL PE metadata 拒绝 global `<Module>` `.cctor`，因为 CLR load 会执行 module initializer；该快照不创建 ALC、不加载/执行 assembly，也不等于 loaded generation；
- pinned assembly loader 只消费 current load-image，并以 loader-owned project reservation 串行跨过首次不可逆 load。它创建 path-free、non-collectible custom ALC，以 implementation/PDB stream 只加载 exact root assembly；same project/same image 幂等复用，different image 或 ALC 创建后的失败均要求进程重启。ALC dependency hook 只返回 `null` 以共享已验证的 Default Host/framework closure，不探测目录/private/native assets；host receipt 只固定 Assembly/ALC/runtime identity，不解析 module type，也不 Configure/Activate 或推进 active/LKG；
- pinned module type resolver 只消费 pinned assembly host 及其内嵌 exact module index。它按 index 顺序对 root Assembly 做 case-sensitive full-name lookup，并复核 exact Assembly、full name、public top-level sealed non-generic concrete direct `EditorModule` shape 与 public parameterless constructor presence；receipt identity 只绑定 host/index。resolver 不枚举 assembly、不读取或实例化 attribute、不调用 constructor/Configure/Activate，也不推进 registry/active/LKG；
- pinned module constructor 只消费 exact module-type set，并由显式 owner 以 per-project reservation 串行首次用户代码执行。它按 index 顺序调用 receipt 固定的 exact `ConstructorInfo.Invoke(null)`；same type-set 重复/并发调用复用同一 result/object，different type-set 或 constructor failure 要求重启。失败 reservation 保留已构造的 partial objects 且不重试；该阶段会执行目标 type/instance constructor，但仍不实例化 attribute、不 Configure/Activate、不写文件或推进 registry/active/LKG；
- pinned module configurator 只消费 exact constructed-module set，并以独立 per-project reservation 按 index 顺序为每个 object 建立 `EditorModuleBuilder`、调用一次 `Configure()`、再 `Build()` immutable declaration。metadata 只投影 exact index entry；same construction lineage 幂等复用同一 declarations，different lineage 或 Configure/Build failure 要求重启。失败 reservation 保留原 objects 与 partial declarations 且不重试；该阶段不重构 object、不读取 attribute、不 Activate，也不创建 shared definition/registry/current/active/LKG；
- pinned module definition set 是对 exact configured receipts 的纯内存投影。共享 `EditorModuleDefinition` 直接持有 metadata/module/declaration，不再反向持有 static registration/factory；set 保留 exact 顺序与 definition-id lookup。该阶段不执行用户代码，也不创建 scope transaction、registry partition 或 activation；
- pinned module scope preparer 只接受未来 ProjectSession 显式提供的 Project `ScopeInstanceId` 与 host-capability value snapshot；它不把 persistent ProjectId 当 session identity。preparer 复用 `EditorScopeTransaction.Prepare` 对 exact definitions 构建不可见 candidate，并将 structural failure 转为 typed diagnostic；该阶段不 Commit registry、不 reservation，也不 Activate；
- initial Project scope committer 只消费 exact preparation，并在 captured registry snapshot 仍有效且目标 scope 为空时首次提交 candidate。成功 receipt 是绑定 exact partition reference 的 registration owner；关闭时幂等 compare-and-remove 自己的 partition，发现 successor replacement 时 fail closed。stale、已有 scope 或重复消费返回 path-free conflict 并要求重新 Prepare；该阶段不 Activate、不推进 current/active/LKG，也不实现 replacement/revision/rollback observer；
- build diagnostic 结构化投影到 Problems/Console；
- `FileSystemWatcher` 只触发 debounce，重新计算 fingerprint 才决定是否构建；
- 构建期间输入再次变化时取消或丢弃旧结果。

这里采用 Unreal `FBuildProduct` 的 typed build-product receipt 与后续 stage/validation 分界，并采用 Godot
managed tooling 中 build orchestration 与 assembly reload 分离的 owner 边界。当前 Asharia implicit Slice
只有一个 credential-bound assembly，不复制 Unreal 的 native product taxonomy，也不引入 Stride
AssemblyProcessor/Mono.Cecil 式 IL scan/rewrite。当前身份、引用闭包、reference marker、PDB/deps 与 module
declaration 验证都由 .NET BCL `PEReader`/`MetadataReader` 完成，保持无第三方依赖、无执行路径。

### Identity and reload unit

- `EditorAssemblyId` 是 `(PackageName, AssemblyName)`，不是 CLR simple name；
- `EditorModuleDefinitionId` 使用显式 stable `ModuleLocalId`，不使用可重命名 CLR type name；`EditorModuleInstanceId` 再加入 Application/Project `ScopeInstanceId`；
- Package identity 复用 `asharia.package.json.name`；项目根 `Editor/` 使用 reserved `project:<canonicalProjectId>:editor` name、`0.0.0-project` version sentinel、logical project source 与 canonical Editor source-set content hash；zero-config implicit AssemblyName 由 lowercase canonical UUID text 的 SHA-256 first-128-bit hash 派生，不随项目显示名/路径/UUID 大小写变化；
- 默认 load/reload unit 是 Package generation，一个 Package 的 managed assembly 和 private dependency 进入同一 ALC；
- 跨 Package reference 必须同时出现在 Package dependency 与 assembly dependency graph；
- process-resident Editor entry/aggregate assembly 与 global-resource assembly simple name 必须按大小写不敏感比较进程内唯一；reservation 仅在 Collectible unload probe 证明消失后释放，Pinned/Static/Leaked 到进程退出仍占用；纯代码 private dependency 可在隔离 ALC 重名；Code-first old/new staging 只对同一 logical assembly 例外，Avalonia experimental reload 使用 generation-unique physical name/exact routing。

每个 Package generation/definition 只有一个 immutable、thread-safe `EditorModule` object，并且只 Configure 一次；每个 `EditorModuleInstanceId` 有独立 activation lease。Required module/capability descriptor 形成 scope-aware activation graph；Project→Application 允许，Application→Project 禁止，Engine capability 是带 Epoch 的 Host node。暂时 unavailable capability 产生 Waiting/Blocked descriptor，不赋予 extension 阻止 Project open 的权限。

### Package resolution and lock transaction

- `asharia.package.json.dependencies` 保留现有 string entry 作为 workspace/catalog-local dependency，并增加 `{ name, version, source? }` object entry；可发布外部依赖必须给出 Asharia Package SemVer range；
- resolver 对同一 PackageName 求全部 range 交集，只使用 approved logical source；同 version 不同 source/hash 不设静默 precedence；
- managed/NuGet reference 由 Resolve/Update 写入 Asharia lock，restore graph 中每个 generated assembly/aggregate project从它确定性生成 project-scoped `packages.lock.json`，普通 Studio/CI restore 强制 locked mode，并将 aggregate `.deps.json` 反向校验到 Asharia lock；
- Application/Project catalog 的 resolved lock、精确 PackageGeneration 集合、artifact hash、dependency closure 与 registry generation 是同一个 versioned transaction；
- source 与 prebuilt variant 由 lock 在 Package-wide 粒度二选一；prebuilt manifest 必须覆盖全部 Editor assembly、aggregate host、统一 `.deps.json` 与 asset table，禁止逐 assembly 混合；
- active Application 或其他 Project Package 只有在请求完全相同 PackageGenerationId、artifact selection、managed/native subgraph 与 closure hash 时才可复用；共享 generation 被其他 catalog lease 时，普通单 Project update 返回 `PackageGenerationInUse`，不并行加载或隐式改写其他 lock；
- managed update 先写 immutable prepared snapshot，只有整个 reload closure required activation 和 registry commit 成功后才推进 durable active pointer/规范 lock；crash recovery只能选择完整 current 或 previous snapshot，不能混合新 lock 与旧 LKG artifact；
- restart-required update 不在当前进程执行 candidate code，而是持久化 `PendingRestartCatalogSnapshot`。下次 clean boot 先写 BootAttempt marker、只加载 candidate，成功才提交规范 lock/LKG；失败进程立即退出，下一次 relaunch previous committed snapshot。Application pending 只激活当时存在的 Application scope，Project-only definition configure/validate 后可提交；Project pending 必须在目标 ProjectSession/Engine context 中激活该 scope 再提交。未完成 attempt/crash 自动判失败，candidate 自动尝试至多一次，避免污染进程混载与 crash loop。

### Load context

- managed-reload-eligible dynamic generation 使用 `CollectiblePackageGenerationHost`；已知 Avalonia Tier-0/native/external-build 等 restart-required generation 使用隔离、non-collectible `PinnedPackageGenerationHost`，close 后保留并复用 exact generation；App runtime reference 的 BuiltIn 使用 default-ALC `StaticPackageGenerationHost`，禁止重复加载；
- 使用 Package aggregate host component 的唯一 `AssemblyDependencyResolver` 与验证后的 asset table 解析 private managed/native dependency；
- `Asharia.Editor`、`Asharia.Editor.Avalonia`、Host Avalonia compatibility band 和明确公开的 framework assembly 从默认上下文共享；
- shared contract 使用 Host full-identity allowlist，不接受扩展通过名称通配伪装共享 assembly；
- 扩展私带共享 contract 或不兼容 Avalonia version 时，在执行 extension code 前拒绝加载；
- cross-Package compile reference 不复制到 dependent artifact；CLR request 在 private ADR 前由 Host assembly table 按 `EditorAssemblyId` 返回 dependency ALC 中的精确 `Assembly`，imported/private identity ambiguity 直接拒绝；
- 跨 Package CLR dependency 启动 dependencies-first，停止/unload dependents-first；dependency generation 变化重载全部 dependents closure；
- `PackageGenerationHost` 是 ALC/module/assembly table 的 owner；Application/Project scope、registry/factory、Panel/UI、task 与 dependency lease 全部归零后才能 retire，只有 Collectible host 调用 unload；
- host type 在执行 extension code 前按最严格 artifact/UI/native policy 选择；Pinned host 保留精确 transitive dependency generation lease 到进程退出，依赖更新同样要求 restart；
- native library 或无法证明可卸载的模块标记 `restart-required`。

当前 implicit Project Code 实现了该选择、load-image preflight 与 exact pinned binary residency 边界：UE 也把 module
descriptor/current-configuration eligibility 与
`FModuleManager` 的 binary load/initialization/unload 分开，O3DE 在 `ModuleManager` load 前显式选择初始化终点
和是否 maintain reference；.NET 则在 ALC 创建时固定 collectible 属性。Asharia 因而不允许 binary loader 再按
运行时猜测改变 #315 已签发的 residency/replacement policy，也不允许把会在 CLR load 时执行 global
module initializer 的 binary 当作无执行快照通过。#316 固定并复验 exact bytes；#317 只将该 image 装入
non-collectible ALC 并保持引用；#318 再把 #313 exact index 对证为 runtime `Type` receipt，但不构造任何对象。
#319 在显式不可逆边界按 exact constructor receipt 至多一次地构造 module object，并把 failure 固定为
restart-required；#320 再按 exact object/index 至多一次地执行 Configure 并冻结 declaration/metadata receipt。
#321 将这些 receipts 投影为 static/dynamic 共用的 module definitions，但不进入 registry。#322 再在
caller-supplied ProjectSession scope 下完成 combined structural validation 与 invisible candidate；
#332 完成 empty-scope initial registry commit 与 exact registration retirement ownership；#333 再把该
registration 一次性转交给独占异步 activation owner，复核 exact host-capability ID 集合，并在 `Faulted`、
取消或 Host failure 时按 activation-first 顺序清理。正式 ProjectSession composition、replacement 与
catalog commit 仍是后继边界。

### Generation replacement

每次有效构建创建新的 immutable artifact 和 module generation：

```text
build candidate closure
  -> staging ALC
  -> module index/configure
  -> compatibility/contribution validation
  -> activate early Coexist graph dependencies-first
  -> save UI-neutral state
  -> quiesce old graph dependents-first
  -> activate QTA + delayed dependent graph dependencies-first
  -> atomically switch registry generation
  -> detach/dispose/unload old generation
```

Required set 覆盖该 generation 正在服务的 Application scope 与全部 active ProjectSession：每个 `ScopeInstanceId` 的 `OnScopeReady` `EditorModuleInstanceId`，以及旧 generation 中已经 Active 的 instance。从未使用的 Dormant lazy instance 只 configure/validate，不在 staging 中强制激活。Staging validation 或 required candidate activation 失败不触碰旧 generation。Commit 前失败时释放整个 candidate closure，并调用旧 activation 的 `ResumeAsync()`。Reload closure 内任一必要 dependency/instance 失败时，不提交任何 registry partition。

Registry generation 可以原子切换；任意 filesystem、network、native 或其他外部副作用不能由 Host 自动事务化。Managed module 必须选择经 Host 验证的 `Coexist` 或 `QuiesceThenActivate` handover：前者在旧 generation quiesce 前激活 candidate；后者供 singleton/exclusive role 使用，先可逆 quiesce 旧实例，再激活 candidate，失败时 dispose candidate 并 Resume 旧实例。QTA 沿 required activation dependency graph 向 dependents 传播，禁止 candidate 混用旧 dependency service；rollback 按 dependents-first dispose candidate、dependencies-first Resume 旧 graph。不能确定性 Resume 的独占资源为 `restart-required`。

如果实际 `ResumeAsync()` 仍失败，registry/active pointer 不推进，但旧运行图已经无法完整恢复；Host 必须进入 Degraded + restart-required并明确报告，不能把 artifact LKG 等同于当前进程已恢复。

### Last-known-good

每个 Package generation 保存最近一次完整通过以下阶段的 artifact identity；每个 Application/Project catalog 另保存包含完整 resolved lock 与 generation closure 的 snapshot identity：

```text
build + load + configure + validate + required activation + registry/catalog commit
```

只有 commit 成功后才能推进 last-known-good pointer。LKG 证明当时所有 active scope instance 的 required activation，不证明未来 scope 或 Dormant lazy module 能成功激活；commit 后第一次 lazy activation failure 只 fault/禁用该 module instance/contribution，不回滚已提交 catalog。Cache 清理不能删除 current active、pending restart、previous committed snapshot 或 last-known-good artifact。

### Avalonia/XAML

Avalonia/XAML module 默认 `restart-required`。原因包括：

- compiled XAML 和 `avares://` resource resolution 依赖 assembly identity；
- asset/resource loader 可能按 simple assembly name 缓存旧 generation；
- custom Control/property registration 和 static style/event 可能形成进程级强引用；
- old/new 同名 assembly 并存时不能假设资源会解析到 candidate。

在当前 Avalonia 12 compatibility band，定义自有 AvaloniaProperty/DirectProperty、RoutedEvent、custom/templated Control、TopLevel/NativeControlHost 或全局 style/resource 的 extension 强制 `restart-required`。

Tier-0 generation 首次加载即进入 pinned host。Project close terminal dispose 全部 UI/module scope，但不调用 `Unload()`；同一 lock 重开时复用，source/lock 变化等待进程重启，禁止反复创建注定泄漏的 ALC。

只有 Avalonia backend 同时提供并验证以下能力后，某个 compatibility band 才能开放 managed reload：

1. generation-aware resource resolution/cache invalidation；
2. 旧 Control 从 visual/logical tree 完整 detach；
3. extension style/resource/event/subscription 清理；
4. 对旧 module Control type 的全局 Avalonia property/type registry 注销；
5. ALC weak-reference negative leak probe。

### Unload verification

调用 `AssemblyLoadContext.Unload()` 只启动协作式卸载，且只用于 Collectible host。Host 释放全部 scope、清理 factory/assembly table 与 Avalonia physical resource descriptor/cache 后，通过 weak reference 和有界 GC probe 验证旧 context 是否消失。

无法卸载时：

1. 记录 Package/module/generation 和可能 retained owner；
2. 标记 reload unit `restart-required`；
3. 停止继续创建新 generation，避免无界泄漏；
4. 保持 Studio 其他模块运行；
5. 不把“调用过 `Unload()`”报告成成功卸载。

### Trust

ALC 只提供 dependency grouping 和 cooperative unload，不提供恶意代码、process crash、CPU/memory 或 BCL access 隔离。进程内 extension 是受信任代码；不可信 extension 需要未来 OS process/container host。

支持动态 extension 的 Studio desktop distribution 使用 CoreCLR/JIT。Native AOT 不支持动态 assembly loading；若未来提供 AOT profile，它只能加载构建时静态已知的 extension，并且是独立 capability profile。

## Alternatives

### Load every extension into the default context

Rejected。无法卸载，dependency version conflict 全局化，一次加载失败污染整个进程。

### Compile in-process with Roslyn as the production source of truth

Rejected。需要重新实现 MSBuild/NuGet/Avalonia XAML/analyzer/source generator 行为，难以与普通 IDE/CI 构建一致。Roslyn 可以用于诊断或未来快速预览，但不是权威 build。

### Stop old version before building candidate

Rejected。普通语法错误或 dependency restore failure 会关闭当前可工作的工具，无法提供 last-known-good。

### Claim fully atomic hot reload

Rejected。Exclusive provider、native resource 和 arbitrary side effect 不能在所有情况下与旧 generation 并行。框架只承诺 staging validation、registry generation commit 和有条件 rollback。

### Continue reloading after an ALC leak

Rejected。会累积 assembly、Control、static event 和 native dependency，最终破坏整个 Studio 进程。

## Consequences

Positive：

- 构建行为与 CLI/CI 一致并天然跨平台；
- 编译错误不会破坏当前 workspace；
- dependency conflict 局部化，公共 contract 保持同一 type identity；
- reload、rollback、leak 和 restart-required 都可观察；
- 为项目 `Editor/` 提供快速迭代，同时保持明确生命周期。

Negative：

- build cache、fingerprint、artifact pinning 和清理策略增加实现成本；
- activation rollback 仍需要 module 正确遵守 coexist/quiesce/resume contract；
- Avalonia global/static reference 和 native dependency 容易阻止卸载；
- 复杂 XAML 与 native module 默认需要重启，不能承诺全部热重载；
- dependency closure reload 比单 DLL reload 更复杂。

## Validation

- 编译失败继续运行 active generation；
- load/configure/validate/candidate activation failure 不改变 registry；
- legacy/object Package dependency constraint、source/hash ambiguity 与 NuGet locked restore 可确定性验证；
- `asharia-comparator-set-v1` 在 Studio/CLI/native tooling 使用同一 SemVer/prerelease conformance fixture；
- synthetic project identity、Package-wide source/prebuilt selection 和 exact Application/Project generation reuse 可重复验证；
- lock + PackageGeneration closure 只作为完整 catalog transaction commit/rollback，prepared crash recovery 不产生混合 graph；
- PendingRestart clean-boot success、candidate failure immediate exit、BootAttempt crash recovery 和 previous-snapshot relaunch；
- Application-catalog Project-only/mixed Package 在无 Project 启动，以及 Project-catalog target-session scope-aware pending commit；
- commit 前失败恢复旧 activation；
- dependency closure 不出现 partial generation commit；
- required Coexist/QuiesceThenActivate handover、Coexist-dependent → QTA-dependency 延迟传播与 Dormant lazy 首次激活失败分别验证；
- mixed Application + 多 ProjectSession replacement 覆盖全部 `EditorModuleInstanceId`/registry partition；
- stable ModuleLocalId 在 CLR type rename 后保持匹配；definition 只 Configure 一次且多 scope state 隔离到 activation lease；
- required/optional module-capability graph、ProjectScope combined-catalog transaction、Waiting/Blocked capability Epoch recovery；
- process-resident Editor/Avalonia simple assembly name reservation 只在 proven unload 后释放；
- 同一 fingerprint 在不同 checkout/cache root 产生相同 artifact hash；
- private dependency 不污染其他 extension；
- Collectible host 的旧 Panel/Control/event/task/provider/resource cache 全部释放后 ALC weak reference 消失；Pinned/Static host 则验证 scope/UI/lease 清零但预期驻留；
- Avalonia reload fixture 覆盖 resource cache、property registry 和 old Control type；
- leak fixture 进入 restart-required 且不继续增长 generation；
- Git checkout/批量文件事件通过 fingerprint 只产生正确一次有效构建；
- Windows、Linux、macOS fixture 使用相同 `.asmdef` 和 `dotnet build` pipeline。
