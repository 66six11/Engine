# ADR: Core Caller-Owned Staged File Preparation v1

## 状态

Accepted for the Core staged-file preparation slice.

## 背景

[Core Staged File Replacement v1](adr-core-staged-file-replacement-v1.md) 已提供“已有 staged file
替换 existing target，并保留 caller-owned backup”的单文件恢复原语，但它明确不写入或 flush staged
bytes。Package Manifest/Lock apply 的上层 transaction 需要先准备 deterministic bytes，再进入 journal
和 replace 阶段；若继续使用 `writeFile*Atomically()`，Core 会隐藏 temporary 名称并立即发布 target，
上层无法在 commit 前验证 staged artifact，也无法把它纳入恢复记录。

因此 Core 增加一个 package-neutral preparation primitive。它只负责 caller-named staged file 的独占
创建、完整写入、文件内容 flush 和 close，不修改 target，也不拥有 Project/Lock schema、journal 或
跨文件事务。

## 决策

公共入口是：

```cpp
prepareStagedFileBytes(target, staged, bytes)
    -> StagedFilePreparationOutcome

prepareStagedFileText(target, staged, text)
    -> StagedFilePreparationOutcome
```

### 前置条件

- `target` 是已经存在的 regular file；本入口不负责首次发布。
- `target` 与 `staged` 非空、lexically distinct，并位于同一 normalized parent directory。
- `staged` 不得存在。实现必须使用 exclusive create，不能先检查再 truncate/overwrite。
- 调用者在 preparation、后续 replace 和 recovery 期间串行化 target/staged/backup namespace。
- 调用者提供的 bytes 已完成业务层 canonical render；Core 不解析 JSON、schema 或 fingerprints。

同目录要求把 staged 与 target 固定在同一 filesystem namespace，并为后续 replacement 排除普通
cross-volume 路径。它不是 hard-link/case-alias、parent symlink 或 hostile-directory 安全证明；
上层仍必须使用自己拥有且隔离的 project directory 和 writer lock。

### 成功

成功由以下条件共同表示：

- `error` 为空；
- `stagedFileState == Present`；
- staged 已接收全部 bytes；
- staged file handle 已完成 `fsync` / `FlushFileBuffers` 并关闭；
- target 未被本入口修改。

空 bytes 是合法文件内容，也必须经过 create、flush 和 close。成功后 staged 归调用者所有，不会在
RAII cleanup 中被删除；调用者可重新读取/hash/验证后再调用 `replaceFileFromStaged()`。

### 失败与 partial artifact

创建失败时实现重新观察 staged path，并返回 `Absent`、`Present` 或 `Indeterminate`。`Present`
常见于另一个 writer 已独占该名称；本入口绝不覆盖它。

一旦 exclusive create 成功，staged namespace 立即成为 caller-owned。后续 partial write、零进度、
非法 backend progress、flush 或 close 失败都：

- 返回结构化 `Error`；
- 保留已经创建的 partial staged artifact，不自动删除；
- 返回 staged path 的最后一次 presence 观察；
- 不调用 replacement，不修改 target。

保留 partial artifact 是显式恢复证据，不表示其 bytes 可提交。调用者必须验证 `error` 为空后才可把
staged 交给 replacement；失败 artifact 的删除、隔离或诊断保留由更高层 recovery policy 决定。

## 平台映射

### Windows

- 用 `GetFileAttributesW` 要求 target 是非 reparse regular file。
- 用 `CreateFileW(..., CREATE_NEW, FILE_ATTRIBUTE_NORMAL, ...)` 独占创建 exact staged path；
  existing file、directory 或 reparse point 均不会被 truncate。
- 循环 `WriteFile` 直到所有 bytes 完成，然后调用 `FlushFileBuffers` 和 `CloseHandle`。
- 写入 handle 不共享给其他进程；这只约束该 staged file handle，不替代上层 transaction lock。

### POSIX

- 用 `lstat` 要求 target 是 regular file，不跟随 target symlink。
- 用 `open(O_WRONLY | O_CREAT | O_EXCL)` 独占创建 exact staged path；可用时同时设置
  `O_CLOEXEC | O_NOFOLLOW`。
- staged 复制 target 的普通 permission bits，然后循环 `write`，调用 `fsync` 和 `close`。
- `O_CREAT | O_EXCL` 的 existence check 与 create 对同一路径是原子的，并在 staged 是 symlink 时
  fail closed。

## Unreal Engine 参考映射

Unreal Engine 的公开 API 体现的是分层，而不是由一个 `atomic-file` helper 拥有所有保存语义：

- Runtime `Core` 的
  [`IPlatformFile`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/IPlatformFile?lang=en-US)
  暴露 physical file operations；`OpenWrite` 产生 file handle，`MoveFile` 默认不覆盖已有目标。
- [`EFileWrite::FILEWRITE_NoReplaceExisting`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/EFileWrite?lang=en-US)
  将“不得覆盖现有名称”作为显式 write policy，而不是先检查再 truncate。
- [`IFileHandle::Flush(bool bFullFlush)`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/GenericPlatform/IFileHandle/Flush?application_version=5.5)
  区分普通 flush 与要求数据、metadata 落盘的强 flush；序列化便利层则由
  [`FArchive`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FArchive?lang=en-US)
  提供 `Flush()` / `Close()`。
- Editor crash recovery 不反向进入低层 file API。
  [`IPackageAutoSaver`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/IPackageAutoSaver)
  在 `UnrealEd` 中拥有 restore file、启动恢复判定与用户提示；
  [`EAutoSaveMethod`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/EAutoSaveMethod)
  进一步区分 backup-and-restore 与 backup-and-overwrite policy。

Asharia 借鉴这一 ownership split：Core 只提供可组合的 file primitive，包括独立的
[Exclusive File Lock v1](adr-core-exclusive-file-lock-v1.md)；Project/Lock apply owner 负责选择 stable
sentinel、持有锁覆盖 revalidation/commit，并拥有 journal 与 recovery；Avalonia Studio 只呈现恢复状态和用户动作。本入口
不照搬 UE 的全局 `IFileManager`、bool-only failure surface 或完整 platform-file wrapper stack，
而继续使用 project `Result/Error` 与可测试 backend。UE 的单文件 API 也不构成 Project/Lock
双文档 crash atomicity 或 parent-directory durability 的证明。

## Durability 与非目标

本入口只同步 staged file 内容和取得该内容所需的 file metadata。它不负责：

- flush parent-directory entry；
- 保证所有 filesystem、storage controller 或断电模型下的物理持久化；
- process/file lock、reader gate、lease 或 stale-plan fingerprint revalidation；
- backup、replace、journal、rollback、crash recovery 或 multi-document atomicity；
- 首次创建 target；
- canonical JSON、Project Manifest/Lock schema、hash 或 signature。

一个完整的 Project/Lock apply 至少还需要：取得 stable project writer lock、在锁内重新读取并验证 #303 plan
preconditions、两份 canonical staged documents、durable prepared journal、按 journal 状态执行 replace、
目录 metadata 同步，以及启动期 recovery。单独调用本入口或紧接一次
`replaceFileFromStaged()` 都不得被描述成跨文件原子事务。

## 验证

Core file-I/O tests 覆盖：

- 空路径、lexical alias 与不同 parent 在 backend 前拒绝；
- partial writes 完整推进，且 success 必经 flush/close；
- write failure 保留 partial staged artifact 与旧 target；
- existing staged artifact 不被覆盖；
- Windows 与 ClangCL/MSVC backend 的真实 preparation → replacement handoff；
- 原有 atomic write 和 staged replacement 行为不回归。

POSIX create/write/fsync 分支由同一 injectable contract 和 conditional compilation 覆盖；Windows
本地验证不能替代 Linux CI 对真实 POSIX syscalls 的执行。

## 外部依据与案例

- [Microsoft `CreateFile` / `CREATE_NEW`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
- [Microsoft `FlushFileBuffers`](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers)
- [POSIX `open()` / `O_CREAT | O_EXCL`](https://man7.org/linux/man-pages/man3/open.3p.html)
- [Linux `fsync(2)`](https://man7.org/linux/man-pages/man2/fsync.2.html)
- [Git lockfile API](https://git-scm.com/docs/api-lockfile)：以 exclusive `.lock` create 区分 writer，
  写完后才 rename commit。
- [Godot Windows temporary-save failure case](https://github.com/godotengine/godot/issues/956)：
  删除旧文件再 rename 的失败会留下 `.tmp` 并让 canonical path 缺失，说明 partial artifact 必须由
  显式 outcome/recovery 协议拥有。
- [SQLite Atomic Commit](https://www.sqlite.org/atomiccommit.html)：journal flush 顺序与 recovery
  判定是多文件 crash consistency 的独立层，不能从单个 staged-file flush 推导。
- [All File Systems Are Not Created Equal (OSDI 2014)](https://www.usenix.org/conference/osdi14/technical-sessions/presentation/pillai)：
  application update protocol 依赖细微且跨 filesystem 不一致的 persistence properties，因此 v1 只声明
  已实际调用的 file-level primitive，不泛化为通用断电保证。
- [CrashMonkey (HotStorage 2017)](https://www.usenix.org/conference/hotstorage17/program/presentation/martinez)：
  后继 journal/recovery Slice 应增加 fault-point/crash-state 验证，而不能只以正常路径单元测试证明
  crash consistency。
