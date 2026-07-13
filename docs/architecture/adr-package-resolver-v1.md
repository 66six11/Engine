# ADR：确定性内存 Package Resolver v1

## 状态

Accepted for #272。本文冻结第一版 resolver policy，以及 candidate discovery、版本求解、lock 验证与后续 planning
阶段之间的边界。

## 背景

仓库已经拥有 Project Manifest v1、Installable Package v2、Feature Set v2、Package Lockfile v1 与 Host Profile v1
的机器可读合同，也已经实现 canonical writer、Project Manifest digest、SemVer constraint 检查和“已选择 lock graph”的
跨文档验证。

缺失的下一步刻意小于一个完整 Package Manager：调用方应能在内存中提供一组 exact package candidates，并获得一个
确定的 exact lock graph 或稳定 diagnostics。目录枚举、source configuration、下载、lock reuse、build planning 和 activation
属于独立 policy boundary，不能泄漏进 solver。

相邻的 [Explicit-source Package Candidate Discovery v1](adr-package-candidate-discovery-v1.md) 已实现从调用方给出的
exact payload roots 产生 candidates；它不改变 Resolver 的纯内存、无 filesystem IO 边界。
[Locked Graph Verification & Reuse v1](adr-package-lock-verification-v1.md) 也已实现 existing lock 的只读、fail-closed 复用；
它组合现有 validators 并重新读取 selected payload，但不会调用本 Resolver。

## 决策

### 1. Resolver 是纯内存边界

公共操作在语义上是：

```text
resolvePackageGraph(project, engineApiVersion, candidates, validators, policy)
  -> ResolutionResult(lock, selectedCandidates, diagnostics)
```

`PackageCandidate` 是调用方提供的 typed record，包含：

- 显式 `identity`、exact `version` 和 `packageKind` metadata；
- 只用于 diagnostics 的稳定、调用方所有 `origin`；
- Resolver 不读取、也不参与排序的 opaque `payloadLocation`，用于把 selected candidate 交还后续 payload verifier；
- 已验证 author manifest projection；
- 稳定 lock `source`、`manifestIntegrity` 与 `payloadIntegrity` evidence。

Resolver 不打开 candidate origin、不枚举目录、不计算 payload hash，也不发现更多 candidates。显式 metadata 必须与 author
manifest 一致；candidate evidence 必须在求解前满足现有 lockfile source/integrity 合同。

`origin` 和任何 adapter-local payload location 只是进程内证据，不能进入 lockfile。Lock 只保存已冻结的 stable source
reference 与 integrity records。

### 2. 现有 contracts 继续作为语义权威

Resolver 分四个阶段验证输入：

1. 验证 Project Manifest 与 exact engine API version；
2. 验证每个 author manifest 和 candidate metadata/evidence；
3. 求解 version 与 kind constraints；
4. 物化 canonical lock，再用现有 lock-local 和 cross-document validators 证明结果。

Resolver 不分叉 schema rules、project hashing、lock ordering、option validation 或 exact-graph validation。Contract failures
保留既有 diagnostic codes；resolver-specific failures 使用 `resolver.*` namespace。

### 3. Resolution Policy v1 显式且确定

Policy version `1` 使用以下规则：

1. 整张图中一个 package identity 只选择一个 `packageKind` 和 exact version。
2. Project requirements、Feature Set members 与 installable dependencies 的 constraints 取交集；任何 origin 都不能覆盖其他
   origin。
3. 下一个 unresolved identity 固定为 package identity 字典序最小项。
4. Compatible candidates 按 SemVer precedence 降序尝试；SemVer precedence 相同的 versions 按 exact version string
   排序，build metadata 不能继承输入枚举顺序。
5. Exact constraint 可以选择对应 exact prerelease；range 只有在 `allowPrerelease` 为 true 时才允许 prerelease。
6. Resolver 使用稳定 DFS/backtracking；下游 conflict 可以让较早 identity 从最高 compatible version 回退到下一个
   compatible version。
7. 同一 identity 与 exact version 出现多个 eligible candidate records 时返回 ambiguity error。v1 不提供隐式
   bundled/project/local precedence，即使这些 records 看起来完全相同也不选择。被全部 active requirements 排除的 ambiguous
   version，以及图中未要求的 identity，不污染其他有效 resolution。
8. Candidate engine compatibility 在选择前以 exact engine API input 检查。
9. Selected dependency cycle 在返回 lock 前失败。

Policy 选择的是“最高兼容图”，而不是孤立地为每个 package 选择最高版本。Candidate input order、manifest array order 与
dictionary insertion order 都没有语义。

### 4. Diagnostics 保留 requirement chains

每个 resolver-specific diagnostic 包含：

- stable code；
- 存在时的 affected identity；
- deterministic message；
- 零个或多个 requirement chains。

Requirement chain 从 project direct selection 开始，记录通向 failure 的 selected author edges。当多个 declarations 约束同一
identity 时，diagnostic 按稳定顺序保留全部不同 chains。经过同一个已选择 author edge 的重复 reachability path 可以使用
canonical shortest chain；它们不创造新的 constraint。这样既能解释 conflict，也不把公共合同绑定到内部 decision stack，
更不会枚举指数数量的等价 paths。

第一版至少区分：

- missing identity；
- requested-kind mismatch；
- unsatisfied version intersection；
- engine API incompatibility；
- ambiguous exact candidate；
- candidate metadata/evidence failure；
- dependency cycle；
- internally invalid materialized output。

### 5. Exact lock 是唯一持久结果

成功时，Resolver：

- 为每个 project direct package 与 Feature Set 创建 exact roots；
- 保留 Feature Set exact nodes，不在展开后令其消失；
- 从 selected author manifests 创建 exact dependency references；
- 复制 selected source 与 integrity evidence；
- 记录 resolver semantic version 与 policy version；
- 记录 exact engine API version 与 normalized Project Manifest digest；
- 通过现有 canonical writer model 规范化 lock；
- 用 selected author manifests 验证完整结果。

`selectedCandidates` 只作为后续 verification/planning boundaries 的内存便利结果返回。它按 identity 排序，不是第二份持久图。

## Failure 与状态模型

Resolution 是 atomic、side-effect-free 操作。失败时 `lock` 和 `selectedCandidates` 为空，`diagnostics` 非空；成功时
`diagnostics` 为空。Resolver 不修改 Project Manifest 或 candidate records，也不写 partial lockfile。

File replacement、journal/recovery policy、candidate payload verification 与 update-versus-locked mode decision 由调用方拥有。

## 拒绝的替代方案

### 无 backtracking 的 greedy highest-version selection

拒绝。局部最高 candidate 可能引入与另一个 root 冲突的 transitive constraint，而较低 candidate 能形成有效图。

### 在 solver 内定义 source-kind priority

拒绝。Bundled、project-embedded 与 local precedence 是尚未设计的 user/workspace policy。静默选择会让 lock 依赖隐藏的
source configuration。

### 在 `resolvePackageGraph` 内做 filesystem discovery

拒绝。Discovery order、path mapping、payload verification 与 missing-source behavior 是独立职责；保持边界后，solver 才能
确定且可直接测试。

### 在 v1 引入 PubGrub

后置。PubGrub 可以改善 conflict explanation 与大图行为，但当前 named-package、single-version 模型没有需要这项复杂度的
证据。公共 inputs、outputs、policy version 与 structured requirement chains 允许未来替换实现而不修改 lock contract。

## 不做事项

- candidate discovery 或目录约定；
- registry、download、authentication、signature 或 trust policy；
- source override/precedence rules；
- existing-lock verify、reuse、update 或 minimal-change policy；
- Build Plan、Artifact Plan、Host Activation Plan 或 Activation Executor；
- Editor Package Manager UI；
- capability-provider resolution 或同一 identity 的多 selected versions。

## 验证

实现必须覆盖：

- empty 与 direct graphs；
- nested Feature Sets 与 transitive packages；
- candidate permutation 与 canonical byte equivalence；
- stable highest-compatible selection 与 downstream backtracking；
- exact/range prerelease behavior 与 engine API constraints；
- multiple constraint origins 与 requirement-chain diagnostics；
- missing、kind mismatch、ambiguity、invalid candidate、cycle 与 invalid option failures；
- input immutability；
- final lock-local 与 cross-document validation。

Slice 提交前仍必须通过 repository contract、topology、encoding、documentation、whitespace、ClangCL 与 MSVC gates。
