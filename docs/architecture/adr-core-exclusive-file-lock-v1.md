# ADR：Core Exclusive File Lock v1

状态：Accepted for the Core writer-exclusion primitive slice.

## 背景

[Package Lock Apply Preconditions v1](adr-package-lock-apply-preconditions-v1.md) 已要求 apply 在
project writer exclusion 持有期间重新读取 Project/Lock/Distribution/candidates，并对现有 update
plan 做无 resolver 的 seal/current-facts revalidation。Core 也已经提供 caller-owned staged
preparation 和可恢复单文件 replacement，但此前没有跨进程 writer exclusion。

这里需要解决的是“同一台机器上，所有合作进程是否只有一个 writer 进入临界区”，不是：

- 用锁文件内容决定 Project/Lock 业务状态；
- 用单文件锁替代 journal、目录 flush 或跨文件 recovery；
- 阻止绕过 Asharia API 的 hostile writer；
- 为网络文件系统、云同步目录或分布式 lease 提供未验证的安全承诺。

## 先例与取舍

| 先例 | 可复用语义 | 本 ADR 的取舍 |
| --- | --- | --- |
| Unreal [`FSystemWideCriticalSection`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/GenericPlatform/FSystemWideCriticalSectionNotImp-?application_version=5.5) 与 [`NewInterprocessSynchObject`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FGenericPlatformProcess/NewInterprocessSynchObject) | 跨进程同步是 Core/platform primitive；owner 用 RAII/显式 release 管理占用 | 保留相同 owner 分层，但用 caller-provided stable path 作为 project-scoped identity，不引入 UE 的全局 HAL/process facade |
| Git [lockfile API](https://git-scm.com/docs/api-lockfile) | exclusive create 很适合“写临时文件后 rename commit”的短事务 | 不把“文件存在”当锁状态；硬崩溃遗留 `.lock` 后需要 stale cleanup，不能直接满足长期稳定 sentinel |
| O3DE [`AZ::IO::SystemFile`](https://github.com/o3de/o3de/blob/development/Code/Framework/AzCore/AzCore/IO/SystemFile.h) 与 Godot [`FileAccess`](https://github.com/godotengine/godot/blob/master/core/io/file_access.cpp) | 引擎 Core IO 应提供平台差异后的普通文件原语；高层 owner 组合保存策略 | 它们用于交叉检查 IO owner 分层，不把普通 create/temporary-file API 误当 writer-exclusion contract |
| Windows [`LockFileEx`](https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-lockfileex) | 非阻塞 exclusive byte-range lock；handle close 或进程退出后由 OS 解锁 | Windows backend 锁定 sentinel 的 byte 0，长度 1 |
| Linux [`flock(2)`](https://man7.org/linux/man-pages/man2/flock.2.html) | `LOCK_EX | LOCK_NB` 表示非阻塞独占锁；所有关联 descriptor 关闭后释放 | POSIX backend 使用 open-file-description scoped `flock`，不采用容易受同进程其他 `close` 影响的传统 process-scoped `fcntl` lock |

结论：采用“持久 sentinel + 内核锁”，不采用“sentinel 存在即占锁”。

## Core contract

公开入口：

```cpp
Result<std::optional<ExclusiveFileLock>>
tryAcquireExclusiveFileLock(const std::filesystem::path& lockPath);
```

三态含义固定：

- `Result` success + engaged `optional`：已取得锁；
- `Result` success + empty `optional`：另一合作 writer 正在占用，不是 IO failure；
- `unexpected(Error)`：路径、权限、file kind、platform syscall 或资源失败。

`ExclusiveFileLock` 是 move-only RAII owner：

- `ownsLock()` 只报告当前对象是否仍拥有内核锁；
- `release()` 显式 unlock/close 并返回可观察错误；
- 析构函数 best-effort release，不能抛异常；
- move 后只有目标对象保留 ownership。

sentinel 规则：

- 缺失时由 backend 创建；
- release 后故意保留；
- 文件存在不代表被占用，内容也不是 authoritative owner evidence；
- 所有合作 writer 必须使用同一稳定路径；
- caller 不得在任何 writer 可能存在时删除、rename 或 replace sentinel。

Core 不规定 sentinel 文件名。Project/Lock application service 将在后继 Slice 冻结 project-root
相对位置，并负责把 lock lifetime 包住 revalidation、journal、commit 和 recovery。

## 平台行为

### Windows

- `CreateFileW(..., OPEN_ALWAYS, ...)` 打开或创建 sentinel；
- 允许其他 writer 同时 open 以便由 `LockFileEx` 区分 contention，但不授予 delete share；
- 用 `FILE_FLAG_OPEN_REPARSE_POINT` 打开并拒绝 directory/device/reparse point；
- `LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY` 锁定 `[0, 1)`；锁范围可以越过空文件 EOF；
- `ERROR_LOCK_VIOLATION`/同步路径上的 `ERROR_IO_PENDING` 映射为普通 contention；
- 显式 `UnlockFileEx` 后 `CloseHandle`；即使进程异常退出，OS 也会在关闭 handle 后释放锁。

### POSIX

- `open(O_RDWR | O_CREAT)`，可用时增加 `O_CLOEXEC | O_NOFOLLOW`，新文件 mode 为 `0600`；
- `fstat` 拒绝非 regular file；
- `flock(LOCK_EX | LOCK_NB)`；`EWOULDBLOCK`/`EAGAIN` 映射为普通 contention；
- 取得锁后用 `lstat` 复核 path 仍指向同一 `st_dev + st_ino`；
- `flock(LOCK_UN)` 后 `close`；descriptor 全部关闭时内核释放锁。

POSIX advisory lock 不能阻止不合作进程修改数据或 unlink sentinel。因此 v1 的安全域明确限制为
cooperative writers + 已验证本地文件系统。Linux CI 仍需执行真实 POSIX syscall branch；Windows
本地编译不能替代它。

## Editor / 前端框架边界

当前 `apps/studio` 使用 Avalonia 12.0.4。Avalonia
[`IStorageProvider`](https://docs.avaloniaui.net/docs/services/storage/storage-provider) 应负责：

- open/save/folder picker；
- 平台 capability 查询；
- sandbox/bookmark 或用户授予的 storage item；
- UI owner window 与异步交互。

它不应成为 Project/Lock、scene 或 asset metadata 的 transaction owner。Avalonia 官方
[File Dialogs](https://docs.avaloniaui.net/docs/services/file-dialogs) 也建议真实应用通过
service + DI 隔离 file picker，而不是让 ViewModel/View 直接拥有文件业务。

因此 Studio 的长期调用方向是：

```text
View / ViewModel
  -> Avalonia storage-dialog service（只选择/授予位置）
  -> Studio application command/service（校验、进度、取消、diagnostics）
  -> shared project/persistence service（writer lock、revalidation、journal、commit/recovery）
  -> Core file primitives / platform adapter
```

普通“导入/导出一个用户选择的非权威文件”可以直接消费 `IStorageFile` stream；Project/Lock 等
权威文档不能用 Avalonia stream 或散落的 `System.IO` 写入绕过共享 transaction contract。
CLI、恢复工具和 Editor 必须得到相同的 contention/failure/recovery 语义。

## 正确调用顺序

```text
confirmed immutable preview
  -> try acquire stable project writer lock
  -> reread current Project/Lock/Distribution/candidates
  -> revalidate existing plan seal and current facts
  -> prepare staged bytes
  -> persist/flush journal
  -> replace targets and recover as required
  -> release writer lock
```

锁必须覆盖最后一次 revalidation 到 transaction 完成或进入可诊断 recovery 状态的整个区间。
锁本身不证明 revalidation 成功，也不把两个 rename 变成原子操作。

## 验证

Core file-I/O tests 覆盖：

- 空路径在 backend 前拒绝；
- acquired / contended / failed 三态；
- move-only ownership、显式 release 与 release-error RAII fallback；
- 同进程两个独立 platform handle 的真实 contention；
- 显式 release 与析构后可重新取得；
- release 后 sentinel 保留；
- directory 与 missing-parent 拒绝。

## 后继工作

1. 冻结 Project/Lock application service 的 sentinel 相对路径与 contention diagnostic；
2. 把 current-facts reread + apply-precondition revalidation 放入锁的 lifetime；
3. 定义 durable journal、目录 flush、双文档 commit point 与 crash recovery matrix；
4. 为 Avalonia Studio 增加 storage-dialog abstraction 和 application command，但不在 View/ViewModel 中复制事务；
5. 在 Linux CI 执行真实 `flock` tests，并单独评估 network/synced filesystem policy。
