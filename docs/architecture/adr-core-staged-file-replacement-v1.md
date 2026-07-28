# ADR: Core Staged File Replacement v1

## 状态

Accepted for the Core staged-file replacement slice.

## 背景

`writeFileBytesAtomically()` 与 `writeFileTextAtomically()` 适合“给定 bytes，创建内部临时文件，
然后发布目标文件”的普通写入路径。它们有意隐藏内部 temporary/backup 路径，并在能够确定安全时
自动清理这些文件。

需要显式恢复证据的上层调用者不能使用该隐藏所有权模型：调用者已经写好并验证 staged file，
需要自己选择 backup 路径，并且必须区分“未提交”“已提交”和“提交状态无法判定”。Core 因此增加
单文件、package-neutral 的 recoverable replacement primitive；Core 不拥有多文件事务、journal、
业务 schema 或重试策略。

后继 [Core Caller-Owned Staged File Preparation v1](adr-core-staged-file-preparation-v1.md) 已补齐
caller-named staged artifact 的 exclusive create、完整写入、file flush 与 close；两者仍是独立
primitives，不共同宣称 Project/Lock 跨文件原子性。

## 决策

公共入口是：

```cpp
replaceFileFromStaged(target, staged, backup)
    -> StagedFileReplacementOutcome
```

### 前置条件

- `target` 是已经存在的 regular file；缺失 target 会 fail closed，本入口不负责首次发布。
- `staged` 是已经存在的 regular file，其 bytes、权限和 flush 状态由调用者负责；可由
  `prepareStagedFileBytes/Text()` 产生。
- `backup` 不得存在。
- 三条路径非空、互不相同、不得通过 symlink/hard-link/case alias 指向同一文件。
- 三条路径位于同一 volume/filesystem。
- 调用者在整个调用与恢复期间串行化这三条路径的其他 writer。

Core 会拒绝可直接识别的空路径、lexical alias、缺失/非 regular staged、缺失/非 regular target
以及已有 backup。非 lexical alias 与外部 writer 仍属于调用者约束；本 API 不提供目录锁或
进程级事务锁。

### 成功与 artifact 所有权

平台确认替换成功后：

- `commitState == Committed`；
- `target` 引用原 staged file；
- `backup` 保留原 target；
- `error` 为空。

若返回前的 artifact presence 观测也成功，则 `stagedFileState == Absent` 且
`backupFileState == Present`；presence 观测本身失败时，对应字段为 `Indeterminate`，
但不会推翻平台已经确认的 `Committed`。

公共入口不会删除 caller-owned staged 或 backup artifact。平台操作遇到部分失败时也只执行恢复
旧 target 所必需的动作。调用者根据 outcome 和自己的内容校验决定何时清理或重试。

原有 `writeFile*Atomically()` 继续保留兼容行为：它们仍可创建不存在的 target，并继续拥有和清理
内部生成的 temporary/backup。

### Outcome 语义

`StagedFileCommitState`：

| 状态 | 含义 | 调用者约束 |
| --- | --- | --- |
| `NotCommitted` | Core 已确认 staged 没有成为 target | 可在重新校验路径与内容后决定重试或清理 |
| `Committed` | Core 已确认 staged 成为 target | backup 仍由调用者保留，直到上层确认不再需要恢复 |
| `Indeterminate` | Core 无法证明 target 最终指向哪份证据 | 不得删除或覆盖 target/staged/backup；先重新读取并校验三者 |

`StagedFileArtifactState` 只表示函数返回前对 staged/backup 路径存在性的最后一次观测：

- `Present`：最后一次观测存在；
- `Absent`：最后一次观测缺失；
- `Indeterminate`：观测本身失败。

它不是文件内容、identity 或 durability 证明。外部 writer 可以在函数返回后立即使该观测过期。
`NotCommitted` 与 `Indeterminate` 带结构化 `Error`；当前实现确认 `Committed` 时 `error` 为空。

## 平台映射

### Windows

已存在 target 使用 `ReplaceFileW(target, staged, backup, ...)`：

- 成功：staged 被消费，原 target 保留在 caller-owned backup；
- `ERROR_UNABLE_TO_REMOVE_REPLACED` (1175)、带 backup 的
  `ERROR_UNABLE_TO_MOVE_REPLACEMENT` (1176) 与普通 pre-commit failure：
  返回 `NotCommitted`，并在返回前重新观测 staged/backup；
- `ERROR_UNABLE_TO_MOVE_REPLACEMENT_2` (1177)：Windows 已把旧 target 移到 backup，
  但 staged 尚未成为 target。Core 尝试无覆盖地把 backup 移回 target；
  - restore 成功：`NotCommitted`，staged 保留，backup 被 restore 消费；
  - restore 失败：`Indeterminate`，保留并重新观测 staged/backup。

已有 backup 在进入 `ReplaceFileW` 前被拒绝。不同 volume 会由平台失败并保持 fail closed。

### POSIX

已存在 target 使用两步 primitive：

1. `link(target, backup)` 创建 caller-owned old-target evidence；
2. `rename(staged, target)` 原子替换可见 target 名称。

若 backup link 失败，target/staged 不变，并重新观测 backup。若 rename 因 `EXDEV` 或其他错误
失败，POSIX 保证已存在 target 仍在原名称；staged 与已经创建的 backup 均保留，返回
`NotCommitted`。成功时 staged 名称被消费，backup hard link 保留原 target。

该合同面向支持普通 local regular-file link/rename 语义的 filesystem。远程 filesystem 若不能
提供这些语义，调用者必须把结果视为不可用于其恢复保证。

## Durability 与非目标

本入口只负责路径替换与恢复 evidence，不负责：

- 写入、验证或 `fsync`/`FlushFileBuffers` staged bytes；
- flush parent-directory metadata；
- 断电后的 crash consistency；
- 多文件原子性、journal、reader gate 或 recovery state machine；
- 文件内容 hash、schema、业务语义、重试或 artifact 清理策略；
- 首次创建 target。

需要 crash durability 的调用者必须在调用前完成 staged 内容与 flush 协议，并在更高层建立
journal、reader serialization、恢复校验和目录持久化策略。

## 验证

Core file-I/O tests 覆盖：

- public success、missing target、existing backup 与 lexical alias；
- Windows 1175、1176、1177 restore success/failure、backup cleanup compatibility 与
  cross-volume error；
- POSIX backup-link failure、`rename == EXDEV`、backup race re-observation 与成功保留 backup；
- 原有 atomic-write create/write/flush/close/replace 行为不回归。

## 依据

- [Microsoft `ReplaceFileW`](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew)
- [Linux/POSIX `rename(2)` behavior](https://man7.org/linux/man-pages/man2/rename.2.html)
