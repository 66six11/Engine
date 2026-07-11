# Editor 扩展构建、装载与重载规范

状态：Target（已批准，尚未实现）

更新日期：2026-07-11

## 1. 目的与权威范围

本文是项目 `Editor/` 和 Package managed extension 的 production build/load contract。它定义输入身份、generated project、artifact、Package lock、ALC dependency resolution、generation replacement、last-known-good、缓存和诊断。

Authoring 入口见 [Editor 扩展开发模型](editor-extension-authoring.md)，决策理由见 [ADR-0005](../adr/0005-managed-editor-module-build-and-reload.md)。本文比示例代码更具实现约束力。

## 2. Identity model

```text
PackageIdentity
  Name
  ResolvedVersion
  Source
  ContentHash

EditorAssemblyId
  PackageName
  AssemblyName

EditorModuleDefinitionId
  EditorAssemblyId
  ModuleLocalId
  ScopeKind

EditorModuleInstanceId
  EditorModuleDefinitionId
  ScopeInstanceId (ApplicationId | ProjectSessionId)

PackageGenerationId
  PackageIdentity
  InputFingerprint
  TargetRid

CatalogSnapshotId
  CatalogKind (Application | Project)
  ResolvedLockHash
  PackageGenerationSetHash
```

规则：

- Package `Name` 复用 `asharia.package.json.name`；
- BuiltIn static generation 使用 Studio distribution manifest 中的 reserved PackageIdentity：`com.asharia.studio.builtin-extensions`、exact Studio product version、`studio-builtin` source 与 signed/bundled assembly content hash；不能用空 owner 或 App assembly path 代替；
- project root `Editor/` 先把 UUID `projectId` 规范为 lowercase canonical text，再使用 reserved synthetic identity：`Name = project:<projectId>:editor`、`ResolvedVersion = 0.0.0-project`、`Source = project:<projectId>/editor`、`ContentHash = Hash(canonical Editor/** source/resource/asmdef set)`；零配置 implicit `AssemblyName = Asharia.Project.<ProjectIdHash>.Editor`，其中 `ProjectIdHash = lowercase-hex(first128bits(SHA-256(UTF8(canonicalProjectId))))`；project display name 与绝对 checkout path 不进入 identity；
- Assembly 和 Module 不能取代 Package identity；
- `ModuleLocalId` 来自 `[EditorModule(Id = ...)]` 的显式稳定命名空间化 ID；CLR entry type 不参与 identity，缺失/重复/非法 ID 在生成阶段失败；
- contribution owner 必须记录 Package/Assembly/EditorModuleDefinition/EditorModuleInstance/PackageGeneration；Project-scoped registry 使用 `ProjectSessionId` 分区；
- 所有 process-resident Editor entry/aggregate assembly，以及通过 Avalonia/resource/tooling global simple-name lookup 可达的 private resource assembly，其 CLR simple name 必须按 case-insensitive comparison 在进程内全局唯一；`EditorAssemblyId` 只解决逻辑寻址；
- name 是 resident reservation：只有 Collectible host 的 unload probe 已证明旧 assembly 消失后才释放；Pinned、Static、Leaked 或仍在 Retiring/Unloading 的 generation 一直到进程退出/成功 probe 都继续占用；
- 纯代码 private dependency 可以在彼此隔离的 ALC 中重名。Code-first replacement staging 只允许同一 `EditorAssemblyId` 的 old/new generation 临时同名，不能把该 name 分配给无关 Package，且它们不得进入 global simple-name lookup；Avalonia experimental Tier 1 必须使用 generation-unique physical assembly name 和 exact resource routing；
- 所有持久化 layout/state 只保存 logical contribution ID 与 `EditorModuleDefinitionId`，不保存 `EditorModuleInstanceId`、generation path 或 CLR object。

## 3. 输入模型

权威输入：

```text
asharia.project.json
project root Editor/**
asharia.package.json
*.asmdef
asharia.packages.lock.json
source files and Avalonia resources
optional Package-wide prebuilt Editor artifact manifest
Studio SDK manifest
target RID
```

FileSystemWatcher event、生成 `.csproj`、`obj/`、build output 和 loaded assembly 都不是 source of truth。

完整 input fingerprint 至少包含：

- normalized source/resource relative path 与 content hash；
- `.asmdef` canonical JSON；
- resolved Package lock entry；
- Studio SDK、Editor API、Avalonia bridge、analyzer/source generator versions；
- TFM、C# language version、configuration 和 RID；
- relevant build policy/schema versions；
- direct/transitive managed package lock and restore-source policy。

Fingerprint 序列化必须与 OS path separator、目录枚举顺序和本地绝对路径无关。

## 4. Generated project contract

隐式 `Editor/` 和每个普通 `.asmdef` 都转换为 Studio cache 中的 SDK-style library project。Build service 还为每个 Package generation 生成一个内部 aggregate host project，它引用该 Package 的全部 Editor assembly project，产生一个统一 Package `.deps.json` 和 private/native asset table。ALC 的 `AssemblyDependencyResolver` 以这个 aggregate host component 为入口，不能任选第一个 module assembly。当前 compatibility band 固定：

```text
SDK: Microsoft.NET.Sdk
OutputType: Library
TargetFramework: net10.0
LanguageVersion: C# 14 compatibility band
Nullable: enable
Deterministic: true
DebugType: portable
EnableDynamicLoading: true
GenerateDependencyFile: true
```

Studio SDK manifest 锁定具体 .NET SDK 和 analyzer/source generator version；不得使用机器上任意“最新 SDK”改变同一 lock/fingerprint 的输出。

Generated project 还必须：

- 使用独立 `BaseIntermediateOutputPath` 和 immutable artifacts path；
- 使用 canonical logical source root、`PathMap`/deterministic source-root 和 CI deterministic build setting，确保 checkout/cache 绝对路径不进入 DLL/PDB/resource；同一 fingerprint 在不同 checkout root 必须产生相同 artifact hash；
- 为 Source Generator 提供 `.asmdef`、Package/Assembly identity 和 module-index schema；
- 生成包含 entry type、无参数 definition factory、stable `ModuleLocalId`、scope、activation/handover policy、API/schema 和 `EditorModuleDefinitionId` 的 module index；`EditorModuleInstanceId` 由 Host 按 scope instance 创建，不依赖目录反射扫描作为 production protocol；
- Code-first module 不引用 Avalonia package；
- Avalonia backend 加入 pinned Avalonia XAML compiler/resource glob；
- Host-shared reference 只作为 compile asset，不复制到 artifact runtime closure；
- 已声明的 cross-Package `EditorAssemblyId` reference 同样只作为 compile asset/non-copy reference；dependent aggregate `.deps.json` 和 private asset table 不得包含 dependency Package 的 Editor assembly；
- extension-private dependency 完整进入 `.deps.json` 和 artifact；
- `.asmdef.managedPackageReferences` 通过 Project/Studio-approved feeds restore，并产生锁定的直接/传递依赖；Asharia lock 必须为 restore graph 中每个 generated assembly/aggregate project 确定性 materialize project-scoped `packages.lock.json` 并强制 `RestoreLockedMode`；
- Package-wide restore 对重复 simple assembly/native library、版本或 RID asset 冲突直接失败，不定义“最后一个获胜”；
- 产生每个 module DLL/PDB/index/resource，以及 aggregate host DLL、统一 `.deps.json`、private/native asset table 和 build metadata；
- 禁止 Studio host native copy target、Windows manifest 或 app OutputType 泄漏到 extension build。

共享 API 引用在生成 project 中等价于 `Private=false`/不复制 runtime asset。Loader 不能仅依赖 build output 是否碰巧包含 DLL；它仍要主动拒绝 extension 私带的 shared contract。

外部自定义 `.csproj` 必须由 `asharia.package.json.editor` 显式声明，视为受信任 external build，默认 `restart-required`。Host 记录实际 project、SDK、binlog 和 artifact，但不把它伪装成标准可重复 `.asmdef` build。

分发 Package 可以提供 Package-wide prebuilt artifact manifest，包含全部 logical assembly identity、TFM/RID、API/schema compatibility、module DLL/PDB/index/resource、aggregate host/统一 `.deps.json`、private/native asset table 和 content hash。`asharia.packages.lock.json` 为整个 Package generation 选择 `source-build` 或一个 prebuilt artifact ID；禁止逐 assembly 混合 source/prebuilt 后临时合成 closure，也不允许从目录中猜测散落 DLL。Manifest 同时提供 source 与 prebuilt variant 时，Project policy 决定可选集合，但实际选择必须被 lock 固定。

## 5. Build process

```text
resolve graph
  -> compute fingerprint
  -> cache lookup
  -> dotnet restore/build in child process
  -> collect diagnostics
  -> validate immutable artifact
  -> publish candidate generation
```

要求：

- 使用 `ProcessStartInfo.ArgumentList`，不通过 platform shell 拼接命令；
- restore/build 不在 UI thread 运行；
- process、stdout/stderr、binlog 和 cancellation 属于 build task scope；
- 同 Package generation 新 fingerprint 到达时取消旧 build；无法及时取消则丢弃旧结果；
- build output 先写临时 generation directory，完成校验后原子 rename/publish；
- published generation directory 永不原地修改；
- Problems/Console 诊断包含 Package/Assembly、文件、范围、code、severity 和 fingerprint；
- build failure 不修改 active 或 last-known-good pointer。

## 6. Package lock 与安装

`asharia.package.json` 是 Package 描述，不是已解析安装状态。`dependencies` 接受向后兼容的两种 entry：

```json
[
  "com.asharia.core",
  {
    "name": "com.example.brushes",
    "version": ">=2.1.0 <3.0.0",
    "source": "asharia-official",
    "allowPrerelease": false
  }
]
```

- legacy string 等价于 workspace/catalog-local dependency：只能绑定当前 workspace 或随 Studio 发布的唯一同名 Package，随后由 lock 固定其 version/source/hash；它不能从远程 registry 随意选择“最新版本”；
- object entry 的 `version` 是 Asharia Package SemVer range；`source` 是 Studio/Project policy 预先定义的 logical source ID，不允许 manifest 注入 URL 或 credential；
- 可发布 Package 的外部依赖必须使用 object entry 给出 version range；legacy string 保留给现有仓库内 dependency；
- 显式 Resolve/Update 对同一 PackageName 求所有 range 的交集，只考虑 approved source；已有 lock candidate 若仍满足约束则默认保留，否则在明确 update policy 下选择最高稳定 compatible version；同 version 不同 source/hash 没有静默 precedence，必须由 source constraint/policy 唯一化或报告冲突；
- 同一 Application + Project process graph 每个 PackageName 只能有一个 resolved version/source/hash。约束交集为空、同名内容歧义或 active Application generation 不匹配均是 resolve failure。

Asharia Package range 的 normative grammar 是 `asharia-comparator-set-v1`：一个或多个以单个 ASCII space 分隔的 comparator；comparator 是可选 `= | > | >= | < | <=` 加 SemVer 2.0.0 exact version，全部按 AND 求交集。裸版本等价于 `=`；v1 禁止 caret、tilde、OR、wildcard。SemVer build metadata 不参与 precedence；prerelease 默认排除，只有 entry 显式 `allowPrerelease: true` 才纳入。Package/source ID 必须是 lowercase ASCII canonical form，SemVer prerelease identifier 维持大小写敏感。所有 resolver/tooling 以 lock 中记录的 grammar/schema version 和同一 conformance fixtures 为准，不支持就 fail closed。

系统使用同一 schema 的两个 lock catalog：

- Studio/Application catalog lock：随 Studio installation 加载，并记录所有用户启用、从全局 Installed source 发现的 Package；Package 可以只含 Project-scoped module，catalog ownership 不由 module scope 推导；
- Project catalog lock：项目根提交的 `asharia.packages.lock.json`，记录该 Project 的 Package/Editor/native/managed graph。

两者至少记录：

```text
package name
resolved version
source/repository identity
content hash
signature/provenance when available
dependency edges
selected RID assets
editor assemblies
native artifacts
managed/NuGet package versions, sources, hashes and selected assets
```

规则：

- CI、其他开发机和 Studio 使用同一 lock 解析结果；
- Application-scoped module 只能来自 Studio/Application catalog lock；Project root/source catalog 不能在 Project open 时偷偷提升为 Application scope；
- 打开/更新 Project 时，resolver 把 active Application catalog 与所有 active Project catalog 的 Package generation 都当作精确约束；只有请求的 `PackageGenerationId`、artifact selection、managed/native resolved subgraph 和 closure hash 全部一致才复用。仅 PackageName/version/source/content hash 相同仍可能选择不同 NuGet/prebuilt/RID closure，不足以复用；
- 普通 Resolve/Update 不允许把某个 Project 升级到仍被其他 active catalog lease 的 Package 新 generation，也不并行加载两个版本。返回 `PackageGenerationInUse`，列出阻塞的 catalog/ProjectSession；用户关闭这些 session 后再更新。未来若增加显式 multi-catalog coordinated update，必须另立事务协议，不能暗中修改其他 Project lock；
- Package 安装到 content-addressed store，不原地修改 active package；
- 下载/解包/验证在临时目录完成，成功后原子发布；
- Resolve/Update 先创建 immutable candidate lock snapshot；下载、build 和静态 artifact validation 都只引用 candidate，不修改 committed/active lock；
- managed-reload-eligible closure 可以在当前进程 load/configure并取得 required activation outcome；只有完整 registry commit 成功后，才原子推进 catalog active pointer 与规范 lock 文件；
- 包含 Pinned/Tier-0/native/external-build 的 restart-required closure 不在当前进程执行 candidate code，而是写 durable `PendingRestartCatalogSnapshot`；其规范 lock/active pointer 只在下一次 clean boot activation 成功后推进；
- 保留 previous committed snapshot/generation 以支持回滚；
- lock、精确 PackageGeneration 集合、artifact hashes、dependency closure 和 registry generation 构成同一个 versioned catalog transaction；不能只推进其中一部分；
- crash recovery 只选择完整 committed current 或 previous catalog snapshot。普通 Prepared 但未提交的 snapshot 可清理；durable PendingRestart 按第 6.1 节保留。两者都不得把旧 LKG artifact 与新 lock entry 混合；
- offline 模式缺失锁定内容时明确失败，不静默选择其他版本；
- Project Package、用户 installed Package 和 built-in Package 使用同一 identity/provenance model。

`managedPackageReferences.version` 使用 NuGet VersionRange，但正常 build 不重新自由解析：显式 Resolve/Update 操作根据 approved source policy 解析 graph，写入 Asharia lock，并为 restore graph 中每个 generated `.asmdef`/implicit/aggregate project materialize 标准、project-scoped NuGet `packages.lock.json`。普通 Studio/CI build 对每个 project 设置 `RestorePackagesWithLockFile=true` 和 `RestoreLockedMode=true`；direct/transitive dependency、content hash 或 source mapping 漂移时 restore 必须失败并要求显式更新 lock。

Generated `packages.lock.json` 必须能从 committed Project lock 或 Application catalog lock确定性重建。Build 完成后还要把 aggregate `.deps.json` 的 runtime/native asset closure 反向校验为 Asharia lock 的精确子图。Build fingerprint 同时包含统一 lock entry、全部 materialized lock hash 和 source policy；不能先做 floating restore，再把结果事后记录成“锁定”。

Project lock 的规范文件使用 temporary write + fsync/flush + atomic replace；Application catalog 使用等价的 durable pointer。内存 registry commit 与持久文件不是硬件级同一事务，因此使用 prepared transaction record：进程崩溃后以 durable committed pointer 为准，结果只能是完整旧 snapshot 或完整新 snapshot，不能是混合 graph。

### 6.1 PendingRestart handoff

Restart-required update 使用以下 durable protocol：

1. 当前进程 resolve、下载、build，并只做不执行 extension code 的 artifact/index/hash/compatibility validation；
2. 把 candidate lock、完整 PackageGeneration closure、previous committed snapshot ID、artifact hashes 和 attempt state 写入 content-addressed `PendingRestartCatalogSnapshot`，flush 后原子更新 pending pointer；当前 registry/规范 lock 保持旧值；
3. 下次 clean boot 在加载任何相关 extension 前写入 durable `BootAttempt(candidateId)` marker，然后只加载 candidate closure，并按 catalog kind 建立下述 scope-aware staging；
4. candidate 对本次 commit 时全部 active scope instance 获得有效 required outcome（Active 或 soft-gate Waiting）且 registry commit 成功后，原子推进规范 lock/active/LKG pointer，清除 pending/attempt marker；
5. load/configure/activation failure 时记录 failed pending，并立即结束该进程；不能在可能已污染 Avalonia/global registry 的同一进程加载旧 LKG；
6. 下一次自动 relaunch（或检测到未完成的 BootAttempt/crash）忽略 failed candidate，加载完整 previous committed snapshot，并保留诊断；自动 candidate attempt 至多一次，避免 crash loop；
7. 用户可显式重试或删除 pending；任何路径都不能逐 entry 混合 candidate 与 previous snapshot。

Pending snapshot 是 durable handoff，不属于 active/LKG，也不能被普通 candidate GC 删除。CLI/CI 必须能检查 pending/committed 状态；规范 Project lock 只有成功 boot commit 后才成为可提交的新 source-of-truth 文件。

Scope-aware staging：

- Application-catalog pending 在无 Project 的 application bootstrap 中只创建/激活 Application scope；Project-scoped definition 完成 configure 与 definition-local validation，但跨 Project catalog 的 provider/conflict resolution 延迟到 ProjectScope transaction，没有 `ProjectSession` 就不实例化。Application catalog 可以因此提交一个只含 Project module 的 Package；以后首次创建 Project scope 的 `OnScopeReady` failure 是该 `EditorModuleInstanceId` 的 post-commit scope fault，不回滚 Application catalog；
- Project-catalog pending 必须在 clean process 中打开其目标 ProjectSession、启动所需 Engine capability，并在隐藏 staging registry取得该 Project scope 的有效 required outcome 后才提交 Project lock；失败遵循 exit + previous-snapshot relaunch；
- LKG 只证明 commit 当时存在的 scope instance。未来 ProjectSession、Dormant lazy instance 或当时未恢复的 workspace scope 不被虚假声明为已激活验证。

签名不是第一版强制安全边界，但 content hash、来源和版本必须可审计。

## 7. ALC dependency resolution

Dynamic Package generation 使用一个专属隔离 ALC，不使用整个依赖岛单 ALC；BuiltIn static generation 复用 default ALC。具体算法：

1. 按 Package/assembly dependency graph 进行无环校验和拓扑排序；
2. dependencies-first 创建并加载 Package ALC；
3. Host-shared full assembly identity 从默认 ALC 精确返回；
4. 对已声明跨 Package `EditorAssemblyId`，loader 先从对应 dependency Package generation 的 assembly table 返回精确 `Assembly` 实例；
5. 只有前两类均未匹配时，当前 Package 的 own/private assembly/native asset 才由基于 aggregate host component 创建的唯一 `AssemblyDependencyResolver` 和已验证 asset table 解析；
6. build/load validation 拒绝 imported cross-Package assembly identity 与当前 private asset 的 simple/full identity 歧义，不能用 resolver 顺序掩盖错误 artifact；
7. 未声明或 identity 不匹配的 cross-context request 失败，不回退扫描任意目录；
8. dependency generation 变化时，所有直接/间接 dependents 组成 reload closure；
9. managed collectible closure 按 dependents-first quiesce/unload，再按 dependencies-first 建立新 generation；closure 含 Pinned/Static generation 时转 PendingRestart，不在当前进程替换。

共享 assembly 使用完整 identity allowlist（name、public key token/host provenance、major band），不能用 `System.*`、`Avalonia.*` 字符串通配接受任意 DLL。BCL 使用 runtime/default resolution；Host 只列出其主动共享的非 BCL contract。

该算法确保 B 引用 A 时获得 A 的同一个 CLR `Assembly`，而不是在 B 的 ALC 再加载一份同名 DLL。

Module runtime topology 来自 `Configure()` 生成的 immutable required/optional dependency 与 provided-capability descriptor，不从 assembly reference 猜测：

- graph node 是 `EditorModuleInstanceId` 或 Host capability instance；
- required edge 参与 dependencies-first activation、dependents-first stop、failure closure 与 QTA propagation；optional edge 不参与拓扑；
- Project→Project edge 在同一 `ProjectSessionId` 展开，Project→Application 允许，Application→Project 拒绝；
- cross-Package module/capability edge 必须有对应 Package + assembly dependency；
- Project PendingRestart 先 configure graph，再请求 required Engine capability node，再执行 scope activation；
- required cycle、未声明/missing provider、ambiguous provider 和非法 scope edge 是 structural validation failure；已声明 Host capability 暂时 Unavailable/Recovering 则产生 `WaitingForCapability`，不是 graph invalid。

新 `ProjectSessionId` 需要独立 ProjectScope registry transaction：把 BuiltIn、Application-catalog Installed、目标 Project catalog 与 project-root definition 的 Project descriptor 合并到不可见 partition，验证 combined ID/role/dependency/capability graph，对 Ready chain 激活并为 unavailable/faulted chain 记录 Waiting/Faulted/Blocked 状态后一次发布。Structural failure 才销毁整个 scope staging；committed-catalog runtime activation fault 只隔离 module instance/dependent chain。Project pending candidate 自身 activation exception 仍使 pending attempt 失败；soft capability unavailable 可以提交为 Waiting。Project pending commit 与 partition commit 是同一个边界。

Host capability 带单调 Epoch。Capability lost/device recovery 使 dependent activation按 dependents-first quiesce并进入 Blocked；新 epoch Ready 后 dependencies-first Resume，或在旧 lease 不可恢复时重新 Activate。Extension capability gate不能阻止 Studio/Project Shell 打开；hard open gate只属于 Host infrastructure policy。

## 8. Generation state

```text
Common:      Discovered -> Building -> Built -> Loading -> Configured
             -> Validated -> StagedActive -> Active -> Quiescing -> Retiring
Collectible: Retiring -> Unloading -> Unloaded | Leaked
Pinned:      Retiring -> InactivePinned -> Active ... -> ProcessExit
Static:      Retiring -> InactiveStatic -> Active ... -> ProcessExit
Update:      Built/Validated -> PendingRestart -> BootAttempt
             -> ActiveCommitted | FailedPending -> PreviousCommitted
```

Package host 分为三种；同一 `PackageGenerationId` 在进程内只能由一种 host 持有：

- `CollectiblePackageGenerationHost`：用于通过 managed reload eligibility 的 Code-first/Tier-1 dynamic generation；retire 且所有 lease 归零后执行 ALC unload probe；
- `PinnedPackageGenerationHost`：用于 Avalonia Tier-0、native/external build 或其他 `restart-required` dynamic generation；使用隔离但 non-collectible ALC，Project close 时 dispose scope/UI 后保留 exact generation 到进程退出，重新打开复用同一 host；新 generation 只能 build 并显示 diagnostics，必须重启后加载；
- `StaticPackageGenerationHost`：用于 App runtime reference 的 BuiltIn/default-ALC generation；只释放 scope/registry，代码跟随 Studio 重启。

三者都拥有 resolver、assembly table、generation-wide module definition、scope activation 与 generation lease。Application/Project scope、registry/factory、Panel/UI、task 和 cross-Package dependency 都持有 lease。只有 collectible generation 已从 catalog retire 且全部 lease 归零后才进入 unload；Pinned/Static 永不调用 `Unload()`，避免已知不可卸载 Type 被反复装入新 ALC。

Host type 必须在执行 extension assembly code 前由 lock、artifact manifest/sidecar module index、UI backend、native/external-build metadata 的最严格策略确定；未知能力 fail closed 到 Pinned。已经加载的 Pinned generation 不能在进程内升级为 Collectible，Collectible candidate 若在预执行 validation 中暴露 Tier-0 能力则拒绝并要求重启后按 Pinned 加载。

Pinned host 保留其 cross-Package dependency generation lease 到进程退出。因此它会把精确 dependency closure 一并 pin；任何试图更新该 closure 的 Resolve/Update 返回 restart-required/`PackageGenerationInUse`，不能只替换 dependency 后让 pinned assembly 继续运行。

Active generation pointer 和 last-known-good pointer 分开。只有 candidate 取得有效 required outcome 并完成 registry/catalog commit 后，才能同时推进 active/LKG。

## 9. Generation replacement

Managed reload eligibility：

- module 明确选择 Host 验证过的 `Coexist` 或 `QuiesceThenActivate` handover；
- activation side effect 全部属于可追踪 scope；
- module 能 quiesce，commit 前失败时能确定性、幂等地 resume；
- 不拥有 exclusive native/global resource；
- UI backend 支持该 reload tier。

`Coexist` 适用于 candidate 可与旧 generation 短暂共存的普通模块。`QuiesceThenActivate` 适用于 singleton provider 等独占角色：candidate 先完成 configure/validate，旧 activation 可逆 quiesce 并释放 exclusivity 后才调用 candidate `ActivateAsync()`；如果 candidate 失败，Host dispose candidate 并 `ResumeAsync()` 旧 activation。无法证明 Resume 能重新取得资源的角色必须 `restart-required`。

Handover policy 沿 required activation dependency graph 传播：`QuiesceThenActivate` module 及其所有直接/间接 required dependents 都进入 delayed set，即使 dependent 自身声明 `Coexist`。Early set 只包含 policy 为 `Coexist` 且所有 required dependencies 也在 early set 的 module。Candidate generation 不能在 activation 时混用旧 dependency service/instance。

流程：

1. build immutable managed-reload-eligible candidate；restart-required closure 转入第 6.1 节，不在当前进程继续；
2. staging ALC load、module index、`Configure()`、compatibility 和 descriptor validation；
3. 对所有正在 lease 该 generation 的 scope instance 计算 `EditorModuleInstanceId` required activation graph：每个 active Application/Project scope 的 `OnScopeReady` instance，加上旧 generation 中已 Active 的 instance；Dormant lazy instance 不进入 required set；
4. 在不可见 staging scope 按 dependencies-first 激活 early candidate；
5. 保存 Host-owned UI-neutral state；
6. 旧 closure activation lease 按 dependents-first 执行 `QuiesceAsync(Reload)`；
7. 按 dependencies-first 激活 delayed candidate；
8. 跨全部受影响 `ScopeInstanceId` registry partition 执行预校验的 in-memory closure commit，并提交新的 catalog snapshot；dependency lock 未变化时复用相同 `ResolvedLockHash`，但新的 `PackageGenerationSetHash` 产生新的 `CatalogSnapshotId`；
9. commit 前任一失败：按 dependents-first dispose 已激活 candidate，再按 dependencies-first `ResumeAsync()` 完整旧 graph；任一 Resume 失败则 registry/active pointer 仍保持旧值，但 session 进入 Degraded + restart-required，不能报告 rollback succeeded；
10. commit 后：detach/dispose 旧 Panel/UI/provider/task/activation；
11. dependents-first unload 旧 collectible closure；
12. restore panel instances/state，并发布完成诊断。

一个 Package generation 是最小 candidate/LKG unit；dependency generation 改变时，全部直接/间接 dependent 构成不可拆分的 commit closure。Structural/configure/validation/真实 required activation failure 拒绝整个 candidate closure；soft-gate Waiting 是有效 outcome。外部 side effect 不由 Host 自动事务化；不满足 eligibility 的 module 需要重启。

## 10. Last-known-good 与 cache

- Package LKG 指向完成 build/load/configure/validate、可运行 required activation outcome（Active 或显式 WaitingForCapability）和 registry commit 的 immutable generation；catalog LKG 指向包含完整 resolved lock、PackageGeneration 集合和 dependency closure 的 committed snapshot；
- required set 覆盖该 generation 正在服务的 Application scope 与全部 active ProjectSession：每个 scope 的 `OnScopeReady` instance 和旧 generation 中已经 Active 的 instance；从未使用的 Dormant lazy instance 保持未激活；
- LKG 不证明 Dormant lazy module instance 将来能成功激活。Commit 后首次 lazy activation failure 只 fault/禁用该 instance/contribution，不回滚已提交 Package/catalog；
- compile/load/configure/validation/required candidate activation failure 不改变 active/LKG；
- active 与 LKG artifact 被 pin，cache GC 不得删除；
- cache GC 使用 generation lease，不能只按文件时间删除；
- Studio crash 后恢复验证整个 catalog LKG 的 lock hash、Package generation/artifact hash、compatibility 和 dependency closure；
- lock update 失败或 crash 时恢复完整 previous catalog snapshot；不能用新 lock 解释旧 LKG，也不能逐 entry 混用新旧依赖。

## 11. Unload 与诊断

Host 先释放已知 root：

```text
registry/factory handles
panel content and DataContext
provider/task/subscription/timer
command delegate
UI backend lease
Avalonia AssetLoader assembly descriptor/resource cache entry
cross-Package assembly table references
module activation
ALC resolver/diagnostic records
```

对 Avalonia Tier-1 generation，全部 resource consumer detach 后必须按 physical assembly identity 使 backend/`AssetLoader` assembly descriptor 与 resource cache entry 失效；这只是 root cleanup，不是 generation selection。然后 collectible host 才调用 `Unload()`，清除 Host 对 ALC/Assembly/Type/delegate 的强引用，通过 weak reference 和有界 GC probe 判断是否卸载。失效失败时 quarantine 该 generation 并要求重启。

CLR 不向普通应用提供完整 GC root graph。失败诊断只承诺：

- 已知 Host lease/root 报告；
- Package/module/generation/assembly identity；
- 存活 weak reference 和线程/handle/count telemetry；
- 获取 dump 并使用 SOS/诊断工具分析的指引。

不得把推测的“retained root”描述成 CLR 已证明的完整 root。

Collectible host 的 leak 将 reload unit 升级为 process-lifetime restart-required，停止继续生成新 ALC；已经调用 `Unload()` 的 leaked generation 不能再复用，只保留诊断并要求重启。已知 Tier-0 generation 从一开始就使用 non-collectible pinned host，不走这条失败路径。

## 12. 跨平台

- 支持动态 Project/Package extension 的 Studio desktop profile 使用 CoreCLR/JIT；Native AOT profile 只能包含静态已知 extension，不能宣称支持 ALC 动态加载；
- 同一 `.asmdef`、Package lock、generated project template 和 module index schema；
- RID-specific native/resource asset 从 lock 解析；
- path canonicalization 保留文件系统大小写事实，不进行 Windows-only lowercasing；
- build process 不依赖 PowerShell、`.bat`、Visual Studio、Xcode 或 platform shell；
- portable PDB 和结构化诊断路径映射保持 CI/本机一致；
- watcher overflow/error、Git checkout、项目激活和窗口重新获得焦点触发完整 fingerprint scan。

## 13. 验证矩阵

- implicit `Editor/`、nested `.asmdef`、环、重复 assembly 和 package-qualified reference；
- case-insensitive process-resident Editor/global-resource simple-name reservation、private pure-code dependency 重名隔离、Code-first same-logical old/new staging 例外与 Avalonia Tier 1 generation-unique physical name；
- generated project snapshot、TFM/SDK/analyzer/XAML/resource/shared-reference contract；
- 同一 fingerprint 在两个 checkout/cache root 与支持平台上产生相同 artifact hash；
- legacy workspace dependency、SemVer range intersection、source/hash ambiguity 与 active Application catalog conflict；
- `asharia-comparator-set-v1` 跨 Studio/CLI/native resolver 的 valid/invalid/prerelease/precedence conformance fixtures；
- Studio product/Editor API/Avalonia bridge compatibility range 的 lower/upper boundary、prerelease 与 omitted-field fixtures；
- project synthetic Package 的 name/version/source/content hash 在不同 checkout root 稳定；
- project rename 不改变 implicit AssemblyName/EditorAssemblyId；
- source-build/prebuilt 只允许 Package-wide lock selection，拒绝 per-assembly mixed closure；
- Application/Project reuse 必须匹配 exact PackageGeneration/artifact/managed/native closure；
- 两个 ProjectSession 复用同一 collectible generation 时，其中一个更新被 `PackageGenerationInUse` 拒绝，直到其他 catalog lease 释放；Pinned closure 即使 scope 关闭也只允许 PendingRestart；
- committed Asharia lock 为每个 generated project 确定性 materialize NuGet `packages.lock.json`，locked restore drift 必须失败，aggregate `.deps.json` 必须反向匹配 lock；
- private dependency version conflict 与 cross-Package type identity；
- cross-Package compile reference 不复制到 dependent closure，exact dependency ALC resolution 优先于 ADR/private table，imported/private identity ambiguity 被拒绝；
- 私带 `Asharia.Editor`/Avalonia shared DLL 被拒绝；
- build cancel/supersession、restore failure、immutable publish；
- Coexist/QuiesceThenActivate handover、mixed Coexist-dependent → QTA-dependency 的 delayed propagation、commit 前 dependencies-first resume、dependency partial-closure rejection；
- rollback `ResumeAsync()` fault 进入 Degraded/restart-required，且不误报成功；
- Dormant lazy module 不在 staging 强制激活，首次使用失败只隔离其 contribution；
- lock + generation closure 原子 commit、prepared-transaction crash recovery、previous snapshot rollback；
- restart-required PendingRestart clean-boot success、activation failure relaunch、BootAttempt crash-loop prevention 与 pending GC pin；
- 无 Project 的 Application pending（Project-only/mixed module）与目标 ProjectSession pending 的 scope-aware commit；
- mixed Application + 多 ProjectSession replacement 覆盖全部 `EditorModuleInstanceId`/registry partition，任一失败回滚完整 closure；
- future ProjectScope combined-catalog staging/commit 与 failure isolation；
- last-known-good pin/recovery/cache GC；
- 100 次 reload 后 ALC/thread/handle/Control count 不增长；
- intentional leak fixture 进入 restart-required；
- Tier-0/restart-required generation 在 Project close 后复用 exact pinned host，source/lock 变化只提示 restart，且不重复创建 ALC；
- Pinned generation 的 transitive dependency closure 同时被 pin，dependency update 在重启前被拒绝；
- historical `.asmdef`、module index、Editor API 和 Package lock fixtures；stable ModuleLocalId 在 CLR type rename 后保持 old→candidate matching；
- required/optional module-capability graph、scope legality、cycle/provider ambiguity 与 Engine capability startup ordering；
- Host capability unavailable/epoch lost/recovery 的 Waiting/Blocked/Activate/Resume 状态，不清空无关 Project contribution；
- generation-wide immutable definition object只 Configure 一次，多 Project `ActivateAsync` 并发且 per-scope state 只存在 activation lease；
- 同 artifact 重复 definition staging 产生相同 descriptor/dependency hash，禁止 clock/random/environment 漂移；
- 同一 Package generation 的多 ProjectSession ModuleInstance/registry lease 隔离；
- Windows、Linux、macOS CLI build、desktop load 和 published-app smoke。

## 14. 参考资料

- [NuGet PackageReference lock files and locked mode](https://learn.microsoft.com/en-us/nuget/consume-packages/package-references-in-project-files)
- [.NET AssemblyLoadContext concepts](https://learn.microsoft.com/en-us/dotnet/core/dependency-loading/understanding-assemblyloadcontext)
- [.NET plugin loading with AssemblyDependencyResolver](https://learn.microsoft.com/en-us/dotnet/core/tutorials/creating-app-with-plugin-support)
- [.NET assembly unloadability](https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability)
- [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html)
