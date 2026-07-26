# ADR: Package Lock Apply Preconditions v1

## 状态

Accepted for the no-write apply-precondition reference slice.

## 背景

[Package Lock Update Plan 与 Impact Preview v1](adr-package-lock-update-plan-v1.md) 已产生 immutable
`PackageLockUpdatePlan`：它绑定 base/proposed Project、base/proposed Lock、current Distribution、
complete/selected candidates、request、impacts 与最终 `planIntegrity`。计划完成到真正写盘之间仍可能
发生三类漂移：

- Project Manifest 或 Package Lock 被另一个 Editor、CLI 或外部工具修改；
- Engine Distribution generation 或 complete candidate snapshot 已变化；
- 调用方传入的 plan backing snapshots、policy、selected candidates、impacts 或 integrity records
  不再与原始 sealed payload 一致。

Apply 不能因计划曾经成功就直接准备 staged files，也不能通过重新运行 resolver 来静默产生另一张图。
因此先增加一个纯内存、无 IO、无 resolver 的 revalidation boundary。它是后继 writer-lock /
journal transaction 的前置组件，不是完整 apply。

## 引擎案例优先核对

本功能先核对具有相近职责的引擎实现：

- Unreal Engine 把 physical file operations 放在
  [`IPlatformFile`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/IPlatformFile?lang=en-US)，
  跨进程 exclusion 由
  [`FGenericPlatformProcess::NewInterprocessSynchObject`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FGenericPlatformProcess/NewInterprocessSynchObject)
  提供，而 Editor package restore 由
  [`IPackageAutoSaver`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/IPackageAutoSaver)
  拥有。Asharia 借鉴的是 low-level IO、process exclusion 与 Editor recovery policy 的 owner 分离。
- O3DE
  [`AZ::IO::SystemFile`](https://github.com/o3de/o3de/blob/development/Code/Framework/AzCore/AzCore/IO/SystemFile.h)
  将 `SF_OPEN_CREATE_NEW`、`Flush()` 与 `Rename(..., overwrite)` 保持为显式低层 primitives；这些能力
  不替代产品级 plan staleness check。
- Godot
  [`FileAccess::create_temp(..., keep)`](https://github.com/godotengine/godot/blob/master/core/io/file_access.cpp)
  明确区分自动删除与保留 temporary evidence；历史
  [Windows safe-save failure](https://github.com/godotengine/godot/issues/956) 说明 temp/rename 的正常路径
  不能代替失败状态和 recovery ownership。

这些引擎没有 Asharia 的 Project/Lock 双文档、candidate snapshot 或 domain-separated update-plan
contract，因此 plan seal 与 optimistic preconditions 必须由 Asharia 定义。这里不把“没有可直接复制
的类”解释为可以忽略引擎案例，而是先复用其成熟的 owner 分层，再补 Asharia 特有语义。

## 决策

Python reference oracle 增加：

```python
revalidate_package_lock_update_plan(
    plan,
    current_project,
    current_lock,
    distribution,
    candidates,
    validators,
) -> PackageLockUpdatePlanRevalidationResult
```

调用方必须从当前 owner 重新取得 Project、Lock、verified Distribution 与 complete candidate
snapshot 后再调用。该函数本身不打开路径，因此不能把旧的 in-memory dictionaries 描述成“已重新读取
磁盘”。

成功结果满足：

- `diagnostics` 为空；
- `plan_integrity` 非空且等于重新计算的 complete canonical plan integrity；
- current Project、Lock、Distribution 与 candidate multiset 的 domain-separated integrities 仍与
  plan 相同；
- current Lock 仍绑定 current Project Manifest；
- plan backing base/proposed documents 仍可解析、通过 contract validation 并保持 canonical bytes；
- request/update policy 与 resolver policy version 仍匹配；
- selected-candidate projection、proposed Lock、impacts、change flags、status、Engine generation 与各
  component integrity 仍能重建原 plan seal；
- proposed Lock 与 current verified Distribution、complete candidates、selected candidates 之间的
  exact output contract 仍成立。

任一条件失败时原子返回稳定 `apply.precondition.*` diagnostics，`plan_integrity` 为空。失败不返回
“部分可应用”的 proposed documents。

## 调用顺序

真正 apply 必须使用以下顺序：

1. 用户或 CLI 确认 canonical preview；
2. 获取 project writer exclusion；
3. 在 exclusion 持有期间重新读取 Project/Lock，并重新取得当前 immutable Distribution/candidate
   facts；
4. 调用 `revalidate_package_lock_update_plan()`；
5. 只有成功才准备两份 caller-owned staged documents、写 prepared journal 并开始 replacement；
6. replacement 后按 journal phase 完成、rollback 或在下次启动 recovery。

在获取 writer exclusion 之前可以做一次快速预检，但它不能替代第 3–4 步，否则检查与写入之间仍存在
TOCTOU window。

## 明确非目标

本 Slice 不提供：

- filesystem read、writer lock、interprocess semaphore 或 lock-file lifecycle；
- resolver rerun、candidate discovery、acquisition 或隐式 plan refresh；
- staged write、file/directory flush、replace、backup 或 cleanup；
- journal schema、rollback、startup recovery 或双文档 crash atomicity；
- 数字签名、MAC、security authorization 或对 hostile caller 的 authenticity 证明；
- C#、Avalonia 或 native production orchestration。

`planIntegrity` 是 deterministic integrity/freshness contract，不是秘密签名。Production owner 仍需控制
plan 来源、writer lock 与 project directory 权限。

## 验证

Focused tests 覆盖：

- success 返回原 `planIntegrity`；
- revalidation 期间 resolver 和 `open()` 均不得被调用；
- current Project、Lock、Distribution 与 candidate multiset 分别漂移时 fail closed；
- forged policy version 与 selected-candidate backing snapshot 被拒绝；
- complete candidate input permutation 不改变结果；
- 既有 planning、preview、immutability 与 path-redaction tests 不回归。

Core writer-lock primitive 已在 [Exclusive File Lock v1](adr-core-exclusive-file-lock-v1.md) 增加
collision、RAII release 与 persistent-sentinel tests。后继 Project apply/journal Slice 仍需增加跨进程
collision、crash phase、partial staged、replacement failure 与启动 recovery tests，并证明
revalidation 确实位于 project lock lifetime 内。
