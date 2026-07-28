# ADR：Static Contribution Payload Accessors v1

## 状态

Implemented for #295。本文记录 #295 当时的 registration-time accessor boundary；#296 已实现下述后继 publication boundary。

本文是 [Static Typed Contribution Contract Bindings v1](adr-static-typed-contribution-contract-bindings-v1.md) 的后继。
#294 已让当前进程 callback table 可信地保存 selected contribution 的 exact ID、logical kind、cardinality 与
process-local C++ type key；#295 补齐 registration-time runtime binding，但本身不调用 accessor、不发布 payload，也不实现 registry 或
`ActivationLease`。

[ProcessScope Lifecycle v1](adr-process-scope-lifecycle-v1.md) 的五个 lifecycle callbacks 与
`create-activate-quiesce-deactivate-destroy-v1` model 保持不变。后继
[ProcessScope Contribution Registry and Activation Lease v1](adr-process-scope-contribution-registry-and-activation-lease-v1.md) 已由 #296
在 admitted `ProcessScope` 中于 `activate` 成功后调用 selected accessors，完成 per-factory staging/atomic lease commit，并将
ProcessScope public surface 硬切到 V2。

## 问题

当前 `StaticContributionBindingV1` 只能证明：

- 一个 exact contribution ID 对应哪个 public C++ contract type；
- 该 contract 的 stable kind 与 `single | multiple` cardinality；
- callback table 中的 private type evidence 与 canonical RegistrationSnapshot v2 对齐。

这些事实仍不能取得 payload。Host 不能把 opaque `FactoryInstanceViewV1` 直接 cast 为某个 contract：实际 instance 可以通过
组合、多继承或 package-private adapter 实现 public contract，只有 provider implementation 知道正确的 pointer adjustment。
如果下一步把 publication 直接塞进 `FactoryActivateContextV1`，provider 还需要在运行时重复选择 ID、处理 duplicate/missing
publication，并把一个已冻结的 v1 context 静默改成可变 publisher。

本 ADR 只回答：

> 如何在 registration 时把 Blueprint-selected contribution 与一个 compile-time typed、exact-build payload accessor 绑定起来，
> 使 future Host 能在 `activate` 成功后取得 non-owning contract pointer，同时保持 registration、verification 与 restricted Host
> 全部零 accessor invocation？

## 资料与已有约束

| 资料 | 已确认行为 | 对 #295 的约束 |
| --- | --- | --- |
| [C++ inline declaration](https://eel.is/c++draft/dcl.inline) | 具有 external/module linkage 的 inline entity 在程序内是同一 entity并具有同一地址 | #294 的 writable inline type key 继续作为当前程序内的类型相等证据；payload accessor 不能替代它 |
| [MSVC `/OPT:ICF`](https://learn.microsoft.com/en-us/cpp/build/reference/opt-optimizations?view=msvc-170) | retail link 可折叠相同函数或只读 COMDAT | accessor/function address 不得参与 type identity、stable key、hash 或 diagnostics；type key 继续使用独立 writable inline storage |
| [Static Typed Contribution Contract Bindings v1](adr-static-typed-contribution-contract-bindings-v1.md) | selected ID/kind/cardinality/type key 已与 canonical table indices 对齐 | accessor 必须加入同一 selected binding authority 与同一 immutable table storage，不能 detached registration |
| [Static Factory Callback Table v1](adr-static-factory-callback-table-v1.md) | table 拥有五个 direct lifecycle callbacks，并且比由 callbacks 创建的 token 活得更久 | accessor 只能借出 instance-owned payload，不能转移 ownership 或增加独立销毁路径 |
| [ProcessScope Lifecycle v1](adr-process-scope-lifecycle-v1.md) | `create -> activate`、failure rollback、reverse stop 与 explicit token destroy 已冻结 | accessor 不是第六个 lifecycle callback；#295 不改变 callback signature、执行顺序或 ProcessScope state machine |

这些资料只支持当前 exact executable/program 内的静态绑定。它们不建立跨 DLL、跨 Engine generation、跨工具链或跨进程 ABI。

## 决策

### 1. Contract type 同时是 future payload 的 public interface type

`StaticContributionContractV1` 继续唯一声明 stable kind 与 cardinality。future lookup 返回的 payload 类型就是该 `Contract`，不是
另一个由字符串、RTTI name 或 compiler hash 选择的类型：

```cpp
class ExampleServiceContractV1 {
public:
    static constexpr std::string_view kind{"com.asharia.example-service"};
    static constexpr asharia::host_runtime::StaticContributionCardinalityV1 cardinality{
        asharia::host_runtime::StaticContributionCardinalityV1::Single};

    virtual void update() noexcept = 0;

protected:
    ~ExampleServiceContractV1() = default;
};
```

contract 可以是抽象 interface，也可以是一个直接 payload type。registry 永远不通过 contract pointer 销毁对象；payload lifetime
仍由 factory 的 owning `FactoryInstanceTokenV1` 管理。只有 metadata、没有任何可用 public behavior 的 marker type 虽能表达测试
证据，却不能构成有意义的生产 lookup contract。

### 2. `StaticContributionBindingV2` 绑定 exact typed accessor

public helper 硬切为以下概念形状：

```cpp
template <
    StaticContributionContractV1 Contract,
    Contract* (*Accessor)(FactoryInstanceViewV1) noexcept>
[[nodiscard]] constexpr StaticContributionBindingV2
bindStaticContributionV2(std::string_view contributionId) noexcept;
```

provider implementation 定义 accessor，并把函数作为 non-type template parameter 绑定：

```cpp
ExampleServiceContractV1*
exampleServicePayload(FactoryInstanceViewV1 instance) noexcept {
    auto* implementation = static_cast<ExampleSystem*>(
        FactoryInstanceTokenProviderAccessV1::pointer(instance));
    return implementation;
}

constexpr auto kExampleService =
    bindStaticContributionV2<ExampleServiceContractV1, &exampleServicePayload>(
        "com.asharia.example-service.default");
```

规则如下：

- accessor signature 必须精确为 `Contract* (FactoryInstanceViewV1) noexcept`；wrong return type、wrong contract、可抛签名或
  null accessor 在 compile-time 拒绝；
- NTTP 使 helper 能为 exact typed accessor 生成 Host-private erased thunk；不得用 `reinterpret_cast` 把不同返回类型的 function
  pointer 强行变成统一签名；
- typed accessor 在 provider TU 内完成 implementation-to-contract pointer adjustment；Host 不 cast opaque instance payload；
- binding 继续是 trivially copyable、不可 default construct 的 by-value descriptor；
- helper 从同一个 `Contract` 同时取得 kind、cardinality、type key 与 accessor thunk，provider 不能分别拼装这些字段；
- available 但未被 Blueprint 选择的 binding 保持 inert，其 accessor 不进入 selected runtime evidence，也不获得调用权限。

### 3. Accessor 是 pure borrow，不转移 ownership

accessor 只把一个 valid、已经成功 `activate` 的 `FactoryInstanceViewV1` 投影为 non-owning `Contract*`。author 必须保证：

- non-null 返回值指向当前 instance 拥有或保证存活的 contract object/subobject；
- pointer 从 future publication commit 到对应 lease revoke 始终有效；
- accessor 不分配、不执行 IO、不创建线程、不注册全局状态，也不产生需要单独补偿的 side effect；
- accessor 不保存传入的 instance view，也不返回临时对象、栈对象或独立 ownership；
- accessor 对同一次 instance activation 是稳定投影；future Host 只调用一次并缓存借用 pointer，不在每次 lookup 时重新调用；
- accessor 自身为 `noexcept`；抛异常导致 termination，属于 provider programming error，不伪装成 recoverable Host diagnostic。

Host 可以机械验证 signature、非空 function pointer、selected ownership、canonical alignment 与 zero invocation；Host 无法从任意 native
code 外部证明“无副作用”或 pointer lifetime。这两项仍是 provider author obligation，并由后续 lifecycle/lease tests 验证可观察结果。

### 4. Erased thunk 只存在于 Host Runtime PRIVATE storage

templated helper 生成统一的 private thunk，概念行为为：

```cpp
void* erasedPayloadAccessor(FactoryInstanceViewV1 instance) noexcept {
    return static_cast<void*>(Accessor(instance));
}
```

擦除只发生在 typed accessor 已返回正确的 `Contract*` 之后，因此会保留多继承等情况下已经完成的 pointer adjustment。Host future
lookup 必须先以同一 private type key 对证 contract，再把 stored object pointer 恢复为 `Contract*`；不能只凭 kind 或 ID cast。

type key 与 accessor 具有互不替代的职责：

- type key：只比较“当前 Host program 内是否为同一个 C++ contract type”；
- accessor：只在 future activation commit 中从 exact instance 借出 payload；
- accessor address 即使被 linker folding、重排或跨 build 改变也不影响类型正确性；
- 两者都不序列化、不打印、不 hash，不进入 generation ID、snapshot、receipt 或 diagnostics。

### 5. Callback table canonical 保存 selected runtime binding evidence

registration state 为每个 selected contribution 同时记录 type key 与 erased accessor。`finish()` 继续按 canonical factory/contribution
identity materialize immutable table storage；private runtime binding 与 RegistrationSnapshot v2 使用
`registrationIndex + contributionIndex` 一一对齐。

完整 private record 概念上包含：

```text
registration index
contribution index
process-local type key
erased payload accessor
```

stable snapshot 仍只包含 exact owner、contribution ID、kind 与 cardinality。provider/binding observation order 不影响 snapshot bytes 或
canonical runtime binding indices。table move 继续转移同一个 immutable storage anchor，不能复制、重建或借用 provider span。

registration 必须 fail closed 于 selected accessor 缺失/null、runtime binding count/index mismatch 或 post-materialization allocation
failure；错误保存 stable exact provider/factory/contribution attribution，不保存 function/type-key address。

### 6. #295 production 与 registration-only 路径保持 zero invocation

#295 的 production contract 只建立 future invocation authority，不执行 accessor。PRIVATE accessor projection unit probes 只验证 erased thunk 的类型安全转换与 canonical payload 对齐；以下产品与 registration-only 路径必须只复制/验证 accessor function pointer：

- provider registration 与 recorder `finish()`；
- callback table snapshot projection 与 JSON rendering；
- Activation Eligibility 两阶段 admission；
- generated Windows Development Host restricted registration mode；
- Host executable binding receipt collection、cross-verification 与 deep verification；
- table move、destruction 和所有 registration-only focused branches；direct invocation 仅存在于 PRIVATE accessor projection unit probes。

上述路径没有 factory instance，因此调用 accessor 本身就是 Host bug。synthetic provider 必须使用 counter/abort accessor 证明调用次数为零；
原有五个 lifecycle callbacks 同样保持零调用。

### 7. Accessor 不是第六个 lifecycle callback

`StaticFactoryCallbacksV1` 继续只包含 `create`、`activate`、`quiesce`、`deactivate`、`destroy`。payload accessor：

- 不属于 `StaticFactoryCallbacksV1`；
- 不改变 `FactoryActivateContextV1` 或任一 callback signature；
- 不改变 `create-activate-quiesce-deactivate-destroy-v1` lifecycle model；
- 不在 registration、verification、rollback、revoke、deactivate 或 destroy 时调用；
- future 只在 exact factory 的 `activate` 返回 success 后、factory 对 dependents 或 public registry 可见前调用一次。

#296 已实现该后继规则：selected accessor 返回 null 时，Host 将其视为 activation publication failure；该 factory 不成为
dependency-visible，也不留下 committed contribution。因为其 `activate` callback 已经返回 success，rollback 仍对该 factory 执行
quiesce/deactivate/destroy cleanup。该执行状态属于 ProcessScope V2，不回写为 #295 的 registration contract。

### 8. 版本硬切

外层 provider function 的底层 C++ spelling 仍可能是 `void(StaticFactoryRegistrar&) noexcept`，但 registrar 接受的 binding 类型和
runtime semantics 已 breaking change。所有 active readers/generators/validators 只接受：

| 轴 | #295 current value |
| --- | --- |
| package static factory bindings | schema/model v4 |
| derived provider Binding Plan | schema/model v4 |
| static provider API | `asharia-static-factory-provider-v4` / `StaticFactoryProviderV4` |
| static contribution binding | C++ `StaticContributionBindingV2` |
| Static Composition Root | schema v1 / renderer revision 5 |
| registration capacity/context | C++ v2 |
| Registration Snapshot | schema/model/C++ v2 |
| callbacks / callback table / lifecycle model | C++ v1 |
| Windows Development Host Template | schema v1 / renderer revision 2 |
| Host Executable Binding Receipt | schema v1 |

author bindings 与 derived Binding Plan 的 JSON shape 可以保持相同，但 schema/model 仍提升为 v4，因为 `providerApi` discriminator 与
registrar C++ contract 已改变。Composition generated source 必须把 compile-time provider check 改为 `StaticFactoryProviderV4`，因此
generated bytes 改变，renderer 必须提升为 5。

Host Template renderer 2 只有在其 generated `main.cpp`/CMake bytes 完全不变时才能保持；Receipt schema v1 继续只作为 artifact/
snapshot envelope，但 binding assembly、deep verifier 与 Activation Eligibility generation tuple 只接受
Template 2 + Composition 5/provider v4 + Snapshot 2。pre-v4 bindings/Plan、provider v3 与 Composition renderer 4 不保留 reader、alias、
adapter、双写或 fallback。

## 数据流

```text
public Contract + typed payload accessor
    -> StaticContributionBindingV2
    -> provider v4 registration（zero invocation）
    -> canonical callback-table private runtime binding
       |- type key：current-program type equality
       |- erased accessor：future instance payload borrow
       `- RegistrationSnapshot v2：stable ID/kind/cardinality only
```

本数据流只建立 callable evidence。table completion、snapshot 输出或 receipt publication 都不等于 payload 已存在或 registry 已激活。

## 拒绝的方案

### 在 `FactoryActivateContextV1` 中立即增加显式 `publish()`

拒绝用于当前静态 exact-set baseline。它会让 provider 在运行时重新提交 ID，并新增 unknown、duplicate、missing 与 ignored-result
失败面；同时会静默扩大已经冻结的 v1 context。Blueprint 已给出 exact selected set，Host 后续按 canonical selected accessors 自动
stage 更窄、更确定。未来若确有动态 contribution，需要单独冻结 dynamic owner、safe point 与 mutation transaction，不能伪装成静态 binding。

### 接受 `void*` accessor 或 reinterpret-cast function pointer

拒绝。它把类型错误推迟到运行时，且无法由 helper 证明 accessor return type 与 type key 来自同一个 Contract。

### 使用 accessor/function address 作为 type key

拒绝。MSVC `/OPT:ICF` 等 linker 优化允许函数地址折叠；同一 contract 也可以有多个 owner-specific accessor。类型身份继续只由 #294
的 writable inline storage 建立。

### registration 或 restricted Host 预调用 accessor

拒绝。这些路径没有 valid instance，预调用既无法验证 future payload lifetime，也会把 artifact evidence 变成 lifecycle execution。

### 序列化 accessor、type key、RTTI 或 compiler name

拒绝。它们是 exact-program private values，不是 portable package、snapshot 或 ABI evidence。

### #295 同时实现 registry、handle 与 `ActivationLease`

拒绝。#295 只让 table 拥有 future payload projection authority；它没有 concrete ProcessScope registry generation、publication staging、
atomic visibility、revoke order 或 stale-handle state。把这些职责同时加入会混合 provider/schema hard cut 与 runtime lifetime transaction。

## 验证要求

- compile-time positive：exact `Contract* (FactoryInstanceViewV1) noexcept` accessor、abstract interface 与 multiple-inheritance pointer
  adjustment；
- compile-time negative：wrong return type、wrong Contract、非 `noexcept`、null accessor NTTP、旧 `StaticContributionBindingV1`/
  provider v3 surface；
- selected accessor/type key 与 canonical Snapshot v2 indices 一一对齐；multi-provider/multi-factory/multi-contribution observation-order
  扰动后 snapshot bytes 与 runtime index mapping 不变；
- unselected available accessor inert；zero-contribution factory 保留；selected accessor 缺失/null或 count/index mismatch fail closed；
- table move 后 immutable storage anchor、type key 与 accessor evidence 仍由同一 table instance ownership 持有；
- registration、eligibility、restricted generated Host、receipt/deep verification 的 accessor invocation count 与五个 lifecycle callback
  count 全部为零；
- Snapshot v2 JSON、receipt、diagnostics、generation identity 与 logs 不出现 accessor/type-key address、RTTI 或 type name；
- only-current tuple：Template 2 + Composition 5/provider v4 + Snapshot 2 成功，Composition 4/provider v3 及更早组合拒绝；
- Host Template renderer 2 generated bytes golden 保持不变；Composition renderer 5 golden、双向 Blueprint/Binding Plan exact-set validation、
  generated provider compile-time check 与 content identity 更新；
- Python contracts、C++ registration/eligibility regressions、generated composition 与 exact Host 双编译器 integration、MSVC Release/ICF、
  encoding、doc sync、topology、`git diff --check`、ClangCL 与 MSVC full gates。

## 不做事项

以下是 #295 Slice 的非目标；其中 publication、lookup、weak handle、stale detection、revoke 与 contribution-only lease 已由 #296 的
后继 ADR 实现，但不因此改变本 ADR 的 registration-time 职责。

- 在 production lifecycle 中调用 accessor 或取得真实 payload；
- contribution publication staging、atomic commit、lookup、registry snapshot 或 raw pointer/reference API；
- `single` scope-generation conflict、priority、replacement、shadowing 或 runtime winner；
- public generation handle、stale-handle detection、revoke 或 contribution-only `ActivationLease`；
- 修改 ProcessScope projection、executor state、factory `active` bookkeeping、rollback/stop passes 或 diagnostics；
- jobs、subscriptions、callback gates、safe point、线程安全或并行 activation；
- Project/Editor/World 等其他 scope owners；
- DLL、dynamic discovery、hot reload/unload 或跨 Engine generation ABI；
- production Editor/Runtime/Bootstrap 接线；
- pre-v4 compatibility reader、adapter、alias、双写或 fallback。

## 后继边界（#296 已实现）

#296 已在 admitted ProcessScope 中消费 callback-table private runtime bindings：

1. preflight 为 process-selected contributions 建立 fixed registry slots 与 exact scope-generation anchor；
2. 每个 factory 的 `activate` 成功后，按 canonical contribution order 调用全部 selected accessors；
3. non-null payload 先进入不可见 staging，exact set 全部成功后一次 commit 为 Host-owned contribution-only lease；
4. 整个 ProcessScope start 成功前不开放 public lookup；
5. rollback/stop 采用 reverse quiesce → `Revoking` → reverse lease revoke → reverse deactivate/destroy → `Revoked`；weak generation
   view/handle 在 revocation、epoch 变化或 registry owner 销毁后 fail closed；
6. accessor-null/commit failure 发生在 `activate` success 之后时，failed factory 仍完成 quiesce/deactivate/destroy，但不成为
   dependency-visible；
7. factory context 不暴露 arbitrary `getService<T>()`，也没有 process-global registry。

下一步必须用真实 Host 与真实 system/contribution 闭合可观察的 vertical feature。其他 concrete scopes、production Bootstrap/Session adapter、
jobs/subscriptions lease 与动态 contribution mutation 只有在该功能边界确实需要时再分别设计，不继续预先扩展抽象 Foundation。
