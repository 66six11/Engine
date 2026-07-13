# ADR：显式来源 Package Candidate Discovery v1

## 状态

Accepted and implemented for #273。本文冻结 Candidate Discovery 第一版的最小职责：从调用方明确给出的 bundled、project-embedded 或
local package root 加载一个不可变 `PackageCandidate` 快照。它不冻结 catalog、目录扫描、source precedence 或 locked
reuse/update workflow。

本文位于 [Package Candidate / Lockfile v1](adr-package-candidate-lockfile-v1.md) 与
[Deterministic in-memory Package Resolver v1](adr-package-resolver-v1.md) 之间。前者定义 candidate evidence 与持久 lock，
后者只消费调用方提供的 candidates；本文定义这些 in-memory candidates 如何从一个已知 payload root 产生。

## 背景

当前仓库已经实现：

- installable package v2 与 Feature Set v2 的 schema/semantic validation；
- exact author-manifest bytes digest 与 `asharia-package-tree-v1` payload digest；
- `PackageCandidate`、deterministic resolver、canonical lock writer 与 locked candidate integrity validator。

当前缺口不是“在任意目录里猜哪些文件夹是 package”，而是把 source configuration、catalog 或 workspace mapping 已经确认的
位置转换成 resolver 可以信任的 typed evidence。如果 discovery 同时拥有父目录枚举、版本求解和 lock policy，目录布局、机器路径
与失败恢复就会泄漏进 package 合同，未来 Package Manager 也无法替换某一个阶段。

因此，Candidate Discovery 是一个 umbrella boundary，但第一版只冻结其中的 **strict candidate loader**。Bundled catalog、项目
package index 和 local workspace mapping 是它的上游位置提供者，不属于本 Slice。

## 候选方案

| 方案 | 优点 | 代价与风险 | 结论 |
| --- | --- | --- | --- |
| 显式来源位置 + strict loader | 输入边界小、可确定测试；不冻结目录布局；可复用现有 integrity/manifest 合同 | 上游必须先拥有 catalog 或 mapping | 采用 |
| 递归扫描若干约定父目录 | 初次 demo 调用简单 | 扫描顺序、忽略规则、symlink、权限和目录结构成为隐式公共 API；无法正确表达 local mapping/未来 acquisition | 拒绝 |
| discovery、resolver 与 lock reuse 合成一个操作 | 表面调用点少 | source IO、constraint search、stale-lock decision 和文件事务混在一起，失败与重试边界不清 | 拒绝 |
| best-effort 返回部分 candidates | UI 浏览可继续显示部分结果 | 同一坏源可能静默改变可解版本集合，导致 resolver 生成不同 lock | v1 拒绝；未来可由 catalog UI 单独提供 |

## 决策

### 1. Package Manager 按阶段组合，不由目录扫描驱动

```mermaid
flowchart LR
    Providers["Source providers (planned)\nbundled catalog / project index / workspace mapping"]
    Locations["Explicit CandidateLocation records\ncaller-owned"]
    Loader["Strict Candidate Loader v1\n#273 implemented"]
    Candidates["PackageCandidate snapshots\nin memory"]
    Resolver["Deterministic Resolver v1\n#272 implemented"]
    Lock["Canonical lock graph\nin memory"]
    Locked["Locked verify/reuse/apply\nfuture Slice"]

    Providers -->|"produces exact roots"| Locations
    Locations -->|"loads and validates"| Loader
    Loader -->|"creates evidence"| Candidates
    Candidates -->|"offers versions"| Resolver
    Resolver -->|"materializes"| Lock
    Lock -->|"verifies before use"| Locked
```

Source providers 拥有“有哪些来源位置”；loader 拥有“这个明确位置当前是否构成有效 candidate”；resolver 拥有“选择哪一组
exact candidates”；locked workflow 拥有“已有 lock 是否仍可复用以及如何安全写入”。

递归读取一个已确认 payload root 内的 regular files 是 payload integrity 计算，不是候选枚举。Loader 不查看 sibling directories，
也不尝试从父目录发现更多 versions。

### 2. 输入是封闭的显式位置 union

公共操作在语义上是：

```text
loadPackageCandidates(locations, validators)
  -> CandidateDiscoveryResult(candidates, diagnostics)
```

`locations` 是以下封闭 union；所有 filesystem roots 都是 adapter-local、进程内状态：

| descriptor | 字段 | 派生的 lock `source` |
| --- | --- | --- |
| `BundledCandidateLocation` | `distributionRoot`、`distributionId`、`relativePath` | `bundled` + `distributionId` + `relativePath` |
| `ProjectEmbeddedCandidateLocation` | `projectRoot`、`relativePath` | `project-embedded` + `relativePath` |
| `LocalCandidateLocation` | `sourceId`、`payloadRoot` | `local` + `sourceId` |

Descriptor 不接收 package `id`、`version`、`packageKind`、integrity 或自由 `origin`。这些值必须来自已验证 manifest、实际读取的
payload 和稳定 source fields，避免调用方声明与磁盘证据漂移。

`distributionRoot`、`projectRoot` 与 `payloadRoot` 不进入 candidate lock projection，也不得出现在 stable diagnostics。
`local.sourceId` 到机器路径的映射继续由 workspace/CLI source configuration 拥有。

### 3. Stable source key 与 origin 只能派生

Loader 从 canonical source fields 派生唯一 `sourceKey` 和 diagnostic-only `origin`：

| source kind | `sourceKey` / `origin` 文本 |
| --- | --- |
| bundled | `bundled:<distributionId>:<relativePath>` |
| project-embedded | `project-embedded:<relativePath>` |
| local | `local:<sourceId>` |

相关字段先满足既有 identity/normalized-relative-path schema；相对路径禁止 `:`，因此该表示没有 delimiter ambiguity。
Source keys 按 NFC UTF-8 bytes 排序。相同 stable source key 在一个 batch 中出现两次时，loader 以
`discovery.source.duplicate` 失败，不按调用方输入顺序保留一条。

两个不同 source keys 若在当前进程解析到同一物理 payload root，也以 `discovery.source.alias` 失败。Alias identity 只用于本地
输入验证：先解析已存在的真实目录，再使用平台路径等价规则比较；diagnostic 只列出排序后的 source keys，不泄漏绝对路径。
这避免同一 payload 伪装成不同来源并把无意义 ambiguity 交给 resolver。

### 4. Root containment 在读取 manifest 前证明

Bundled 与 project-embedded descriptor 的 `relativePath` 必须先通过既有 portable relative-path 规则，再在可信 base root 下解析。
Loader 必须证明：

1. base root 与最终 payload root 存在且是目录；
2. resolved payload root 仍包含在 resolved base root 内；
3. 从 base root 到 payload root 的相对路径段不包含 symlink、junction 或其他 reparse/link entry；
4. payload root 自身不是 symlink/junction。

任何失败都发生在 manifest parse 前。Local descriptor 没有 containment base，但 `payloadRoot` 自身仍必须是存在的普通目录且不是
symlink/junction。Payload root 之下的 links、non-regular entries、NFC/path portability 和 case-fold collisions 继续由冻结的
`asharia-package-tree-v1` 规则拒绝。

### 5. 一个位置只产生一个 candidate snapshot

对按 source key 排序后的每个位置，loader 同步执行：

1. 验证 descriptor、root containment、duplicate source 与 physical alias；
2. 从 root 读取且只读取 `asharia.package.json` 作为 author manifest；
3. 以 strict UTF-8 解析 exact bytes，并调用现有 dispatcher、schema 与 semantic validators；
4. 只接受 installable package v2 或 Feature Set v2；
5. 从已验证 manifest 派生 `identity`、exact `version` 与 `packageKind`；
6. 对 exact manifest bytes 计算 `manifestIntegrity`；
7. 对同一 root 计算 `asharia-package-tree-v1` `payloadIntegrity`；
8. 生成 immutable `PackageCandidate`，其 `payloadLocation` 是 adapter-local root。

Loader 不修复、规范化写回或修改 manifest。Candidate 中保存的是独立的 validated manifest projection；调用方输入与 parser 临时
对象都不能被后续 resolver 修改。

Manifest 在 payload hashing 前后必须保持相同 exact bytes；可观察到的文件消失、读取失败或变化返回
`discovery.source.changed` 或更具体的 source/integrity diagnostic。普通 filesystem 无法提供跨整棵树的事务快照，因此此检查不
承诺消除 TOCTOU；它只证明 loader 实际观察到的一次同步快照。

### 6. `PackageCandidate` 属于共享边界，不属于 Resolver 实现

`PackageCandidate` 物理定义在 `tools/package_candidates.py`，语义所有者是 discovery/resolver 之间的 package contract。
`tools/package_resolver.py` 兼容 re-export 现有名称，避免破坏已有调用点。

Discovery location descriptors、discovery diagnostics 与 loader result 由 discovery adapter module 拥有。Resolver 只依赖共享
`PackageCandidate`，不能反向依赖 filesystem loader。此 Slice 不因此创建新的 native CMake target；未来生产
`engine/package-runtime` 仍按相同依赖方向落地。

### 7. Batch 结果是严格、原子且确定的

语义结果为：

```text
CandidateDiscoveryResult(
  candidates: tuple<PackageCandidate>,
  diagnostics: tuple<CandidateDiscoveryDiagnostic>
)
```

- 成功：`candidates` 非空或为空均合法，`diagnostics` 为空；candidates 按 source key 排序。
- 失败：任一位置无效时 `candidates` 必须为空，`diagnostics` 非空；不向 resolver 暴露部分 catalog。
- 空输入：成功返回两个空 tuples；空 Project 是否可解由 resolver 决定。
- Loader 不写文件、不缓存、不保留 watcher 或开放 file handle。

确定性范围是相同 descriptor 语义、相同 source mappings 与稳定 filesystem snapshot。Descriptor permutation、directory enumeration
order 和 dictionary insertion order 不得改变 candidate projection 或 rendered diagnostic bytes。

同一 `identity + exact version` 来自不同有效 source keys 时，loader 保留所有 records；它不定义 bundled/project/local
precedence。Resolver v1 继续按既有 ambiguity policy 决定失败。

### 8. Diagnostics 保留稳定来源上下文

Discovery-owned failures 使用 `discovery.*` namespace。第一版至少区分：

- invalid descriptor、duplicate source key 与 physical alias；
- unavailable/not-directory source、path escape 与 link/reparse point；
- missing/unreadable/changed manifest 或 payload；
- unsupported manifest/package kind；
- integrity path portability、case-fold collision 与 non-regular entry。

Schema/semantic validator 已有 codes 不重新命名；loader 为它们附加 stable source key 与 manifest-relative location。
Diagnostics 按 `(sourceKey UTF-8 bytes, code, relativeLocation, message)` 排序。Rendered message 不包含绝对路径、原始平台异常文本、
枚举序号或 object address；底层异常只映射到稳定分类。

### 9. Resolver 与 locked workflow 的交接

成功 candidates 可直接传给 `resolvePackageGraph()`。Resolver 不重新打开 root，也不重新计算 integrity。成功 resolution 返回的
`selectedCandidates` 保留 opaque `payloadLocation`，供后续 locked verification/planning boundary 使用。

Discovery 与 resolution 之间、resolution 与实际 build/activation 之间都可能发生文件变化。未来 locked workflow 必须在使用 selected
payload 前依据 lock 的 source/integrity evidence 重新验证；stale lock decision、minimal-change update、临时文件 replace、journal 与
recovery 都不属于本文。

## 拒绝的替代方案

### 扫描固定 `packages/`、`Packages/` 或 workspace 目录

拒绝。仓库布局会变成 source protocol；无关目录、权限错误、嵌套仓库和枚举顺序会改变候选集合。未来 bundled catalog、项目索引
与 registry acquisition 也无法共享同一边界。

### 由调用方直接构造 `PackageCandidate`

保留为 resolver 的低层测试接口，但不是 Package Manager 的可信生产路径。生产路径必须从 exact manifest bytes 和 payload tree
派生 metadata/integrity，不能接受调用方自由填充 evidence。

### 创建可提交的 candidate catalog

拒绝。现有 Candidate / Lockfile ADR 已冻结 candidate 为进程内边界。Catalog 来自 distribution、project 与 workspace state；只有
resolver 选中的 exact graph 能持久化为项目 lock。

### 在 discovery 时静默选择来源优先级

拒绝。Source precedence 是尚未设计的 user/workspace policy。v1 既不覆盖候选，也不根据 source kind 选择一个“更可信”的版本。

## 非目标

- bundled distribution catalog、project package index 或 workspace mapping 文件格式；
- candidate parent-directory enumeration 或 best-effort catalog UI；
- registry、git、download、credential、signature、publisher trust 或 license policy；
- version resolution、source precedence/override 或 resolver policy 修改；
- existing-lock verify/reuse/update、file transaction 或 recovery；
- Build Plan、Artifact Plan、Host Activation Plan、Activation Executor 或 Editor UI；
- native `package-runtime` target、production package publication layout 或 artifact file list。

## 实现与验证

#273 保持为一个独立、可回退 Slice：

- `tools/package_candidates.py` 拥有中立 candidate contract；`tools/package_candidate_discovery.py` 拥有 location descriptors、
  diagnostics、atomic result 与 strict loader；Resolver 兼容原导入路径；
- 实现覆盖三类 descriptors、stable source derivation、containment、link/junction、alias 与 strict batch result；
- 复用现有 manifest dispatcher/validators 与 integrity byte domains，不复制 schema 规则；
- 测试覆盖 candidate permutation、diagnostic byte determinism、input immutability、case-fold collision 与 observable
  source mutation；
- candidate → resolver → canonical lock → locked candidate integrity validator 合成交接测试已落地；
- 提交前仍运行 package contracts、topology、encoding、doc-sync、whitespace、ClangCL 与 MSVC gates。

后继 [Locked Graph Verification & Reuse v1](adr-package-lock-verification-v1.md) 已实现只读 exact graph 复用与 selected payload
重哈希；上游 catalog/index、lock update/apply 与 production Package Manager 仍未实现，不能由本 Slice 的 strict loader
推断为已落地。

## 后果

正向后果：未来 Package Manager 可以替换 source provider，而不修改 candidate、resolver 或 lock 合同；同一个 strict loader 能服务
CLI、Editor、CI 与测试；失败不会静默缩小可解版本集合。

代价：上游必须显式维护来源位置，首次 demo 不能只传一个父目录；大 payload hashing 是同步 IO；普通 filesystem snapshot 仍存在
TOCTOU，因此 locked verification 是强制后继边界而不是可选优化。

## 依据

- [Package Candidate / Lockfile v1](adr-package-candidate-lockfile-v1.md)
- [Deterministic in-memory Package Resolver v1](adr-package-resolver-v1.md)
- [Installable Package Manifest v2](adr-installable-package-manifest-v2.md)
- [Project Package Manifest v1](adr-project-package-manifest-v1.md)
- [Package-first 架构](package-first.md)
- GitHub #264、#270、#271、#272 与 #273
