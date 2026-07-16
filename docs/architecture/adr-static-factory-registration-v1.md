# ADR：Static Factory Registration v1

## 状态

Accepted and implemented for #289。本 ADR 冻结 `engine/host-runtime` 的 identity-only 静态 factory registration
边界。它把 generated static composition root 的 expected provider/factory evidence 记录为 owning、canonical snapshot，
但不保存 factory callback，不创建 instance，也不执行 Host lifecycle。

[Static Factory Callback Table v1](adr-static-factory-callback-table-v1.md) 已为 #291 完成后继硬切：同一次 provider invocation
提交 local factory ID 与完整 typed descriptor，并由 frozen table 投影本 ADR 的 identity snapshot。本 ADR 记录 #289 的历史
identity-only 决策；active executable path 不再提供单参数 registrar、`StaticFactoryProviderV1` 或兼容 adapter。

[Static Typed Contribution Contract Bindings v1](adr-static-typed-contribution-contract-bindings-v1.md) 已为 #294 再次硬切 active recorder：
Capacity/Context 为 v2，provider 为 v3，table 投影 RegistrationSnapshot v2；本 ADR 下文的 identity-only Snapshot v1 仅保留为 #289
历史记录。

[Static Contribution Payload Accessors v1](adr-static-contribution-payload-accessors-v1.md) 已由 #295 实现 provider v4 与
`StaticContributionBindingV2` 的后继硬切；Capacity/Context 与 RegistrationSnapshot 仍保持 v2，registration 只复制 private
payload accessor evidence，绝不调用 accessor。

## 问题

[Generated Static Composition Root v1](adr-generated-static-composition-root-v1.md) 已经能够直接引用并调用每个
`StaticFactoryProviderV1`，但 #287 只拥有一个不完整的 `StaticFactoryRegistrar` 类型。因此编译和链接只能证明：

- provider header 中的函数签名正确；
- selected provider target 进入了 final link closure；
- generated source 会按 canonical provider order 发起调用。

这些事实不能证明 provider 实际声明了哪些 factory。若直接把 callback、instance、scope 或 activation API 塞进 registrar，
则 registration evidence、runtime lifecycle 和最终 Host artifact 又会被压回一个边界；若允许 provider 自报完整 package identity，
则 provider 还能伪造 generated plan 已经验证过的 package/version/module/entry point。

本阶段只需要回答一个更窄的问题：

> 在一个 exact static-composition generation 下，每个已验证 provider 是否恰好登记了 generated root 预期的 local factory IDs？

## 所有权

| 能力 | owner | 不拥有 |
| --- | --- | --- |
| provider 可见的窄 registrar | `engine/host-runtime` public contract | package identity、callback、instance、lifecycle |
| recording state 与失败状态 | `StaticFactoryRegistrationRecorder` | Host `main()`、build、artifact collection |
| complete expected provider context | generated static composition root | package resolution、activation order |
| canonical registration evidence | `StaticFactoryRegistrationSnapshotV1` | final executable bytes、进程 loaded-generation state |

`engine/core`、`engine/platform` 和 `engine/package-runtime` 不依赖该实现。provider module 可以向内依赖 Host Runtime 的
public contract；Host Runtime 不反向依赖任何 package-specific header。

## 决策

### 1. Provider 只能登记 local factory ID

provider API 继续保持：

```cpp
using StaticFactoryProviderV1 =
    void (*)(StaticFactoryRegistrar& registrar) noexcept;
```

`StaticFactoryRegistrar` 只公开：

```cpp
void registerFactory(std::string_view localFactoryId) noexcept;
```

provider 的合法实现形状为：

```cpp
void provideRuntimeFactories(
    asharia::host_runtime::StaticFactoryRegistrar& registrar) noexcept {
  registrar.registerFactory("runtime");
}
```

provider 不能传入 package ID、version、module ID、entry point 或全局 factory reference。这些值已经由 Binding Plan 和
generated root 验证，必须由调用侧注入，不能重新信任 package callback。

registrar 不是容器，也不能由 provider 构造、复制或移动。provider contract 禁止保存它的地址；在 recorder 仍存活时于
invocation 之外调用会形成 recorder error，而不是写入 process-global registry，recorder 销毁后的地址则没有合法 lifetime。

### 2. Generated root 注入完整 composition/provider context

renderer revision 2 的 generated header 导出：

```cpp
[[nodiscard]] asharia::host_runtime::StaticFactoryRegistrationCapacityV1
staticFactoryRegistrationCapacity() noexcept;

void recordStaticFactoryProviders(
    asharia::host_runtime::StaticFactoryRegistrationRecorder& recorder) noexcept;
```

generated source 负责：

1. 以 `StaticCompositionRegistrationContextV1` 调用 `beginComposition()`，注入 generation ID、
   Host Activation Blueprint SHA-256 和 exact capacity；
2. 为每个 canonical provider 构造 `StaticFactoryProviderContextV1`，注入 package ID、package version、module ID、
   qualified entry point 与 canonical expected factory IDs；
3. 通过 `invokeProvider(context, provider)` 调用 exact typed function；
4. 全部 provider 完成后调用 `endComposition()`。

概念流程为：

```text
staticFactoryRegistrationCapacity()
  -> createStaticFactoryRegistrationRecorder(capacity)
  -> recordStaticFactoryProviders(recorder)
       -> beginComposition(generation + blueprint + capacity)
       -> invokeProvider(exact provider context, typed provider)
            -> provider(registrar)
                 -> registerFactory(local factory ID)
       -> endComposition()
  -> move(recorder).finish()
       -> complete snapshot | first owning error
```

[Windows Development Host Template v1](adr-windows-development-host-template-v1.md) 的 restricted verification mode 或 test harness
只负责调用这两个 generated entry points；它们不得重新枚举或构造 provider list。

### 3. Capacity 在 provider 调用前一次性预留

`StaticFactoryRegistrationCapacityV1` 包含：

- `providerCount`：expected provider 数；
- `factoryCount`：expected factory 总数；
- `textBytes`：generation、Blueprint digest、provider identity、entry point 与 factory IDs 所需的 UTF-8 owning storage bytes。
- `diagnosticFactoryIdBytes`：首次失败时保存 offending factory ID 的独立 owning buffer；renderer 至少预留 256 bytes，
  若合法 expected ID 更长则取其最大 UTF-8 byte length。

capacity 由同一个 generated renderer 从 canonical inputs 计算。`createStaticFactoryRegistrationRecorder()` 在任何 provider
被调用前验证 capacity，并预留 recorder 所需的 owning storage。allocation failure 以
`StaticFactoryRegistrationErrorCode::AllocationFailed` 返回；不能在 provider 已开始执行后再尝试恢复。

provider callback 调用期间必须零动态分配。该约束使 `noexcept` provider 不会因为 recorder 扩容而 terminate，也避免
package callback 的 registration 行为受 allocator timing 影响。它不承诺整个 Host lifecycle 无分配；construction、snapshot
所有权转移和未来 instance creation 属于各自边界。

### 4. Recorder 使用显式状态机与 sticky first error

`StaticFactoryRegistrationRecorder` 是 move-only、单次使用对象。合法顺序固定为：

```text
create -> begin composition -> invoke zero or more providers -> end composition -> finish
```

v1 由 Host owning thread 串行驱动；recorder、registrar 与 provider invocation 都不是并发 API。跨线程登记或在另一个
provider invocation 中重用保存的 registrar 地址不属于合同。

以下错误通过 `StaticFactoryRegistrationErrorCode` 分类：

- invalid capacity/context 或 capacity/text overflow；
- begin/end/finish 顺序错误、moved-from 或重复 finish；
- provider 缺失、重复、嵌套、位于 composition 外或数量不符；
- expected factory IDs 非 canonical；
- factory 位于 provider 外、unknown、duplicate、missing 或数量不符。

所有 public recording operations 都是 `noexcept`。首次错误先被捕获到 recorder-owned fixed failure state 并成为 sticky；
后续调用不得覆盖第一错误，也不得暴露部分成功 snapshot。`finish() &&` 只在完整且无错误时返回 snapshot，否则把首次错误
物化为 owning `StaticFactoryRegistrationError`。

错误只保存归因所需的 package/version/module/entry point/factory ID，不保存悬空 `string_view`。若 provider 提交的 unknown ID
仍超过预留 diagnostic buffer，recorder 返回显式 `DiagnosticFactoryIdCapacityExceeded` 并记录 observed byte count，绝不把静默
截断的 prefix 伪装成完整 `FactoryUnknown`。

### 5. Snapshot 是 canonical owning evidence

成功的 `StaticFactoryRegistrationSnapshotV1` 保存：

- `generationId`；
- `hostActivationBlueprintSha256`；
- `registrations`。

每个 `StaticFactoryRegistrationV1` 保存 exact package ID、package version、module ID、factory ID 和 provider entry point。
所有字符串和 registration records 均由 snapshot 拥有，不借用 generated arrays、provider stack 或 recorder storage。

输出按 exact registration identity 的 UTF-8 bytes 规范化。provider 内部调用 `registerFactory()` 的顺序不成为语义；
registration snapshot 也不定义 create/activate 顺序。factory activation DAG 的唯一权威仍是
[Host Activation Blueprint v1](adr-host-activation-blueprint-v1.md)。

snapshot 是未来 receipt 的输入，不是新的 lockfile，不写回 Project Lock、Blueprint、Binding Plan 或 composition manifest。

### 6. 信任与证明边界

本合同能证明：

- generated root 提供的 exact provider context 与 composition generation 被 recorder 接受；
- 在 generated root 执行路径上，每个 canonical provider 恰好被调用一次；
- provider 只登记了自己的 expected local factory IDs，且无 missing/extra/duplicate；
- 成功结果不借用 provider 或 generated source 的临时内存。

本合同不能单独证明：

- 哪一个 final executable bytes 执行了该 generated root；
- 当前 Editor/Runtime 进程已经加载该 generation；
- factory callback 可创建有效 instance；
- scope、dependency、activation、rollback 或 shutdown 成功。

[Windows Development Host Template v1](adr-windows-development-host-template-v1.md) 已为 #290 提供 final executable target、File API
path binding 与受限 verification entry；这些仍不证明 artifact bytes。
[Host Executable Binding Receipt v1](adr-host-executable-binding-receipt-v1.md) 已由 #288 把 composition manifest、same-index
configured compiler、registration snapshot 与 exact staged Host artifact bytes 交叉绑定。

## 编译效率边界

- #289 historical provider public header 只暴露 `string_view` registrar API，不包含 recorder implementation 或 lifecycle types；
  #295 current v4 surface 暴露 `StaticContributionBindingV2` span 与 exact typed accessor helper，但 recorder implementation 仍保持 PRIVATE，registration 也不得调用 accessor；
- recorder implementation 使用 private state，不把容器算法扩散到每个 provider TU；
- 一个 Host composition 仍只生成一个薄 TU；renderer revision 变化只使该 TU 和 final Host link closure 失效；
- #290 的 fixed Host 只增加一个薄 `main.cpp`，并只构建 exact target，不要求 clean-first；
- capacity 是常量生成数据，不通过运行时扫描或第二份 provider index 计算；
- 本 Slice 不引入 PCH、unity build、module scanning、clang-tidy jobs 或全局 build policy 变化。

## 拒绝的方案

### Provider 登记完整 package/factory reference

拒绝。provider 不应重新声明或伪造 generated plan 已验证的 package/version/module/entry point；它只拥有 module-local factory ID。

### Registrar 同时保存 factory callbacks

拒绝。callback signature、factory context、allocator ownership、scope 和失败清理尚未冻结。identity evidence 不需要提前决定这些 ABI。

### 通过 global registry 或 static constructor 自动收集

拒绝。它绕过 exact generated provider list，并让 link/initialization timing 成为隐藏输入。

### 在 provider callback 中按需扩容

拒绝。它破坏 `noexcept` failure boundary，也让注册能否成功依赖 allocator timing。v1 必须在进入 callback 前完成 capacity validation
与 storage reservation。

### Snapshot 保存 `string_view`

拒绝。generated arrays、provider stack 和 recorder 的 lifetime 都短于未来 receipt；evidence 必须完整拥有自己的 bytes。

## 不做事项

- 不保存 function pointer、factory callback、deleter、allocator 或 opaque instance state；
- 不创建 factory instance、scope tree、typed contribution registry 或 activation lease；
- 不执行 create/activate/quiesce/deactivate/destroy、rollback 或 shutdown；
- 不定义 Host `main()`、Build Profile、Host Template、verification process 或 final target；
- 不收集 object/library/executable bytes，不生成 #288 post-build receipt；
- 不实现 dynamic loading、hot reload/unload 或 stable cross-generation C++ ABI；
- 不把 snapshot 持久化为 Project/Distribution dependency truth；
- 不修改 resolver、Host Profile、Factory Declaration、Source Build Plan、Blueprint 或 Provider Binding 的所有权。

## 验证要求

- valid empty/single/multi-provider composition produces owning canonical snapshot；
- provider 内 factory registration 次序变化仍产生相同 canonical registrations；
- invalid capacity/context、lifecycle misuse、nested/duplicate/missing provider 与 unknown/duplicate/missing factory fail closed；
- sticky first error 不被后继错误覆盖，失败不返回 partial snapshot；
- exact-capacity positive case 与 text-capacity exhaustion 都有 focused execution evidence；
- fixed-size recording storage、索引写入与 ClangCL `noexcept`/exception-escape gate 共同证明 provider callback window
  不含可能扩容的容器操作；
- generated revision 2 注入 exact generation、Blueprint digest、provider context、expected factory IDs 与 capacity；
- generated valid fixture 在 ClangCL/MSVC 下 configure、compile、link、execute 并验证 snapshot；
- full Python/contracts/topology/encoding/docs/diff 与双编译器 gates。

## 后继边界

1. [Windows Development Host Template v1](adr-windows-development-host-template-v1.md)：#290 已拥有固定 final target、`main()`、
   final configure/build、File API target binding 与 registration-only verification；
2. [Host Executable Binding Receipt v1](adr-host-executable-binding-receipt-v1.md)：#288 已把 exact composition manifest、
   owning registration snapshot 与 collector-owned final Host artifact bytes/hash 对证；
3. [Static Factory Callback Table v1](adr-static-factory-callback-table-v1.md)：#291 先绑定完整 typed callbacks，但不调用它们；
4. Activation Eligibility 与 concrete Host Runtime lifecycle：长生命周期 Host 先对证 receipt/current process 才允许调用
   providers，table snapshot 再次对证后才允许 context、scope instance、activation、rollback 与 shutdown；
5. Bootstrap/Session adapter：把 build/receipt/process generation 状态映射为 Ready、PendingBuild、PendingRestart 或 SafeMode。
