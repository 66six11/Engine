# ADR：Static Factory Callback Table v1

## 状态

Accepted and implemented for #291。本文冻结 exact-build 静态 factory callback 的类型、所有权、注册、失败与可见性边界，作为
[Static Factory Registration v1](adr-static-factory-registration-v1.md) 与后续 Activation Eligibility / ProcessScope
Lifecycle 之间的窄桥梁。

版本轴彼此独立：callback contract/table 保持 v1；active provider API、author bindings 与 derived Binding Plan 为 v2；Static
Composition Root 保持 schema v1、renderer 3；Windows Development Host Template 保持 schema v1、renderer 2；RegistrationSnapshot
与 Host Executable Binding Receipt 仍为 v1。当前处于早期硬切阶段，不提供旧 provider/schema/renderer 的 reader、adapter 或 deep
verification compatibility。

本 Slice 只建立当前进程内的 frozen callback table。它不调用任何 lifecycle callback，不创建 instance，不建立 scope、lease
或 contribution registry，也不把 table completion 解释为 activation、`Ready` 或 current-process generation 证明。

## 问题

#289 已让 generated static composition root 显式调用 exact providers，并产生 owning、canonical、identity-only
`StaticFactoryRegistrationSnapshotV1`；#288 又把 snapshot 与 final Host executable bytes 绑定成不可变 receipt。现有证据仍故意不包含
factory 实现：

- registration snapshot 只能回答“哪些 exact factory identity 被观察到”；
- receipt 只能回答“哪些 exact executable bytes 输出了该 identity evidence”；
- Host Runtime 尚无类型安全方式把一个 exact factory identity 绑定到 create/activate/shutdown 实现。

如果下一步直接暴露 raw symbol lookup、抽象基类、`std::function` 或 process-global registry，就会把静态 exact-build 模型误做成
跨 generation 插件 ABI；如果 table 一完成就允许任意 consumer 调用 callback，则又会绕过尚未实现的 artifact/current-process
eligibility gate。

本 ADR 只回答：

> 同一次受控 provider registration 中，如何让每个 expected exact factory identity 恰好绑定一份完整 typed callback descriptor，
> 同时保持 identity evidence、artifact evidence 与 runtime lifecycle 三个边界相互独立？

## 资料验证

| 资料 | 官方行为 | 对 Asharia 的约束 |
| --- | --- | --- |
| [O3DE Components](https://www.docs.o3de.org/docs/user-guide/programming/components/overview/) | component 按 dependency 激活、按相反顺序 deactivate，生命周期跟随 owning entity | callback table 不能把 provider registration order 解释为执行顺序；Blueprint 与 future scope executor 继续拥有顺序和 owner |
| [Unreal Programming Subsystems](https://dev.epicgames.com/documentation/en-us/unreal-engine/programming-subsystems-in-unreal-engine) | subsystem 是具有 Engine、Editor、GameInstance 或 LocalPlayer 等明确 managed lifetime 的 instance | callback 只提供实现；instance lifetime 必须由明确 scope owner 管理，不能变成无 owner 的全局 singleton |
| [Unreal Engine Modules](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules) | build dependency chain 决定编译闭包；module loading phase 内顺序本身不确定 | static target closure 与 logical activation DAG 保持分离；不从链接或 registration timing 推断 lifecycle order |

这些资料支持“显式依赖顺序 + owner-scoped lifetime + 构建期原生组合”。它们不要求 Asharia 复制 Unreal/O3DE 的对象模型、反射、
动态模块加载或 ABI。

## 所有权

| 能力 | owner | 不拥有 |
| --- | --- | --- |
| callback 类型与 provider-facing registrar contract | `engine/host-runtime` public exact-build contract | package identity、activation eligibility、scope instance |
| expected provider/factory context | generated static composition root | callback 实现、activation order、artifact freshness |
| 单次 recording state 与 sticky failure | `StaticFactoryRegistrationRecorder` | process-global registry、rollback provider 任意副作用 |
| frozen current-process callback table | `StaticFactoryCallbackTableV1` | receipt、lock、stable ABI、`Ready` |
| canonical identity evidence | #289 `StaticFactoryRegistrationSnapshotV1` projection | callback address、opaque token、execution outcome |
| exact executable bytes evidence | #288 `HostExecutableBindingReceiptV1` | callback invocation、loaded-process state |
| provider invocation permission in long-lived Host | future Pre-Registration Admission | table construction、lifecycle invocation |
| callback access permission | future Activation Admission | package resolution、provider discovery |
| instance/scope/lease lifecycle | future scope executor | build、receipt publication、provider discovery |

## 决策

### 1. 使用五个直接 callback 与强类型 opaque token

provider-facing 轻量 header 概念形状固定为：

```cpp
class FactoryCreateContextV1;
class FactoryActivateContextV1;
class FactoryQuiesceContextV1;
class FactoryDeactivateContextV1;
class FactoryInstanceTokenProviderAccessV1;
class FactoryInstanceViewV1;

class FactoryInstanceTokenV1 final {
public:
    FactoryInstanceTokenV1() noexcept = default;
    ~FactoryInstanceTokenV1() noexcept;
    FactoryInstanceTokenV1(FactoryInstanceTokenV1&& other) noexcept;
    FactoryInstanceTokenV1& operator=(FactoryInstanceTokenV1&&) = delete;
    FactoryInstanceTokenV1(const FactoryInstanceTokenV1&) = delete;
    FactoryInstanceTokenV1& operator=(const FactoryInstanceTokenV1&) = delete;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] FactoryInstanceViewV1 view() const noexcept;

private:
    explicit FactoryInstanceTokenV1(void* opaque) noexcept;
    void* opaque_{};

    friend class FactoryInstanceTokenProviderAccessV1;
};

class FactoryInstanceViewV1 final {
public:
    [[nodiscard]] bool isValid() const noexcept;

private:
    explicit FactoryInstanceViewV1(void* opaque) noexcept;
    void* opaque_{};

    friend class FactoryInstanceTokenV1;
    friend class FactoryInstanceTokenProviderAccessV1;
};

enum class FactoryCallbackStatusV1 : std::uint8_t {
    Failed = 0,
    Succeeded = 1,
};

class FactoryCallbackResultV1 final {
public:
    [[nodiscard]] static FactoryCallbackResultV1 succeeded() noexcept;
    [[nodiscard]] static FactoryCallbackResultV1 failed(std::uint32_t localCode) noexcept;

    [[nodiscard]] bool isSucceeded() const noexcept;
    [[nodiscard]] FactoryCallbackStatusV1 status() const noexcept;
    [[nodiscard]] std::uint32_t localCode() const noexcept;

private:
    FactoryCallbackResultV1(FactoryCallbackStatusV1 status,
                            std::uint32_t localCode) noexcept;
    FactoryCallbackStatusV1 status_;
    std::uint32_t localCode_;
};

class FactoryCreateResultV1 final {
public:
    [[nodiscard]] static FactoryCreateResultV1
    succeeded(FactoryInstanceTokenV1 instance) noexcept;
    [[nodiscard]] static FactoryCreateResultV1 failed(std::uint32_t localCode) noexcept;

    [[nodiscard]] const FactoryCallbackResultV1& result() const noexcept;
    [[nodiscard]] FactoryInstanceViewV1 instanceView() const noexcept;
    [[nodiscard]] FactoryInstanceTokenV1 takeInstance() && noexcept;

private:
    FactoryCreateResultV1(FactoryCallbackResultV1 result,
                          FactoryInstanceTokenV1 instance) noexcept;
    FactoryCallbackResultV1 result_;
    FactoryInstanceTokenV1 instance_;
};

using FactoryCreateCallbackV1 =
    FactoryCreateResultV1 (*)(FactoryCreateContextV1&) noexcept;
using FactoryActivateCallbackV1 = FactoryCallbackResultV1 (*)(
    FactoryActivateContextV1&, FactoryInstanceViewV1) noexcept;
using FactoryQuiesceCallbackV1 = FactoryCallbackResultV1 (*)(
    FactoryQuiesceContextV1&, FactoryInstanceViewV1) noexcept;
using FactoryDeactivateCallbackV1 = FactoryCallbackResultV1 (*)(
    FactoryDeactivateContextV1&, FactoryInstanceViewV1) noexcept;
using FactoryDestroyCallbackV1 = void (*)(FactoryInstanceTokenV1 instance) noexcept;

struct StaticFactoryCallbacksV1 final {
    FactoryCreateCallbackV1 create{};
    FactoryActivateCallbackV1 activate{};
    FactoryQuiesceCallbackV1 quiesce{};
    FactoryDeactivateCallbackV1 deactivate{};
    FactoryDestroyCallbackV1 destroy{};
};
```

provider implementation 的独立 companion header 只提供明确 bridge：

```cpp
class FactoryInstanceTokenProviderAccessV1 final {
public:
    [[nodiscard]] static FactoryInstanceTokenV1 fromPointer(void* instance) noexcept;
    [[nodiscard]] static void* pointer(FactoryInstanceViewV1 instance) noexcept;
    [[nodiscard]] static void* consume(FactoryInstanceTokenV1 instance) noexcept;
};
```

规则：

- descriptor 只包含五个 function pointers，必须 trivially copyable，并由 recorder/table 按值保存；不借用 descriptor、span、
  closure 或外部 `userData`；
- 五个 slot 全部必须非空。没有工作要做的阶段也要提交显式、typed、返回成功的 no-op；
- 五个 pointer 的参数、返回值和 `noexcept` 都是合同的一部分；错误签名必须在 provider 编译或 generated root type check 阶段失败；
- owning token 是 move-only、payload private 的 non-aggregate；普通 Host consumer 只能检查空值或取得不可解包的 non-owning view，
  不能读取、替换、复制或自行释放 payload；
- move construction、`takeInstance()` 与 provider consuming access 必须清空 source，使 moved-from token 无效；view 是
  callback-duration-only borrow，provider 不得保存；
- provider `.cpp` 通过独立 provider-only `FactoryInstanceTokenProviderAccessV1` helper 完成 `T* -> token`、`view -> T*` 与
  consuming `token -> T*`。helper 位于独立 provider-only include surface/INTERFACE target，provider implementation target 只能
  `PRIVATE` 消费；它不进入普通 Host Runtime/public table include path，并由 include-boundary gate 约束；
- valid owning token 若未经 `destroy`/consuming access 就进入析构，是 fatal programming error；实现必须 fail fast，不能静默
  `delete`、泄漏或猜测 deleter。move-assignment 保持删除，避免覆盖尚未消费的 ownership；
- status 的 zero enum value 必须是 `Failed`。result 不能 default construct，只能通过 `succeeded()` / `failed(code)` factories
  构造。success code 固定为 zero，create success 必须消费非空 owning token，create failure 必须保持空 token；invalid success
  token fail closed 为 failure；
- future executor 若仍观察到 unknown status 或矛盾组合，必须把它归类为 fatal provider contract violation，并先回滚其他已成功
  instances；不得猜测 offending payload 是否可安全 destroy；
- result 只包含 status 与 package-local numeric code。Host 将来附加可信的 factory/stage/generation attribution；详细文本通过
  Host-owned diagnostic sink 提交，不能从 callback 返回 borrowed `string_view`；
- context 类型在 #291 只前置声明以冻结签名。它们由 Host 拥有、不可复制/移动，只在单次 callback 的 dynamic extent 内有效；
  provider 不得保存 pointer/reference。future surface 只暴露 Blueprint 已声明的 dependencies/capabilities 与 Host-owned diagnostic
  sink；
- callback contract v1 不把 Host allocator 交给 provider。instance allocation/deallocation 完全由 provider 拥有；instance 必须
  保留 destroy 所需的 allocator/deallocation state，不能依赖已失效 context。若未来要求 Host allocator lease 或 destroy context，
  必须提升 callback contract revision；
- token/view 只在当前 exact executable/process 内有效。Host 不得解引用、cast、复制底层对象或自行 `delete/free`；
- callback table 必须比由它创建的全部 token 活得更久。

direct callback descriptor 是 exact-build C++ contract，不是 stable C ABI。同一个 exact Host build/link closure 中的 Host 与 providers
使用相同 compiler、configuration、runtime library 和 headers；Engine generation ID 本身不代替这些 build facts。跨 generation、
跨 process、DLL discovery 与 hot unload 不在 v1 内。

### 2. 生命周期后置条件现在冻结，但本 Slice 不执行

后续 executor 必须遵守：

1. `create` 成功时返回非空 owning token；失败时返回空 token，并由 provider 清理全部 partial construction；
2. 成功 create 后，Host executor 取得唯一 lifecycle responsibility。每个成功 token 最终移动进恰好一次 `destroy`；
3. `activate` 失败后，token 仍是“未激活但可 destroy”的有效 instance，且不得遗留 Host-owned contribution/lease。其他外部
   side effect 的补偿是 provider author obligation，Host 无法普遍证明或回滚；
4. 正常停止按 `quiesce -> deactivate -> destroy` 进行。quiesce/deactivate failure 产生 diagnostic，但不能阻止后续 cleanup；
5. `destroy` 没有可拒绝 cleanup 的 result，必须能处理“create 成功但从未 activate”的 instance；它不要求幂等；
6. create/activate/quiesce/deactivate wrapper 必须在 callback 内捕获并转换自身异常。`destroy` 没有 result/context 或可恢复失败，
   必须 intrinsically non-throwing 并完成剩余释放；从任一 `noexcept` callback 抛出都会 terminate，属于 provider programming
   error，不是 Host 可恢复 diagnostic；
7. dependency-first activation、reverse-order rollback/shutdown、scope owner 与 lease 继续由 Blueprint 和 future executor 决定，
   不能从 table 的 canonical identity order 推断。

#291 测试 value/token contract、descriptor/table 完整性与“零 callback invocation”，但不执行 lifecycle。上述运行后置条件由
lifecycle Slice 的执行测试证明。

### 3. Provider registration contract 硬切到 v2

registrar surface 改为：

```cpp
void registerFactory(
    std::string_view localFactoryId,
    StaticFactoryCallbacksV1 callbacks) noexcept;

using StaticFactoryProviderV2 =
    void (*)(StaticFactoryRegistrar& registrar) noexcept;
```

尽管顶层 provider function 的 C++ spelling 仍是 `void(StaticFactoryRegistrar&) noexcept`，registrar 可调用 surface 已发生 breaking
change。因此 `providerApi` discriminator 硬切为：

```text
asharia-static-factory-provider-v2
```

`providerApi` 是明确版本化的 API discriminator。本次按早期硬切处理：

- author `Package Static Factory Bindings` 与 derived `StaticFactoryProviderBindingPlan` 各提升为 schema/model v2；current
  reader/writer/planner 只接受或产生 v2，不保留 active v1 adapter；
- repository 不保留 v1 bindings/Binding Plan schema reader；旧声明与旧生成物直接拒绝；
- `Static Composition Root` document shape 保持 schema v1，但只接受 renderer revision 3 + provider API v2；
- `StaticFactoryProviderV1` C++ alias、单参数 `registerFactory()` overload 与 executable legacy path 全部删除。

顶层 v1/v2 provider function 的底层 C++ 类型相同，因此 `decltype`/`std::same_as` 只能证明 outer entry signature，不能单独证明
v2 语义。v2 evidence 来自 bindings/Binding Plan discriminator、当前 registrar header surface、generated expected context 与 descriptor
completeness validation 的组合。

provider 仍只能提交 module-local factory ID 与 descriptor，不能提交或覆盖 package ID、package version、module ID、provider entry
point、scope、requirements 或 contribution ownership。generated root 继续注入这些 exact expected facts。

### 4. Provider 只是纯 descriptor publisher

provider contract 要求 invocation 只做若干次：

```text
registerFactory(local factory ID, complete descriptor)
```

该窗口禁止 IO、动态分配、thread creation、process-global mutation、lifecycle invocation 与 static-constructor auto-registration。
callback address 只是要复制的 process-local value；registration 不探测 symbol、RTTI/type name，也不试调用 callback。这是 provider
author obligation；#291 不能从任意 native code 的外部行为机械证明纯度。

这条规则使 recorder 能承诺 output atomicity，但不能声称能撤销任意 provider 副作用：

- recorder 在全部 expected providers/factories 成功前不暴露 table 或 snapshot；
- first error sticky；发生错误后，后续 `invokeProvider()` 不执行 provider function；
- 已写入 private staging state 的 descriptor 不可见，并随 failed recorder 销毁；
- 若 provider 违反纯发布合同产生外部副作用，Host 无法回滚，该行为属于 provider bug。

### 5. 单次 registration 同时形成 table 与 identity projection

不新增第二个 callback-table builder，也不再次调用 providers。#291 当前只把这条 pipeline 接到 unit/integration tests 与 #288 使用的
disposable registration-only Host：

```text
generated capacity
  -> create recorder and preallocate
  -> begin composition(exact generation + Blueprint digest)
  -> invoke each exact provider once
       -> register(local ID + descriptor)
  -> end composition
  -> move(recorder).finish()
       -> frozen callback table | first owning error
  -> table.registrationSnapshot()
       -> existing canonical identity-only snapshot
```

`FactoryObservation` 在现有 private recording state 中增加 by-value descriptor。capacity 仍在任何 provider 被调用前完成验证与预留；
provider callback window 中不得发生 container growth 或 allocation。preallocation failure 必须使 provider invocation count 保持为零。

`finish() && noexcept` 的返回类型固定为：

```cpp
StaticFactoryRegistrationResult<StaticFactoryCallbackTableV1>
```

成功 table 是 move-only/frozen。`finish()` 在同一事务中完成 canonical sort、owning snapshot 与 aligned descriptor storage；任何
post-recording allocation/materialization failure 都返回 `AllocationFailed`，不暴露 table 或 snapshot。table 内部完整拥有：

- composition generation ID；
- Host Activation Blueprint SHA-256；
- 一个 owning `StaticFactoryRegistrationSnapshotV1`，保存 canonical exact registration identity text；
- provider entry point attribution；
- 与 snapshot registrations 相同 canonical index 对齐的 by-value callback descriptors。

public projection 固定为：

```cpp
[[nodiscard]] const StaticFactoryRegistrationSnapshotV1&
registrationSnapshot() const noexcept;
```

它返回 table-owned snapshot 的 read-only reference，只在 table lifetime 内有效，不再次分配或重新 canonicalize。future exact lookup
在 snapshot registrations 上 binary search，并用相同 index 取得 private descriptor；不为这一小型 immutable index 把
`unordered_map` 扩散到 public headers。

table move 或 destruction 会使此前取得的 snapshot reference 失效；caller 必须在 move 后重新取得 reference。future admission
成功并开始 lifecycle 后，table 固定由 executor owning state 持有，不再移动，直到全部 tokens 已 destroy。

`registrationSnapshot()` 只投影 #289 已有字段和 canonical order。它不得包含 callback address、token、result、table-local index
或 lifecycle state；同一 identity inputs 在 provider/local registration 顺序变化后必须继续产生 byte-identical snapshot JSON。

### 6. Callback address 永远不是 evidence

function pointer value：

- 不参与 factory identity、排序、相等性、hash、generation ID、JSON、diagnostic 或 receipt；
- 不要求唯一。不同 factories 可以共享同一 no-op 或 implementation address；
- 不用于推断 callback 类型、owner 或 provider；
- 不通过 symbol name、RTTI 或 pointer canonicalization 持久化；
- 不嵌入 #288 post-build binding receipt ID，避免 executable hash 的自引用循环。

ASLR、link-time identical code folding 与 compiler/linker 选择都可能改变或合并地址，而不改变 logical factory identity。#288 的
receipt 继续只保存 exact executable bytes/hash 与 identity snapshot；新的 executable bytes 自然形成新的 receipt generation，无需修改
receipt schema、collector、publisher 或 deep verifier。

recorder 也不能从 pointer value 证明 callback code 属于声明的 provider/target。direct generated provider 与 exact static link closure
提供调用来源，provider 对所提交 callback code provenance 负责；Host 只验证 generated provider context、local factory ownership 与
descriptor completeness，不做 pointer-range 或 symbol ownership 检查。

### 7. Long-lived Host 需要 pre-registration 与 activation 两道 admission

成功 table 只能证明本次 generated registration pipeline 接受了一套完整 descriptor。它不能单独证明：

- table 来自 #288 receipt 所描述的 exact executable bytes；
- 当前 long-lived Editor/Runtime process 已加载 expected generation；
- project/session 仍要求同一 Blueprint；
- callback 能成功创建或激活 instance；
- Host 已经进入 `Ready`。

provider function 自身就是项目 native code。长生命周期 Editor/Runtime 若在 receipt/current-image 校验前先构建 table，即使不调用
lifecycle callbacks，也已经违反 Safe Mode 的“未验证前不执行项目代码”边界。因此后继 Eligibility 必须按两阶段执行：

```text
Ready session + deep-verified receipt + current image match
  -> PreRegistrationAdmission
  -> invoke generated providers and build frozen table
  -> cross-check table snapshot == receipt registration + expected Blueprint
  -> ActivationAdmission
  -> admitted ProcessScope executor may access descriptors
```

missing/stale receipt、session/Blueprint mismatch 或 current-image mismatch 必须在第一阶段 fail，使 provider invocation count 为零。
table identity mismatch 在第二阶段 fail，不产生 descriptor access permission，也不进入 lifecycle。

public table API 不提供任意 consumer 可直接调用的 raw callback lookup。#291 只公开 table identity、generation/Blueprint attribution
与 RegistrationSnapshot projection。private callback access 已由 admitted ProcessScope executor 使用，未来其他 scope executor 也必须取得
constructor-restricted Host Runtime `ActivationAdmission`，才能通过正常 API 读取 descriptor；production Host 接线仍未实现。
实现上 table 只在 public type 中持有不透明 immutable storage；snapshot 通过 public projection 读取，callback storage layout 与 access bridge
均留在 registration/Host Runtime 的 PRIVATE 源码边界，避免低层 registration 公共头反向依赖高层 eligibility 类型。

两个 admission token 只防止 Host 代码意外绕过正常流程，不是 sandbox/security capability。同进程 native package 始终能调用
自己的函数；hostile native code 不在 v1 threat model。

#288 的 collector-owned disposable registration-only subprocess 是明确例外：它的职责正是首次运行 staged bytes 取得 receipt 所需
identity evidence；它使用受限 argv/environment，不创建 lifecycle instance，输出后退出，也绝不成为 long-lived Editor/Runtime
session。该 subprocess 不是恶意 native code sandbox。

“stale generation/Blueprint”在本 Slice 中只指同一次 recording 输入内部出现 mixed/conflicting identity，必须 fail closed。把 table
与 receipt、session 和 current executable/process generation 交叉判断属于 Activation Eligibility；#291 不把 receipt parser、process
inspection 或 Bootstrap state machine 拉进 `engine/host-runtime`。

#291 不把 callback table 接入正常 Editor/Runtime startup；它只实现 contract/table 以及现有 disposable verification path。两阶段
admission 与“pre-admission provider count 为零”的 negatives 属于紧随其后的 Eligibility Slice。

### 8. Generated root 与 registration-only Host 保持窄

generated root 继续只有一个薄 `.cpp` TU，并继续负责：

- exact provider headers/direct function references；
- compile-time outer provider function signature check；
- canonical expected context/capacity；
- 每个 provider 恰好一次的显式 invocation。

provider 实现改为提交 descriptors；不生成 per-package/per-factory TU。由于 generated source 中 provider type/API marker 会改变，
renderer revision 从 2 提升为 3，content-addressed generation identity 随实际 template bytes 变化。

#290 registration-only Host 先得到 frozen table，再投影并输出既有 snapshot。其 restricted mode 必须通过 counter/abort callbacks
证明 create/activate/quiesce/deactivate/destroy 调用次数全部为零。#288 仍运行 staged executable 的该模式，registration snapshot
schema 与 canonical bytes 合同不变。

Host Template 生成的 `main.cpp` 也会从“finish 得到 snapshot”改为“finish 得到 table，再读取 stored snapshot”，因此 Windows
Development Host Template renderer revision 从 1 提升为 2。current generator、schema 与 deep verifier 只接受 revision 2；旧
revision identity 不能声明新的 source bytes，也不保留兼容读取路径。

唯一合法组合是 Host Template renderer 2 + Static Composition renderer 3 + provider API v2。build request、receipt assembly 与
read-only deep verifier 都必须在进入 CMake 或发布证据前复验这一组合；任何旧 revision/provider 或交叉组合全部 fail closed。

## 失败合同

在 #289 已有错误之外，至少增加稳定分类：

- descriptor 的 create/activate/quiesce/deactivate/destroy 任一 slot 为空；
- provider API discriminator 不是 v2；
- duplicate exact factory identity，无论 descriptors 相同或不同；
- mixed/conflicting generation 或 Blueprint context；
- generated provider context/local factory ownership mismatch、unknown/extra/missing local factory；
- factory capacity mismatch 或 preallocation/materialization failure；
- moved-from、重复 finish、provider 外 registration 或 sticky error 后的 misuse。

失败只返回 first owning diagnostic；不返回 partial table 或 partial snapshot，也不打印 pointer value。不同 identity 使用相同 callback
addresses 是合法正例，不得误报 duplicate。

## 实现拆分与编译效率

建议最小文件边界：

| 文件 | 职责 |
| --- | --- |
| `include-contract/.../static_factory_callbacks.hpp` | 轻量 token/result/context forward declarations/callback descriptor |
| `include-provider/.../static_factory_instance_token_provider_access.hpp` | provider-only pointer/token bridge，经独立 PRIVATE include target 消费 |
| `include-contract/.../static_factory_provider.hpp` | v2 registrar 与 provider function contract |
| `include/.../static_factory_callback_table.hpp` | move-only frozen table 的 Host-facing identity/projection API |
| `src/static_factory_registration_state.*` | 唯一 provider window、capacity、sticky failure 与 staged descriptor copy |
| `src/static_factory_registration.cpp` | recorder orchestration，不继续堆入 table materialization |
| `src/static_factory_callback_table.cpp` | owning table、canonical sort/index 与 snapshot projection |
| `tests/static_factory_callback_table_tests.cpp` | descriptor/table 专项 suite，由现有 test `main` 调用，不定义第二个 `main` |

编译边界：

- provider contract header 只引入 `<cstdint>` 与 `<string_view>`，不引入 `vector`、`expected`、`memory`、`std::function` 或 table
  implementation；
- provider pointer bridge 只增加一个 header-only INTERFACE include target，不产生编译或链接 artifact；普通 Host targets 不取得该
  include directory；
- table `.cpp` 加入现有 `asharia-host-runtime-registration` target，不新建 library target；
- 新测试 TU 加入现有 test executable，不为一个小边界增加链接目标；
- generated composition 仍只有一个 TU；不触碰全局 PCH、unity、C++ Modules scanning、clang-tidy jobs 或 compiler cache policy。

## 拒绝的方案

### Abstract factory base class

拒绝。public vtable、virtual destructor、跨边界 delete/allocator 与 exception 语义都比五个 direct callbacks 更宽，并会产生并不存在的
stable C++ ABI 暗示。

### `create` 返回 per-instance vtable

拒绝。Host 无法在 create 前验证完整 lifecycle descriptor，instance 还能把已验证实现替换成另一套 callbacks，并增加一次间接
lifetime/ownership。

### `std::function`、capturing lambda 或 external `userData`

拒绝。它们引入 hidden allocation、borrowed capture lifetime 与跨边界 destruction；package state 应由 opaque instance token 拥有。
non-capturing lambda 只有在显式转换为完全匹配的 `noexcept` function pointer 后才等同普通 callback。

### Global registry、static constructor 或 runtime symbol lookup

拒绝。它们绕过 generated exact provider list，把 link/load/initialization timing 或字符串拼写变成隐藏输入。

### Optional/null lifecycle slot

拒绝。缺失 callback 会把合法 state transition 变成运行期猜测。无操作必须是显式 typed no-op。

### 将 table 序列化到 receipt

拒绝。pointer/token 只在当前 process 有意义；#288 receipt 保存 executable bytes 与 identity evidence 已足够。

### Table 完成后直接对外开放 callback lookup

拒绝。它会允许 consumer 绕过 receipt/current-process/session eligibility，错误地把 registration success 解释为 activation permission。

## 验证要求

- 五个 slot 各自为 null 的 focused negatives；完整 descriptor positive；
- type traits 与 generated synthetic compile negative 证明错误参数、返回类型、缺少 `noexcept` 或错误 provider signature 不可进入
  typed slot；Host-facing table API 不公开 descriptor lookup；provider-only token bridge 只通过独立 PRIVATE target 提供；
- duplicate/unknown/missing identity fail closed；provider 与 local registration 顺序扰动后 identity snapshot bytes 不变；
- 使用不同 callback addresses 的等价 identity 产生相同 snapshot；JSON/diagnostic 不出现 address；
- descriptor 与 identity 来源 stack storage 销毁后 table 仍完整有效；
- create/preallocation failure 与中途 unknown/extra/duplicate 均不暴露 table/snapshot；sticky failure 后不再调用后续 provider；
- mixed generation、Blueprint mismatch、generated provider/local owner mismatch fail closed；external stale receipt/process 判断留给
  Activation Eligibility；
- registration-only 与 #288 staged Host 回归以 abort/counter callbacks 证明 lifecycle invocation count 为零；
- callback result factories 不能表达 success/nonzero-code、success/null-token 或 failure/non-null-token；unknown/corrupt status 的 future
  executor handling 保留 focused contract test；
- move constructor、`takeInstance()` 与 provider consuming access 均使 source invalid；valid-token drop 触发 fatal invariant negative；
- static-composition renderer revision 3 + Host Template renderer revision 2 的 valid generated Host 在 ClangCL/MSVC 下 configure、
  compile、link、execute；wrong signatures 编译失败；旧 revision/provider 与 cross-revision combinations 全部拒绝；
- #289 RegistrationSnapshot schema/canonical bytes 兼容，#288 collection/publication/deep verification 保持绿色；
- full Python/contracts/topology/encoding/doc-sync/diff 与 Conan-before-CMake 双编译器 builds/tests。

## 后继边界

1. [Activation Eligibility v1](adr-activation-eligibility-v1.md)（#292）：C++ boundary 已实现；先以 constructor-restricted Ready
   Session/Blueprint/deep-verified binding/launch handoff 产出 `PreRegistrationAdmissionV1`，再以同一次 admitted recording 的 exact
   table instance 与 snapshot 产出 `ActivationAdmissionV1`。最终门禁结果以 #292 Done evidence 为准，production launch issuer 与
   normal Host 接线仍待实现；
2. [ProcessScope Lifecycle v1](adr-process-scope-lifecycle-v1.md)（#293）：已实现第一个 concrete contexts/executor，按 Blueprint order
   create/activate，失败时 reverse rollback，正常 explicit stop 时 quiesce/deactivate/destroy，并已完成门禁；production issuer 与
   normal Host/Bootstrap adapter 仍待后续；
3. 其余 scope、typed contributions 与 activation leases：在 ProcessScope 证据稳定后逐层增加；
4. Bootstrap/Session adapter：把 build/receipt/process/runtime outcome 映射为 PendingBuild、PendingRestart、Ready 或 SafeMode；
5. exact-build native DLL 仅在静态模型稳定且链接数据证明有必要时单独设计；v1 不承诺通用插件 ABI 或 hot unload。
