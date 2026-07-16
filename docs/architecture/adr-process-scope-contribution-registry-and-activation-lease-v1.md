# ADR：ProcessScope Typed Contribution Registry 与 ActivationLease v1

## 状态

Implemented for #296。

本文是 [ProcessScope Lifecycle v1](adr-process-scope-lifecycle-v1.md)、
[Static Typed Contribution Contract Bindings v1](adr-static-typed-contribution-contract-bindings-v1.md) 与
[Static Contribution Payload Accessors v1](adr-static-contribution-payload-accessors-v1.md) 的直接后继。它把 ProcessScope public
executor/result/error surface 硬切到 V2，并冻结第一个可用的 typed contribution publication、lookup 与 revoke 边界。

本 ADR 不表示 normal Host、Bootstrap 或真实领域系统已经接线。#296 只完成 admitted、headless ProcessScope 内的生命周期闭环。

## 问题

#293 已能按 Blueprint dependency order 创建和激活 factory，并在失败或停止时反向清理；#294/#295 已让 private callback table 保存
selected contribution 的 exact owner、ID、kind、cardinality、process-local type key 与 typed payload accessor。当前仍缺少：

- accessor 应在何时调用，null 或同一 factory 的部分失败如何回滚；
- 已调用 `activate` 与可以被 dependent 使用是否是同一状态；
- 多个 payload 何时同时对调用方可见；
- payload 不延长 instance lifetime 时，旧 lookup 如何避免悬空访问；
- stop/rollback 应在 deactivate 与 destroy 之前何时关闭 lookup；
- `single` 与 `multiple` 如何在一个 concrete ProcessScope generation 中兑现。

把 pointer 放入 process-global `string -> void*` map 不能回答这些问题，还会绕过 Blueprint dependency、scope ownership 与 C++ type
evidence。

## 外部来源事实

以下只是可核验的外部行为，不代表 Asharia 复制对应 API：

| 来源 | 已确认事实 | 本 ADR 可采用的约束 |
| --- | --- | --- |
| [C++ object lifetime](https://eel.is/c++draft/basic.life) | object lifetime 结束后，以旧 pointer 访问成员或调用非 static member function 受到严格限制并可构成 undefined behavior | revoke 必须先阻止新的成功 borrow，不能把“调用方会记得 pointer 何时失效”作为唯一安全边界 |
| [`weak_ptr::lock`](https://eel.is/c++draft/util.smartptr.weak.obs) | 一次操作原子地得到有效 shared owner 或空结果 | weak handle 应通过一次 `tryBorrow()` 完成 generation 与可见性检查；这只启发 checked acquisition，不表示 payload 使用 shared ownership |
| [C++ Core Guidelines I.2/I.3/I.4、R.1](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines.html#Ri-global) | global mutable state/singleton 会隐藏依赖；接口应强类型；成对 acquire/release 应由 resource handle 管理 | registry 必须由 concrete scope owner 持有，lookup 以 contract type 表达，publication/revoke 由 Host-owned lease 成对管理 |
| [O3DE `AZ::Interface`](https://docs.o3de.org/docs/user-guide/programming/messaging/az-interface/) | 提供 typed `Register/Unregister/Get`；官方把它限定于 module/application 长寿命系统，并明确 thread safety 由调用方负责 | typed register/unregister 是可行模式，但不能直接证明短生命周期 scope、stale generation 或并发安全 |
| [Unreal `FComponentRequestHandle`](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/ModularGameplay/FComponentRequestHandle) | handle 保存 weak manager，handle 销毁会移除其 request，manager 消失后 `IsValid()` 为 false | release authority 可以绑定 owner-scoped handle，而不要求 handle 延长 owner lifetime |
| [Unreal `FWeakObjectPtr`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/CoreUObject/FWeakObjectPtr) | weak pointer 不阻止对象回收，使用 index/serial 区分 identity，并可报告曾经有效但现在 stale | 后继 generation 不能让旧 handle 意外命中新对象；handle identity 必须包含 exact scope-generation anchor |

外部资料只支持 typed access、owner-scoped release 与 stale fail-closed 方向。它们没有规定 Asharia 的 per-factory staging、whole-scope
gate 或 cleanup passes。

## Asharia 推论

以下规则由仓库已有合同推导，是本 ADR 的设计决策，不是外部引擎事实：

1. #293 的 `start()` 是 all-or-nothing，因此 public registry 只能在整个 ProcessScope start 成功后一次打开。
2. #295 要求 accessor 只在 owner `activate` 成功后调用；为避免 dependent 看到半发布 owner，factory 必须区分
   `lifecycleActivated` 与 `dependencyVisible`。
3. 一个 factory 的 selected exact set 已由 Blueprint/table 冻结，因此先写不可见 staging，再用一个 Host-owned lease 无分配 commit，
   比让 provider 动态调用 `publish(id, pointer)` 更窄。
4. #293 已冻结完整 reverse quiesce/deactivate/destroy passes；为了在对象停止前阻止新借用，V2 在 quiesce 与 deactivate 之间加入
   `Revoking -> reverse lease revoke`。
5. 当前 executor 是单 control thread、single process epoch；V1 不引入 mutex 或跨线程 pin。borrow 检查与后续同步使用之间不得穿插
   lifecycle operation。

## 决策摘要

```text
preflight
  -> fixed registry slots + per-factory staging（zero callback/accessor）
start, for each factory in Blueprint order
  -> create
  -> activate                         [lifecycleActivated = true]
  -> call selected accessors once
  -> atomic per-factory lease commit
  -> dependencyVisible = true
all factories complete
  -> registry phase Active            [public lookup starts]

rollback / stop
  -> reverse quiesce lifecycleActivated factories
  -> registry gate Revoking           [all new borrows fail]
  -> reverse revoke committed leases
  -> reverse deactivate lifecycleActivated factories
  -> reverse destroy created instances
  -> gate Revoked
```

Registry、slots、generation anchor、leases 与 payload pointer 全部由 `ProcessScopeExecutorV2` 的 stable pimpl 私有拥有。public view 与
handle 只弱引用 exact generation，不拥有 payload，也不延长 factory instance lifetime。

## 1. ProcessScope public V2 硬切

`process_scope.hpp` 中的 public ProcessScope owner、state、stage、error、diagnostic、result 与 preparation entry point 全部提升到 V2：

```text
ProcessScopeExecutorV2
ProcessScopeStateV2
ProcessScopeLifecycleStageV2
ProcessScopeContributionPublicationStageV2
ProcessScopeErrorCodeV2
ExactFactoryReferenceV2
ProcessScopePreparationErrorV2
ProcessScopeOperationErrorV2
ProcessScopeLifecycleDiagnosticV2
ProcessScopeStartFailureV2
ProcessScopeContributionPublicationDiagnosticV2
ProcessScopeStopReportV2
ProcessScopeStartResultV2
ProcessScopeStopResultV2
ProcessScopePreparationResultV2
prepareProcessScopeExecutorV2(...) -> ProcessScopePreparationResultV2
```

原 ProcessScope V1 symbols、aliases、adapters、dual entry points 与 source-compatibility overloads 删除。`Factory*ContextV1`、五个 callback
signatures、provider v4、Binding Plan v4、Composition renderer 5、RegistrationSnapshot v2、Host Template renderer 2、schema 与
generated bytes 不变。

V2 public state 仍为 `Prepared | Active | StartFailed | Stopped | MovedFrom`；`Starting`、`Revoking` 是同步 operation 内部状态，不增加可由
调用方驱动的半完成 public state。

## 2. Preflight 建立 fixed registry generation

`prepareProcessScopeExecutorV2(...)` 在首个 lifecycle callback 和 accessor 之前完成所有 owning allocation，并从 admitted execution view
取得 callbacks、canonical snapshot、runtime bindings、control-thread epoch、process epoch 与 sealed process projection。PRIVATE access
不得把 span、type key、accessor 或 epoch 暴露给 public API。

preflight 只为 process projection 中的 factories 建立 slots；table 中其他 scope 或 unselected contributions 保持 inert。每个 fixed slot
至少私有保存：

```text
exact owner factory index
canonical slot position（由 Blueprint factory order 与 owner contribution order 派生）
stable contribution ID + kind + cardinality attribution
process-local type key + erased accessor
payload pointer = null
slot state = Empty | Staged | Committed | Revoked
```

同时预分配：

- 每个 factory 的 canonical slot-index range；fixed slots 自身承载 invisible staging；
- exact-scope generation anchor、diagnostic scratch 与 lease storage；lookup 只扫描 fixed slots，不分配辅助结果。

`multiple` ordinal 按 `(Blueprint factory order, owner contribution canonical order)` 冻结；每个 factory 调 accessor 时也按自身 canonical
contribution ID 顺序。lookup、staging、commit 与 revoke 不再增长容器或复制 text。

preflight 必须复验 runtime binding count、`registrationIndex + contributionIndex`、snapshot owner/ID/kind/cardinality、non-null type key 与
accessor 的一一对齐。任何 mismatch、allocation failure 或 ProcessScope 内 same-contract `single` 数量大于一都返回稳定 preparation error，
且五个 lifecycle callbacks 与全部 accessors 调用次数为零。table 内 process projection 外的同 kind 记录不参与 `single` 计数。

## 3. Activated 与 dependency-visible 分离

每个 factory record 使用两个独立事实：

| 状态 | 何时置位 | 用途 |
| --- | --- | --- |
| `lifecycleActivated` | `activate` 返回 success 后立即置位 | 决定该 factory 是否参加 reverse quiesce/deactivate |
| `dependencyVisible` | 该 factory 全部 accessors 成功并完成 lease commit 后置位 | 决定后继 factory 能否作为 declared dependency 创建 |

dependent create 仍只能得到 Blueprint 声明的 `FactoryDependencyViewV1`，且每个 dependency 必须已经 `dependencyVisible`。factory context
不会获得 registry、handle 或 arbitrary `getService<T>()`。

zero-contribution factory 在 activate success 后直接成为 dependency-visible；它的 fixed range 在 preflight 已标记为完成，不调用
accessor，也不创建没有撤销内容的 empty lease。

## 4. Per-factory staging 与 atomic lease commit

对一个成功 activate 的 factory，Host 按 canonical order 对每个 selected slot 调 accessor 恰好一次。accessor 输入为该 factory 的 valid
instance view，输出只能是 non-owning typed payload projection。

- non-null pointer 先写入该 factory 的 staging range，registry slot 仍不可见；
- 任一 accessor 返回 null，清空本 factory staging，不创建 partial lease，不设置 dependency-visible；
- null failure 的 primary diagnostic 精确包含 Engine generation、owner factory、contribution ID/kind 与 publication stage；
- 因为 owner 已 lifecycle-activated，rollback 仍对它执行 quiesce、deactivate 和 destroy；dependent 不会开始；
- 已提交的更早 factories 由统一 rollback reverse revoke，不由失败 factory 局部清理。

Host 在每次 accessor 前验证 exact generation、gate 与目标 slot 仍为空；accessor pointer 写入 fixed slot 的 invisible staging。
全部 accessor 成功后，再以不可抛、无分配的 commit 一次标记整段 slots、创建一个 PRIVATE
`ProcessContributionActivationLeaseV1`，最后设置 factory dependency-visible。任一 accessor 或 slot 检查失败都会清空本 factory 的
整段 staging；通过最终 commit 后没有 recoverable failure point。

该 lease：

- 每个有 contribution 且完成 commit 的 factory 恰好一个；lease move-only，由 executor factory record 拥有；
- 只拥有“这些 slots 当前可借用”的 publication authority，不拥有 payload 或 instance；
- explicit `revoke()` 清空并标记整组 slots，不调用 provider code、不分配且 `noexcept`；
- active lease 被遗漏到析构属于 Host programming error；析构不猜测 callback order；
- 不管理 jobs、subscriptions、callback gates、token、scope 或任意 provider 自定义 cleanup。

因此本 ADR 的 “ActivationLease” 是 contribution-only implementation owner，不是 public 通用 RAII/service framework。

## 5. Whole-scope registry gate

registry generation gate 只有：

```text
Staging -> Active -> Revoking -> Revoked
Staging ----------> Revoking -> Revoked   (startup rollback)
```

所有 per-factory leases 可在 `Staging` 下 commit，供 Host 判断 dependency visibility，但 public lookup 仍失败。只有全部 factories 完成后，
`start()` 才将 gate 一次改为 `Active`，随后将 executor public state 改为 `Active`。

normal stop 和 startup rollback 都先完成 reverse quiesce，再将 gate 改为 `Revoking`。从这一刻起，已有 view/handle 的新
`tryBorrow()` 必须 fail closed；之后才 reverse revoke leases。不能先 deactivate/destroy 再关闭 lookup。

## 6. Typed registry view、weak handle 与 borrow

`ProcessScopeExecutorV2` 只在 executor state 与 registry phase 均为 `Active` 时返回一个 copyable、non-owning
`ProcessContributionRegistryViewV1`。public shape 为：

```cpp
std::expected<ProcessContributionRegistryViewV1, ProcessContributionLookupErrorV1>
contributions() const noexcept;
template <ProcessContributionContractV1 Contract>
std::expected<std::size_t, ProcessContributionLookupErrorV1> size() const noexcept;

template <ProcessContributionContractV1 Contract>
std::expected<ProcessContributionHandleV1<Contract>,
              ProcessContributionLookupErrorV1>
single() const noexcept;

template <ProcessContributionContractV1 Contract>
std::expected<ProcessContributionHandleV1<Contract>,
              ProcessContributionLookupErrorV1>
at(std::size_t ordinal) const noexcept;
```

语义固定为：

- `size<Contract>()` 对 single/multiple 均返回该 exact contract 的数量；kind absent、type/cardinality mismatch 返回 error，不以另一个
  同 kind type 或 `0` 掩盖错误；
- `single<Contract>()` 只接受 `Contract::cardinality == Single`，并返回唯一 slot 的 weak handle；
- `at<Contract>(ordinal)` 只接受 `Multiple`，ordinal 为 preflight canonical lookup order，out-of-range 返回 error；
- 三者都先以 kind 定位 group，再比较 #294 的 private type key 与 cardinality；不使用 RTTI、compiler name、hash 或 cast-by-string；
- lookup 不分配，不返回 erased pointer，也不允许 replacement、priority、first-wins 或 shadowing。

`ProcessContributionHandleV1<Contract>` 保存 exact registry generation 的 weak anchor 与 slot identity。它不保存 owning instance，
不使 executor/registry/payload 延寿。`tryBorrow()` 返回 typed、non-null
`std::reference_wrapper<Contract>` 或 `ProcessContributionLookupErrorV1`；public API 不公开 `void*`、type key、accessor 或 slot storage。

每次 `tryBorrow()` 必须按固定优先级检查：generation anchor 仍存在、caller 是原 control thread、process epoch 仍 current、gate 为
`Active`、slot 为 committed、kind/cardinality/type key 仍匹配。executor move 保留同一 pimpl/anchor，handle 继续有效；stop/revoke、executor
销毁或后来的另一个 ProcessScope generation 使用不同 anchor，旧 handle 必须失败，绝不能命中新 payload。

borrow 不提供 shared ownership 或 cross-thread pin。成功 borrow 只允许在当前 control thread、下一次 ProcessScope lifecycle operation 之前
同步使用；调用方不得缓存其 pointer/reference 跨越 stop。当前单线程合同保证一次 `tryBorrow()` 与紧随其后的同步调用之间不会并发 revoke。

## 7. Cleanup 与 rollback 顺序

normal stop 与所有 create/activate/publication failure 使用同一完整顺序：

1. reverse quiesce 所有 `lifecycleActivated` factories；
2. gate 进入 `Revoking`；
3. 按 reverse Blueprint factory order revoke 每个 committed lease，并清空其 fixed slot range；
4. reverse deactivate 所有 `lifecycleActivated` factories；
5. reverse destroy 所有成功 create 的 tokens；
6. gate 进入 `Revoked`，executor 进入 `Stopped` 或 `StartFailed`。

quiesce/deactivate provider failure 继续追加 diagnostics，不截断 revoke 或后继 passes。revoke 是 Host-owned、noexcept、无 provider callback
操作；发现 active lease/slot generation 不一致是 Host programming error，不能作为“跳过 revoke 后继续 destroy”的普通 package error。

## 8. Thread、epoch 与 stale 规则

- prepare、start、registry view 获取、`size/single/at/tryBorrow`、stop 与 move 后 operation 都限定在 admitted control thread；
- wrong-thread lookup/borrow 返回 error，不改变 gate/slot，回到原 thread 可重试；
- process epoch stale 后 lookup/borrow 永久 fail closed；Active epoch 仍必须先在原 control thread stop，强行 invalidation 是 Host owner
  protocol violation；
- exact scope-generation anchor 是 process-local、non-serialized identity；它不以 address、counter bytes 或 hash 出现在 diagnostics；
- V1 不加 mutex、atomic payload、worker dispatch、parallel activation 或 cross-thread handle use。

## 9. Diagnostics 与 privacy

V2 preparation errors 至少区分 runtime-binding count/index/alignment mismatch、missing type key/accessor、ProcessScope `single` conflict 与
allocation failure。V2 start failure新增 publication stage 与 accessor-null code，并携带 exact factory 和 contribution attribution。

lookup/borrow 使用独立、稳定的 `ProcessContributionLookupErrorCodeV1`，至少区分：

```text
RegistryExpired
WrongControlThread
ProcessEpochStale
RegistryNotActive
RegistryRevoking
RegistryRevoked
ContractAbsent
ContractTypeMismatch
ContractCardinalityMismatch
OrdinalOutOfRange
PayloadUnavailable
```

diagnostics、logs、snapshot、receipt、hash 与 JSON 不得包含 payload/type-key/accessor address、RTTI、compiler spelling、pimpl/control-block
address、private slot ordinal 或 borrowed text。slot ordinal 只在 PRIVATE handle state 内用于定位。

## 10. 拒绝的方案

### Process-global service locator 或 singleton registry

拒绝。它隐藏依赖和 scope generation，允许未声明依赖绕过 Blueprint；registry view 必须从具体 Active executor 取得。

### Provider 在 activate context 动态 `publish(id, void*)`

拒绝。selected exact set 与 typed accessor 已经冻结；再次提交 ID 会重新引入 missing/extra/ignored-result 与 cast-by-string 失败面。

### 每个 accessor 成功后立即公开 slot

拒绝。同一 factory 和整个 ProcessScope 都会出现 partial visibility，破坏 #293 的 all-or-nothing start。

### Handle 延长 payload lifetime

拒绝。payload 由 factory token 独占；shared ownership 会让 reverse deactivate/destroy 无法证明资源已经清零。

### 先 deactivate/destroy，再让 handle stale

拒绝。C++ object lifetime 不允许把旧 pointer 的安全性留到对象已经销毁之后补救。gate 必须先进入 `Revoking` 并撤销 lease。

## 11. 不做事项

- Project、Editor、ToolJob、GameSession、World、LocalUser、Document 或 Preview scope；
- production Bootstrap/Host adapter、Editor/Package Manager UI 或真实领域 system integration；
- factory-context registry lookup、全局 locator、静态 singleton 或跨 scope lookup；
- dynamic contribution mutation、priority、replacement、shadowing、hot reload/unload 或 DLL ABI；
- jobs、subscriptions、callback gates、arbitrary cleanup 或 general-purpose lease；
- mutex、parallel activation、worker dispatch、cross-thread borrow 或 long-lived pin；
- pointer/type key/accessor/RTTI serialization；
- provider/schema/renderer/snapshot/receipt revision；
- ProcessScope V1 compatibility surface。

## 12. 验证要求

- compile-time：仅 V2 ProcessScope public surface；typed view/handle valid/invalid contract shapes；无 public erased lookup；
- preflight：exact binding count/index/type/accessor alignment、全部 allocation before callbacks、same-generation `single` conflict zero-call；
- activation：`create -> activate -> accessor -> lease -> dependency visible` trace，multiple accessors canonical once-only；
- atomicity：per-factory accessor-null 无 partial lease、whole-scope start 前 lookup 全失败、full success 后一次 Active transition；
- cleanup：create/activate/accessor failure 与 normal stop 都严格执行
  `reverse quiesce -> Revoking -> reverse revoke -> reverse deactivate -> reverse destroy`；
- lookup：single/multiple、absent、wrong type/cardinality、out-of-range 与 allocation-free ordinal access；
- stale：executor move positive；wrong thread、process epoch stale、stop、executor destruction、later generation negatives；
- regressions：unselected/non-process accessor zero-call、zero-contribution factory、registration/eligibility/restricted generated Host lifecycle-free；
- privacy：snapshot/receipt/diagnostics/logs 不泄漏 private addresses、RTTI 或 compiler spelling；
- repository gates：focused C++ TUs、full Python/contracts/topology、encoding、doc sync、`git diff --check`、Vulkan review、Conan-before-CMake
  ClangCL/MSVC debug build+CTest；identity 假设在 MSVC Release/ICF 下复验。

## 后续边界

#296 完成后停止继续扩展抽象 Foundation。下一独立 Slice 应接入第一个 normal Host、激活一个真实 system package，并通过本 registry
驱动可观察功能。其他 scope owners、Bootstrap 状态映射、dynamic contribution 与通用 job/subscription cleanup 只有在该竖向功能证明需要时
再分别设计。
