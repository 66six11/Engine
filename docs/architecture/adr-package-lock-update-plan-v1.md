# ADR：Package Lock Update Plan 与 Impact Preview v1

## 状态

Accepted for #303。

本文在 [Locked Package Graph Verification & Reuse v1](adr-package-lock-verification-v1.md) 与文件 apply 之间增加一个纯内存、
无写入的 update planning 边界。它消费已经验证的 Engine Distribution、完整 fresh candidate 集、base/proposed Project intent
和 existing Project Lock v2，产生 proposed Project/Lock、graph-only impacts 与 canonical preview；它不修改任何 Project 文件，也不把
Package Manager UI、acquisition、build/restart 判断或 crash recovery 混入 resolver。

当前 `tools/` 下的 Python 实现只作为仓库 reference oracle、测试与 CI 工具。正式 Editor、Launcher、Installer、Repair 或 Runtime
不得启动、携带或依赖 Python；产品实现应在 C#/.NET 或 native owner 中复现本文的输入、确定性、诊断与脱敏合同。

## 背景

现有链已经能：

- 从 verified Engine Distribution 与显式 Project/local sources 派生完整 fresh candidates；
- 用 Resolution Policy v1 从零求解最高兼容 canonical Lock v2；
- 对 current Project、existing Lock、exact sources 与实际 payload 做 fail-closed verify/reuse；
- 把 verified graph 交给 Effective Session、Host Composition、Source Build 与 artifact evidence consumers。

仍缺少的不是另一个 resolver，也不是文件 writer，而是一个可审查的中间决定：当用户修改 Project intent，或显式要求更新全部/部分
packages 时，哪些 existing locked candidates 应优先保留，哪些节点因目标或依赖约束不得不改变，最终将产生哪份 proposed Project/Lock。

如果直接调用 Policy v1，targeted update 会把整图重新提升到当前最高 compatible versions，无法表达“未受影响节点尽量保持 locked”；
如果直接复用 existing Lock，又无法让 target 或新的 Project intent 生效。若 planner 同时写文件，则 graph policy failure、filesystem
replace failure 与跨文件 crash recovery 会失去独立失败边界，也无法在写入前展示稳定 impact preview。

## 外部依据与边界结论

本决策只借鉴官方工具已经证明有价值的行为边界，不复制其格式、resolver 算法或 CLI：

| 生态 | 官方行为 | 对 Asharia 的约束 |
| --- | --- | --- |
| Cargo | `cargo update` 无 package spec 时更新全部；指定 packages 时执行 conservative update，其他 dependencies 保持 locked，除非目标无法在不更新依赖的情况下完成；`--dry-run` 不写 lockfile。 | full 与 targeted-conservative 必须是显式 request；preview 应先于 apply，未授权节点不能因 ordinary highest-compatible policy 静默升级。 |
| NuGet | locked mode 使用 exact lock 或失败；`--force-evaluate` 是显式重新求值入口。 | verify/reuse 与 update planning 必须是不同操作，verification failure 不能自动授权 update。 |
| uv | existing lock 默认优先保留 locked versions；`--upgrade` 更新全部，`--upgrade-package` 只更新目标并保留其他 locked versions。 | locked-candidate preference 必须显式进入 update policy，不能改变普通首次求解的默认行为。 |
| Composer | `update` 可列出 partial package targets；`--minimal-changes` 尽量保留其他 locked dependencies；`--dry-run` 模拟而不执行写入。 | target closure 与必要 transitive changes 需要可解释；graph plan 与安装/文件修改必须分层。 |

官方资料：

- [Cargo `update`](https://doc.rust-lang.org/cargo/commands/cargo-update.html)
- [NuGet PackageReference lock files](https://learn.microsoft.com/en-us/nuget/consume-packages/package-references-in-project-files#locking-dependencies)
- [NuGet `restore` options](https://learn.microsoft.com/en-us/nuget/reference/cli-reference/cli-ref-restore)
- [uv locking and syncing](https://docs.astral.sh/uv/concepts/projects/sync/)
- [Composer `update`](https://getcomposer.org/doc/03-cli.md#update-u-upgrade)

由此冻结三段操作：

1. `verify/reuse`：证明 existing exact graph 仍可原样使用，不调用 resolver；
2. `plan update`：纯内存地产生 proposed documents、selected candidates、impacts 与 preview，不写文件；
3. `apply`：在独立 transaction 中重新验证 plan preconditions 并处理 temp/flush/replace/journal/recovery。

本文只拥有第二段。

## 决策

### 1. Planner 是纯函数式、无文件写入的编排边界

语义操作是：

```text
planPackageLockUpdate(
  baseProject,
  proposedProject,
  existingLock,
  verifiedDistribution,
  completeFreshCandidates,
  request,
  validators
)
  -> success(PackageLockUpdatePlan)
   | failure(diagnostics)
```

Planner 消费 parsed、detached inputs；不打开 Project 文件、不枚举 source roots、不下载 package、不重新生成 Distribution，也不调用
任何 writer/apply callback。`completeFreshCandidates` 必须由 verified Distribution catalog 与 Project/local source provider 在本次调用前
完整收集；planner 不把缺失 provider、extra directory 或 existing Lock 反推位置当成隐式候选来源。

输入要求：

- `baseProject` 是 existing Lock 原本绑定的 canonical Project Manifest v2 intent；
- `proposedProject` 是用户希望评估的完整 Project Manifest v2 intent，而不是 patch instruction；
- `existingLock` 是 canonical Project Lock v2，且其 base Project digest、Distribution identity/generation 与 graph-local/cross-document
  invariants 可被证明；
- `verifiedDistribution` 是 proposed Project engine requirement 对应的完整只读发行事实；
- candidates 包含该 Distribution 的全部 bundled candidates 与项目已选择的全部 fresh Project/local candidates；
- `request` 是封闭的 full 或 targeted-conservative union。

任一输入无效时原子失败。Planner 不修改 caller-owned dictionaries、lists、candidate projections 或 diagnostics；成功值深度脱离输入并
表现为 immutable snapshots。

### 2. Update request 只有 full 与 targeted-conservative 两种

#### Full update

Full request 授权以 `proposedProject` 和 complete fresh candidates 重新求解整张图。它使用 Resolution Policy v1，不携带 locked
preferences，因此继续得到“当前最高兼容 graph”的既有语义。

只有 full request 可以改变 Project 的 Engine Distribution family/API requirement。Engine requirement 变化意味着 existing Lock 的
Engine generation binding 不能被当成 targeted baseline；planner 仍报告 proposed graph 与 engine binding impacts，但不把跨 generation
变化伪装为局部 package 更新。

#### Targeted-conservative update

Targeted request 携带两个分离的 package identity 集合：

- `unlockTargets`：显式授权重新选择 package candidate/version/source/evidence；这些 identities 不获得 old-lock preference；
- `intentOnlyTargets`：只授权 package option 等不改变 direct requirement selection 的 Project intent 变化；每个 identity 都必须有
  existing locked node，并在 proposed graph 仍可达时由 exact `CandidatePreference` 重建，避免 option-only 操作意外解锁或升级 package。

两个集合都必须 canonical、内部无重复，并且互斥；它们的 union 必须非空。Full request 的两个集合都必须为空。Identity 只能是完整
package 或 Feature Set identity，不得是 module、target、provider、source path 或自由文本 selector。

Targeted request 必须满足：

- `baseProject.engine == proposedProject.engine`；engine requirement 变化以稳定 diagnostic 拒绝并要求 full；
- direct package requirement 或 direct Feature Set 的 added、removed、constraint changed 必须由 `unlockTargets` 覆盖；
- package option changes 可以由 `unlockTargets ∪ intentOnlyTargets` 覆盖；只改变 options 且不希望解锁 candidate selection 时应使用
  `intentOnlyTargets`；
- 每个 `intentOnlyTargets` identity 必须对应一个实际的 normalized package-options change；不能用空的 intent-only 授权凑出 targeted request；
- `intentOnlyTargets` 中缺少 existing locked node，或在 proposed graph 仍可达却缺少 exact fresh `CandidatePreference` match 的 identity 会
  fail closed；因其他合法 unlock 而变得不可达的 old transitive node 不要求伪造候选；新加入、或明确要求重新选择
  version/source/evidence 的 identity 使用 `unlockTargets`；
- 被移除的 direct package/Feature Set identity 仍必须以其旧 identity 出现在 `unlockTargets` 中；
- 任何不满足上述 role-specific coverage 的 Project intent drift 都 fail closed，而不是被 preview 静默吸收；
- target identity 必须能由 proposed intent 或 existing graph 解释，任意无关 target 被拒绝。

Planner 为 `unlockTargets` 之外的 existing locked identities 构造 immutable exact `CandidatePreference` records，包含所有
`intentOnlyTargets`，并通过 resolver 的 `candidatePreferences` 参数显式调用 Resolution Policy v2。Unlocked identities 的新选择仍受
proposed Project、author dependencies、engine compatibility 与 candidate ambiguity rules 约束。

“conservative”表示 preference，不是硬 pin：当 preferred candidate 已不满足 proposed constraints，或 target 的新 dependency closure
要求改变 transitive node 时，Policy v2 可以 deterministic backtrack 到其他 valid candidate。Preview 必须解释这些必要 changes；它不能
通过忽略 dependency constraints 强行保留一个无效 graph。

### 3. Resolver Policy v1 保持默认，Policy v2 只表达显式 `CandidatePreference`

[Deterministic in-memory Package Resolver v1](adr-package-resolver-v1.md) 的 default `policyVersion: 1`、highest-compatible ordering、
ambiguity、DFS/backtracking 与 diagnostics 全部保持不变。首次求解、full update 和没有 update intent 的普通调用不会看到 existing Lock，
也不会因为 #303 改变结果。

Resolution Policy v2 只增加一个输入：`candidatePreferences`。其中每个 immutable `CandidatePreference` 按 package identity 唯一绑定
exact version、package kind、canonical source，以及 Project/local source 所需的 manifest/payload integrity；它不是
`PackageCandidate`，也不携带 payload location。Resolver 在该 identity 仍可达并进入搜索点时，要求 preference 在本次 validated candidate catalog 中恰好匹配
一个 candidate，然后先尝试该 candidate；preferred branch 若不能满足完整 graph，搜索只进入其他 version，不在同 version 静默替换
source/evidence。Policy v2 不知道“UI selection”“target depth”或文件路径，也不计算 impact；这些仍由 update planner 拥有。

Preferences 必须显式携带 policy version 2。以下情况稳定失败：

- 在 Policy v1 调用中传入 `candidatePreferences`；
- preference 不是 immutable `CandidatePreference`；
- 同一 identity 有多个 preferences；
- 仍可达的 preference 在本次 complete fresh candidate set 中没有唯一 exact match；
- preference metadata/source/evidence 与 existing Lock 无法精确绑定。

### 4. 非 unlock identity 的 same-version source/evidence drift 不是“未变化”

Targeted-conservative planning 可以因约束需要改变 non-unlock identity 的 version，但不能把同一个 non-unlock version 静默重绑到不同
source 或不同 manifest/payload evidence。对每个不在 `unlockTargets` 的 identity，包括 `intentOnlyTargets`：

- 若 proposed node 保持同一 version，则 package kind、canonical source、manifest integrity 与 payload integrity 必须与 existing node
  精确一致；
- source unavailable、同 version 的 source substitution、integrity-only refresh 或 candidate evidence drift 都 fail closed；
- 若 graph constraints 必须改变 version，则它是明确的 necessary version impact；`intentOnlyTargets` 上的变化按 `direct-intent` 归因，
  其他 non-unlock identity 按 `transitive` 归因；新 node 的 source/evidence 由实际 selected candidate 决定并进入 preview。

这条规则由 Policy v2 的 identity 搜索点保证：exact `CandidatePreference` 缺失或重复时立即失败；preferred branch 失败时不会尝试同 version
alternate source，而只会继续其他 version。Planner 随后仍对 proposed Lock node 与 selected candidate 做 exact output validation。由此，本地
路径内容变化、source ID 重定向或 Distribution inventory 漂移不会被伪装成“版本没有变”。需要接受同版本新 bytes/source 时，调用方必须把
该 identity 纳入 `unlockTargets`，或使用 full request；planner 本身仍不会取得 trust、signature 或 acquisition 权限。

### 5. 成功结果包含 proposed documents，不包含写入授权

成功 `PackageLockUpdatePlan` 至少包含：

- normalized immutable `proposedProject`；
- normalized immutable canonical `proposedLock`；
- 与 proposed Lock 每个 node 一一绑定、按 UTF-8 identity 稳定排序的 `selectedCandidates`；这些 detached records 清除
  `payloadLocation`，并只从 logical source 重建 portable origin，不保留 adapter-local path；
- graph-only `impacts`；
- stable `fingerprints`；
- path-redacted canonical `preview`。

失败时以上值全部为空，只返回稳定 diagnostics；不得返回 partial graph、partial preview 或“可写一半”的 plan。
Python reference oracle 的 public properties 每次返回 detached copies；underscore-prefixed backing fields 只是实现细节，不属于可变更或持久化的
public contract。产品实现仍应使用自身语言的深度不可变 value/snapshot 类型。

返回 proposed Project 是为了让后继 apply 同时拥有要提交的 direct intent 与 exact Lock，不表示 planner 会写 `asharia.packages.json`。
`renderNormalizedProject(plan.proposedProject)` 与 `renderNormalizedLock(plan.proposedLock)` 必须由现有 canonical writers 产生稳定 bytes。

### 6. Impact 只陈述 graph facts

Impact 由 normalized base Project/existing Lock 与 proposed Project/Lock 计算。Plan 先用
`projectManifestChanged` 与 `engineInputChanged` 表达 Project/Engine 输入是否变化，再按稳定 package identity 返回带 before/after graph snapshot
的 package impacts。每个 package impact 的 `changes` 只能描述 graph facts，例如 node added/removed、version/source/evidence、exact dependencies、
directness、direct requirement 或 package options 的变化；dependency edge 变化作为所属 package snapshot 与 `dependencies-changed` fact 表达，
不创建拥有额外生命周期的 edge object。

每个 package impact 还必须带 closed `cause`：`requested-target`、`direct-intent`、`transitive` 或 `full-policy`。

Cause 分类固定为：full request 的 changes 使用 `full-policy`；targeted request 中 `unlockTargets` 的 changes 使用
`requested-target`；由 base/proposed Project intent difference 直接产生的其他 changes 使用 `direct-intent`；剩余 dependency closure changes
使用 `transitive`。同一个 impact 只能有一个 cause，不能由 UI 自由填写。

Impact 不推断：

- 是否需要 build、cook、repair、restart 或 live reload；
- artifact 是否已经存在或可执行；
- download size、license、security、publisher trust 或 network availability；
- Editor panel、用户确认文案或 install progress。

这些结论需要 Build/Artifact/Session/Acquisition 等 owner 的额外证据。Graph-only preview 不能把“package version changed”夸大为
“产品已经更新”或“重启一定足够”。

### 7. Fingerprints 绑定完整 planning evidence

Plan fingerprints 使用 domain-separated canonical logical bytes，而不是 mtime、对象地址或 filesystem path。每个 digest 都计算
`sha256(UTF8(domain) + NUL + canonicalBytes)`；不同语义对象使用不同的 versioned domain，避免相同 bytes 在不同角色下共享 identity。
公开 integrity records 至少包括：

- `baseProjectManifestIntegrity` 与 `proposedProjectManifestIntegrity`；
- `baseLockIntegrity`；
- `distributionIntegrity`，并单独保留 `engineGenerationId`；
- `candidateSetIntegrity`，覆盖 complete fresh candidates 的 identity/version/kind/source 及全部 contract evidence；
- `requestIntegrity`，覆盖 update/resolver policy versions、mode、`unlockTargets` 与 `intentOnlyTargets`；
- `selectedCandidateSetIntegrity`，覆盖 resolver 实际选中的完整 canonical candidate projections；
- `proposedLockIntegrity`；
- `impactSetIntegrity`，覆盖稳定排序的完整 package impacts、closed causes 与 before/after graph facts；
- `planIntegrity`，覆盖状态、Project/Engine change flags、上述 component integrities 和 canonical impact projection。

这些对象分别使用 `com.asharia.package-lock-update/<object>/v1` domain，例如 `request/v1`、`impact-set/v1`、
`selected-candidate-set/v1` 与 `plan/v1`；domain 字符串本身是 v1 integrity contract 的一部分。

Canonical preview 是 plan 的 deterministic projection，并携带 `planIntegrity`；它不是独立持久化对象，也不虚构第二个 preview identity。

后继 apply 必须把这些 fingerprints 当成 optimistic preconditions，并重新读取/验证当前文件与外部 generation；本文不声称 fingerprint
自身提供锁、事务、durability 或 crash recovery。

### 8. Preview 是 canonical、path-redacted 的稳定投影

Canonical preview 只包含可移植 logical facts：request kind、`unlockTargets`、`intentOnlyTargets`、stable fingerprints、Project/engine changes、
package impacts 及其 exact dependency facts，以及 proposed resolver provenance。Arrays 使用稳定 UTF-8 key 排序，JSON 使用 repository
canonical formatting；candidate input permutation、
Project declaration semantic order 或 dictionary insertion order 不得改变 preview bytes。

Preview 不包含：

- candidate `payloadLocation`、project root、local absolute mapping、Windows extended path；
- adapter-local `origin`、temporary/cache/staging paths；
- exception 原文、object repr/address、时间戳或 enumeration index；
- credential、token、registry response 或未验证的 UI metadata。

Stable source union 中本来就属于 Project Lock 合同的 logical `project-embedded.relativePath` / local `sourceId` 可以显示；它们不是机器
absolute path。Diagnostics 使用 `update.*` namespace，并遵守相同脱敏和确定排序规则。

## Owner 边界

| Owner | 拥有 | 不拥有 |
| --- | --- | --- |
| Project/CLI/UI caller | proposed Project intent、full 或 split `unlockTargets` / `intentOnlyTargets` request、是否把 plan 交给 apply | resolver internals、文件原子性、隐式 candidate discovery |
| Candidate providers | verified Distribution 与 complete fresh Project/local candidate snapshots | update target、locked preference、Project/Lock write |
| Resolver Policy v1 | 首次/full 求解的最高兼容 deterministic graph | existing Lock preference、impact、apply |
| Resolver Policy v2 | 调用方通过 `candidatePreferences` 提供的 immutable exact `CandidatePreference` + deterministic version fallback | target coverage、preview、filesystem state |
| Lock update planner | request validation、locked preference projection、proposed graph、impact/fingerprint/preview | source acquisition、文件写入、build/restart/UI policy |
| 后继 apply transaction | plan precondition revalidation、Project/Lock write、journal/recovery | 重新决定 graph 或静默扩大 target sets |

## Diagnostics 与原子性

Planner 优先保留现有 `contract.*`、`resolver.*` 与 `lock.*` diagnostics；只为编排层新增稳定 `update.*` codes。最低类别包括：

- missing/invalid existing Lock 或 base/Lock mismatch；
- invalid/empty/duplicate/overlapping/unknown unlock 或 intent-only targets；
- targeted engine requirement/current Distribution Engine generation change；
- untargeted Project intent drift；
- `CandidatePreference` unavailable/ambiguous/mismatched；
- non-unlock same-version source/evidence drift；
- proposed resolution failure；
- internally invalid impact、fingerprint 或 preview projection。

所有 diagnostics 按 canonical identity/location/code/message key 排序，message 不泄漏 machine path。输入 permutation 不能改变结果或
rendered diagnostic bytes。

## 拒绝的方案

### 修改 Resolver Policy v1 让它默认偏好 existing Lock

拒绝。首次求解与 full update 的既有 highest-compatible 语义会被隐式改变；调用方也无法从 lock 的存在判断是否真的授权 targeted update。

### 让 locked verifier 返回 `needsUpdate` 并自动调用 resolver

拒绝。Verification failure 可能是 corruption、source unavailable、engine mismatch 或 TOCTOU，不等同于更新授权。

### 把 unlock targets 直接变成 hard pins

拒绝。Unlock target 是“允许主动重新选择”的 intent，不是跳过完整 dependency solving 的许可。Transitive constraints 仍必须形成有效
exact graph；intent-only target 更必须保留 existing locked preference。

### 只按 version 比较 impact

拒绝。同 version 的 source/evidence 变化会遗漏 bytes 与 ownership drift，也可能让 local mapping 静默替代已审计 source。

### Planner 直接写 Project/Lock

拒绝。跨文件 write 涉及 concurrency、stale-plan check、temp files、flush、replace、journal、rollback 与 crash recovery；这些失败与 graph
policy 独立，必须由后继 transaction ADR 决定。

### 在 preview 中推断 build/restart/download

拒绝。Package graph 没有 artifact/current-process/acquisition evidence；这会越过 Build、Effective Session 与 installer owners。

## 验证要求

#303 的 reference implementation 和 tests 应覆盖：

- full request 保持 Policy v1 highest-compatible 语义；
- targeted request 只对 `unlockTargets` 解除 preference；`intentOnlyTargets` 与其他 non-unlock graph 使用 Policy v2 exact
  `CandidatePreference` records；
- necessary transitive backtracking 与稳定 impact cause；
- split target 的 canonical/disjoint rules；direct package/Feature Set add/remove/constraint changes 只接受 `unlockTargets`，option changes 接受
  两个集合的 union；
- option-only intent 不会意外升级 package，且无实际 option difference 的 intent-only target 会被拒绝；
- targeted engine change、unknown/duplicate/empty/overlapping target 拒绝；
- non-unlock same-version source、manifest integrity 与 payload integrity drift 拒绝；
- proposed Project/Lock、selected candidates、impacts、fingerprints 与 preview 深度不可变；`requestIntegrity`、`impactSetIntegrity`、
  `selectedCandidateSetIntegrity` 和 `planIntegrity` 使用各自 domain-separated canonical bytes；
- candidate/Project semantic permutation 与 dictionary insertion-order 变化下结果及 preview bytes 等价；target sets 自身必须先采用 canonical UTF-8 顺序；
- diagnostics 原子、稳定、绝对路径脱敏；
- mocked file writer、replace、network/acquisition 与 UI callback 均不被调用；
- Distribution + Project/local providers → update plan → canonical proposed Lock → existing validators 的 headless handoff。

提交前仍运行 Python 3.14 全量 tests、package contracts/topology、encoding、doc-sync、asset boundaries、`git diff --check`，以及
Conan-before-CMake 的 incremental ClangCL + clang-tidy / MSVC gates。该 Slice 不修改 C++ targets 或 CMake topology；复用现有 build trees，
不以 clean rebuild 代替相关性验证。

## 后果与后继边界

正向后果：Package Manager/CLI 可以在任何写入前得到 deterministic proposed graph 与可审查 preview；普通首次求解行为不变；targeted
update 保留未受影响 locked candidates，并明确显示因 dependency constraints 必须发生的 transitive changes；机器路径不会进入 ABI、日志
或 confirmation payload。

代价：调用方必须提供 complete fresh candidate set 和完整 proposed Project，而不是松散 patch；同 version 的 non-unlock bytes/source drift
会拒绝 targeted plan；Policy v2 增加必须长期测试的 resolver provenance；真正落盘前仍需再次验证 fingerprints 和 filesystem state。

后继工作继续独立：

1. Project Manifest/Lock atomic apply、process/file locking、temp/flush/replace、journal、rollback 与 crash recovery；
2. Package Manager/Bootstrap UI 对 canonical preview 的展示与确认；
3. registry/acquisition/trust/license/security evidence；
4. Build/Repair/Restart 对 applied exact graph 的后续归约；
5. production C#/.NET 或 native orchestration，Python 保持 repository-only。

## 相关资料

- [Deterministic in-memory Package Resolver v1](adr-package-resolver-v1.md)
- [Locked Package Graph Verification & Reuse v1](adr-package-lock-verification-v1.md)
- [Engine Distribution Package Catalog Snapshot v1](adr-engine-distribution-package-catalog-snapshot-v1.md)
- [Project / Local Package Source Catalog v1](adr-project-local-package-source-catalog-v1.md)
- [Project Manifest 与 Package Lock v2 硬切](adr-project-manifest-lock-v2-hard-cut.md)
- [Package-first 架构](package-first.md)
- GitHub #264、#271、#301、#302 与 #303
