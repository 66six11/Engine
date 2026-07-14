# ADR：Installed Distribution Repair Verifier v1

## 状态

Accepted for #283。

本 ADR 建立在以下已实现合同上：

- #278 immutable Package Artifact Generation；
- #279 Engine Distribution Manifest v1；
- #281 Effective Session v1；
- #282 immutable Engine Distribution Assembly v1。

本 Slice 只实现已安装 generation 的只读深度复验与结构化健康报告，不执行下载、覆盖、删除、隔离、active-generation 选择、项目构建、重启或动态加载。

## 问题

Distribution Assembler 能证明其刚刚 staged 或复用的 generation 与进程内 assembly request、manifest 和 artifact publication receipts 一致，但安装后的 Editor Image 或 Launcher 不再拥有这些进程内对象。仅凭目录名或磁盘上的 Distribution Manifest 自报的 ID 也不能建立信任：攻击或损坏可以同时改写二者，形成内部一致但不符合调用方选择的另一份库存。

已安装 generation 因此需要独立边界回答：

- 调用方预期的是哪一个 exact `EngineGenerationId`；
- 磁盘 manifest 是否仍是该 ID 对应的 canonical inventory；
- Editor Image、bundled package、artifact generation 和 Host Profile 是否仍与 inventory 一致；
- generation 是否仍是 closed tree；
- 哪些 owner/path 需要由后续 Repair Executor 恢复。

Verifier 不能把“发现损坏”和“获准修复”混成一个 API。诊断可以由固定 Editor Image 或外部 Launcher 调用；修改安装目录必须由拥有安装来源、事务与回滚策略的后继组件执行。

## 所有权

| Owner | 拥有 | 不拥有 |
| --- | --- | --- |
| Launcher / installer selection | exact expected `EngineGenerationId` 与 generation root | 磁盘库存的真实性结论 |
| Installed Distribution Repair Verifier | 只读深度复验、稳定 findings、`DistributionHealthReport` | 修复动作、active pointer、项目 graph、进程重启 |
| Engine Distribution Manifest | generation 的 canonical inventory | 自身可信来源、修复命令、安装来源 |
| 后继 Repair Executor | 获取并原子恢复完整 generation | 本 Slice 不实现 |
| 后继 Bootstrap / Session adapter | 消费 `VerifiedInstalledDistribution` 或 `RepairRequired` | 本 Slice 不接入启动状态机或 Effective Session composer |

Verifier 位于 headless package/build-support boundary。它不依赖 Editor UI、Renderer、VFS、项目 Package Lock、Host Runtime、Factory/Activation 或 native module loader。

## 决策

### 1. request 必须携带外部信任锚

唯一入口是 process-local typed request：

```text
InstalledDistributionVerificationRequest
  generationRoot: Path
  expectedEngineGenerationId: sha256-<64 lowercase hex>
```

`expectedEngineGenerationId` 必须来自 Launcher、installer inventory 或已经选择的 activation record。Verifier 不从磁盘 manifest、目录 basename、`current/latest` 指针或最高版本推导它。

request 类型、`Path` 类型或 expected ID 非法属于调用错误。此时返回空 report 与 diagnostics，并且在验证 request 前不得访问文件系统。

### 2. report 只表达健康事实

v1 健康状态只有：

- `Healthy`：完整 closed-tree 深度复验成功；
- `RepairRequired`：有效 request 指向的 generation 缺失、不可信或损坏。

`FatalDistributionError` 不是磁盘 verifier 能决定的状态。它表示固定 Bootstrap/Launcher 本身无法继续提供诊断控制面，应由更外层启动状态机决定。

```text
InstalledDistributionVerificationResult
  report: DistributionHealthReport | none
  diagnostics: Diagnostic[]

DistributionHealthReport
  state: Healthy | RepairRequired
  expectedEngineGenerationId
  observedEngineGenerationId | none
  verifiedDistribution | none
  findings: Diagnostic[]
```

只有 `Healthy` report 携带 `VerifiedInstalledDistribution`。`RepairRequired` 始终没有 verified handoff。调用错误使用 result-level diagnostics；有效安装请求的健康问题使用 report findings，避免把“API 用错”和“磁盘损坏”混为一种状态。

report 和 handoff 只在进程内存在。v1 不持久化第二份 inventory、repair plan、命令、绝对路径、时间戳或环境快照。

### 3. Distribution Manifest 是待验证库存，不是信任锚

验证顺序固定：

1. 先验证 typed request 与 expected ID；
2. 逐路径组件检查 root，不允许 symlink、junction/reparse 或 non-directory ancestor；
3. 检查 root basename 等于 expected ID；
4. 从 exact root 读取 `asharia.engine-distribution.json`；
5. 验证 UTF-8/no-BOM、JSON、schema、semantics 与 canonical exact bytes；
6. 对证 manifest declared ID、canonical content-derived ID、root basename 与 expected ID；
7. 只有库存可信后，才解释其下游 file/package/artifact/profile references。

manifest 缺失或不可信时，report 只给出 manifest-scope finding，不继续猜测下游 owner。这样避免损坏 inventory 驱动任意路径读取或产生误导性 repair 建议。

### 4. 完整复验所有 transitive evidence

可信 manifest 建立库存后，Verifier 必须复验：

- Editor Image：每个 declared file 的 regular-file、size 与 streaming SHA-256；
- bundled package：author manifest exact-byte integrity、schema、package identity/version/kind，以及完整 `asharia-package-tree-v1` payload integrity；
- package artifact generation：从磁盘 canonical per-package manifests 重建 typed evidence，重算 manifest-set identity、generation ID、closed layout 与每个 artifact file 的 size/SHA-256；
- Host Profile：exact-byte integrity、UTF-8 JSON、schema、host kind 与 target platform；
- whole generation：所有文件必须由 manifest exact file、bundled package root 或 verified artifact root 拥有，且目录集合必须恰好等于文件 parent closure。

bundled package tree hash 的 domain 会忽略 top-level `.git/.hg/.svn/build/generated`，但这些内容不属于发行 payload。Installed verifier 必须显式拒绝它们，不能让 hash exclusion 变成 installed-tree 漏洞。

### 5. artifact generation 从磁盘重新建立 evidence

#278 的 `PackageArtifactPublicationReceipt` 是 publication 当时的进程内 handoff，不会随安装自动持久化。Installed verifier 不伪造 receipt，也不依赖旧 Python 对象。

`verify_published_package_artifact_generation()` 从调用方指定的 artifact generation root 和 expected generation ID 开始：

1. 发现 `packages/<package-id>/asharia.package.artifacts.json`；
2. 验证每份 manifest 的 UTF-8 JSON、schema、self-integrity 与 exact canonical bytes；
3. 重建 immutable `PackageArtifactManifest` tuple；
4. 重算 manifest-set integrity 与 content-derived artifact generation ID；
5. 对证 expected ID、root basename 与重算 ID；
6. 验证 closed files/directories 和所有 artifact hashes；
7. 仅成功时返回 `VerifiedPackageArtifactGeneration`。

这是 read-only disk loader，不是 publication API，也不拥有 staging cleanup。

### 6. findings 稳定且不携带执行授权

findings 使用稳定 code、manifest-relative path或 JSON pointer，并按 `(manifestPath, pointer, code, message)` 确定排序。消息不包含绝对安装路径、时间戳、环境变量、shell command 或下载 URL。

可信 inventory 建立后，Verifier 可以收集多个互相独立的 finding；但任何一个 finding 都使状态成为 `RepairRequired`，并阻止 verified handoff。finding 只描述 observation 与 owner，不规定覆盖、删除或单文件修补动作。

### 7. 复验全程只读且使用 bounded streaming

payload hashing 使用固定 1 MiB 上限的 streaming read，并在 open 前后对证 file identity/size，检测合作式并发环境中的漂移。验证代码不调用 `Path.read_bytes()` 读取 payload，不创建临时目录，不写 report，不删除 extra entry，也不清理 staging。

v1 不承诺对 hostile concurrent writer 提供 sandbox 安全；调用方仍应 quiesce installation mutation。任何观察到的 drift 都 fail closed 为 `RepairRequired`。

## 失败合同

主要 diagnostic family：

- `distribution.repair.request-*`：调用错误，result-level，无 report；
- `distribution.repair.root-*`：root missing/invalid/link/name mismatch；
- `distribution.repair.manifest-*`：missing/read/encoding/JSON/schema/canonical/generation ID；
- `distribution.repair.editor-*`：Editor file missing/type/size/hash/drift；
- `distribution.repair.package-*`：author manifest、identity、payload 或 excluded content；
- `distribution.repair.artifact-*`：disk generation、manifest/context/layout/hash；
- `distribution.repair.profile-*`：Host Profile bytes/schema/context；
- `distribution.repair.layout-*`：whole-generation missing/extra/empty/link/special entry。

有效 request 即使 root 或 manifest 不存在，也返回 `RepairRequired` report；它不是 verifier 的内部异常。只有 malformed request 返回空 report。

## 被拒绝的方案

### 只信磁盘 manifest 与目录名

拒绝。二者可能同时被替换；必须与调用方拥有的 expected ID 对证。

### 复用 assembler 的 in-memory verifier

拒绝作为 public installed API。assembler verifier 需要 expected manifest 与 publication receipts，恰好是安装后不存在的对象。可以复用纯合同与低层 hashing 语义，但不能伪造 process-local handoff。

### 启动时每次全量 hash

拒绝作为默认启动策略。v1 提供显式 deep Verify/Repair boundary；后继 Slice 可以用受约束的 receipt/identity 快速路径，但没有证据时不得声称已深度验证。

### verifier 发现问题后原地覆盖文件

拒绝。generation 是不可变修复单元；恢复需要安装来源、事务、并发与回滚政策，属于 Repair Executor/Launcher。

### 由 verifier 选择另一个 generation

拒绝。active generation selection 是 Launcher/installer policy，不是健康检查的职责。

## 后果

- Editor Bootstrap 或 Launcher 可以在不加载项目包图的情况下解释安装损坏；
- 后继 Bootstrap / Session adapter 可以只接收深度验证过的 installed Distribution；
- artifact publication receipt 与 installed disk evidence 的生命周期边界明确；
- 深度复验会读取全部 bytes，因此只在显式 Verify/Repair、安装、缓存恢复或需要重建信任时执行；
- 实际修复、Launcher/Installer 与快速启动 receipt 仍保持独立 Slice。

## 验证

- valid assembler generation、input immutability 与 expected-ID trust anchor；
- invalid request 在 filesystem access 前失败；
- root/manifest missing、link/reparse、invalid UTF-8/JSON/schema/canonical bytes；
- Editor、package、artifact、profile 与 whole-layout 独立 fault injection；
- disk-only artifact manifest reconstruction、generation ID、closed layout 与 hash；
- multiple findings 的稳定排序与 manifest trust-boundary stop；
- large-file bounded streaming 与成功/失败路径 read-only mutation guards；
- Engine Distribution、artifact publication、Effective Session 与全量 package-runtime regression；
- repository encoding、docs、topology、asset、Vulkan review 与双编译器 gates。

## 后续

1. Repair Executor / Launcher installer transaction；
2. active generation selection 与 rollback；
3. 轻量 startup verification receipt；
4. CMake install adapter；
5. 静态 composition root 与 Factory/Activation。
