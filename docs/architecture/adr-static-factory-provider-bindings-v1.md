# ADR：Static Factory Provider Bindings v1

## 状态

Accepted and implemented for #286。本 ADR 冻结逻辑 Factory 与构建期静态 C++ provider 入口之间的最小绑定合同、证据链和失败边界。

它位于 [Source Build Plan v1](adr-source-build-plan-v1.md) 与
[Host Activation Blueprint v1](adr-host-activation-blueprint-v1.md) 之后、generated static composition root 之前。它不生成 C++，也不实现 Host Runtime。

## 问题

当前两个已验证计划分别只能回答：

- Host Activation Blueprint：本次 Host 需要哪些 exact logical factories，以及它们的 scope、依赖和 contribution ownership；
- Source Build Plan：本次 Host 需要链接哪些 exact CMake build roots 和 target closure。

两者都故意没有保存 C++ header、function 或动态库信息。因此还缺少一条证据：

> Blueprint 中的 factory，究竟由已选择静态 target 中的哪个类型安全 C++ 入口提供？

如果不先冻结这条边界，composition-root generator 只能依赖进程全局注册表、静态初始化顺序、字符串 symbol lookup，或把 package-specific C++ 表达式硬编码在 generator 中。

## 资料验证

| 资料 | 官方行为 | 对 Asharia 的约束 |
| --- | --- | --- |
| [CMake `target_sources`](https://cmake.org/cmake/help/latest/command/target_sources.html) | generated source 可作为现有 target 的 `PRIVATE` source 明确加入 | composition root 可以是显式生成输入，不需要静态构造发现 |
| [CMake `target_link_libraries`](https://cmake.org/cmake/help/latest/command/target_link_libraries.html) | target 名称携带链接依赖与 usage requirements | provider 必须绑定到 Source Build Descriptor 已拥有的 target，而不是猜测文件名 |
| [O3DE Gem Module System](https://www.docs.o3de.org/docs/user-guide/programming/gems/module-system/) | monolithic/static module 使用显式 module entry point 注册 | 静态组合与明确入口可以共存，不要求 package 等同于 DLL |
| [Unreal Engine Modules](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules) | build target 决定 module 编译/链接闭包 | build closure 与 runtime lifecycle 是两层证据，不能互相推断 |

这些资料支持“显式静态入口 + build target ownership”的方向，但不要求 Asharia 复制其 ABI 或插件系统。

## 决策

### 1. 使用独立的源码构建 sidecar

可安装 source package 可以在 payload root 提供：

```text
asharia.package.static-factory-bindings.json
```

discriminator 为 `com.asharia.package-static-factory-bindings`，schema version 为 `1`。该 sidecar 是 exact source candidate 的构建证据，不是：

- installable package intent；
- Project Lock；
- Host Profile；
- artifact manifest；
- 跨 Engine Generation 的 native ABI 承诺。

`asharia.package.factories.json` 继续只拥有 logical factory、scope、requirements 与 contributions；`asharia.package.build.json` 继续只拥有 module/source-boundary/CMake target roots。三者不互相吞并。

### 2. 固定 provider API 版本，不把 registration 与 lifecycle 混合

v1 sidecar 固定：

```text
providerApi = asharia-static-factory-provider-v1
```

它代表未来公共 Host Runtime header 中的函数类型：

```cpp
void provideFactories(
    asharia::host_runtime::StaticFactoryRegistrar& registrar) noexcept;
```

这里冻结的是 build-time 类型形状：

- function 必须位于 `asharia::...` namespace；
- generator 通过 public header 取得声明并直接引用函数；
- generated source 必须用 `decltype`/function-pointer type check 证明签名匹配；
- registrar 由 composition root/Host Runtime 显式拥有并作为参数传入；
- provider 不通过全局 mutable registry、静态 constructor 或字符串 symbol lookup 被发现。

本 Slice 当时不定义 concrete registrar。后继
[Static Factory Registration v1](adr-static-factory-registration-v1.md) 已为 #289 将它收窄为 identity-only API：provider
只能调用 `registerFactory(localFactoryId)`；package/version/module/entry point 与 expected factory IDs 由 generated root 注入
recording context。registrar 仍不接收 callback，不创建、激活或销毁 system instance。

### 3. 合同形状

简化示例：

```json
{
  "schema": "com.asharia.package-static-factory-bindings",
  "schemaVersion": 1,
  "package": {
    "id": "com.asharia.system.example",
    "version": "1.0.0"
  },
  "providerApi": "asharia-static-factory-provider-v1",
  "modules": [
    {
      "moduleId": "contract",
      "binding": {
        "kind": "no-providers"
      }
    },
    {
      "moduleId": "implementation",
      "binding": {
        "kind": "provider-set",
        "providers": [
          {
            "target": {
              "name": "asharia_example",
              "type": "STATIC_LIBRARY"
            },
            "entryPoint": {
              "header": "asharia/example/static_factory_provider.hpp",
              "function": "asharia::example::provideStaticFactories"
            },
            "factoryIds": [
              "example-service"
            ]
          }
        ]
      }
    }
  ]
}
```

v1 规则：

- author manifest 的每个 logical module 恰好有一个 binding record；
- `no-factories` module 必须使用 `no-providers`；
- `factory-set` module 必须使用 `provider-set`；
- 每个 logical factory ID 在其 module 中由且仅由一个 provider 覆盖；
- provider target 必须是同 module 的 `asharia.package.build.json` target root，且 v1 只允许 `STATIC_LIBRARY`；
- header 是以 `asharia/` 开头的规范化 public include spelling，不能引用另一个 package 的 `src/`；
- function 是受限的 `asharia::...` fully-qualified identifier，不允许 template expression、调用表达式或任意代码片段；
- 同一 package 中一个 `(header, function)` entry point 只能声明一次，防止 generated root 重复调用。

### 4. 证明“selected target”的方式

本 Slice 不持久化第三份依赖选择真相。它从已经验证的输入派生一个可丢弃、可重建的
`StaticFactoryProviderBindingPlan`，只记录“哪些已选择 factory 由哪些已选择静态 target/entry point 提供”；它不重新选择
package、module、factory 或 target，也不写回 Project Lock。证明链为：

```text
Ready Effective Session
  └─ verified exact candidate
       ├─ Factory Declaration
       ├─ Source Build Descriptor
       └─ Static Factory Provider Bindings

Host Activation Blueprint
  └─ selected exact package/module/factory

Source Build Plan
  └─ selected exact package/module/target root + target closure

Static Factory Provider Binding Plan（派生 handoff）
  └─ input fingerprints + exact selected provider calls
```

author binding validator 已证明 provider target 属于同一 exact package/module 的 build descriptor。binding planner 再同时复验：

1. Blueprint factory exact reference 存在于 verified candidate binding；
2. binding module 与 Blueprint module 相同；
3. Blueprint 与 Source Build Plan 来自同一 Ready Session/Host Composition，Host 与 Engine Generation facts 一致；
4. binding target 同时存在于本次 Source Build Plan 的同一 package/module、`buildRoots` 与 `targetClosure`；
5. 每个 selected factory 恰好进入一个 provider call。

任一步不一致都不产出部分 Binding Plan，也不选择 fallback target 或相似 function。后续 root generator 只消费该 verified
handoff，并通过 generated C++ 的 `decltype`/function-pointer check 证明 public header 中的实际函数签名匹配 provider API。

### 5. Candidate、Locked 与 Effective evidence

Candidate Discovery 对可选 sidecar 执行：

```text
read exact bytes
  -> UTF-8 JSON/schema/semantic/cross-contract binding
  -> payload-tree integrity
  -> re-read and reject TOCTOU
  -> snapshot parsed data + exact bytes + SHA-256 integrity
```

Locked Verification 要求 parsed data、exact bytes 与 integrity 三件套全部存在或全部缺席，并重新验证：

- bytes/integrity；
- bytes/parsed data；
- package ID/version/module；
- Factory Declaration 完整覆盖；
- Source Build Descriptor target ownership/type；
- payload 中 sidecar 的 presence 与 exact bytes。

Effective Session 深拷贝 verified candidate，并把 sidecar data、声明 integrity 与 exact-bytes integrity 纳入 `candidateBindingsIntegrity`。任何内存篡改都会使 Ready Session 复验失败。

sidecar 在 Discovery 层保持 optional，允许 Feature Set、content-only package 与尚未迁移的 package 被发现。Binding planner 对
Blueprint 中任何 selected native factory 强制要求 verified binding；缺失不是 `no-providers`。

### 6. 确定性与失败合同

规范化顺序为：

- modules：`moduleId` UTF-8 bytes；
- providers：target name、header、function UTF-8 bytes；
- factory IDs：UTF-8 bytes。

验证拒绝：

- unknown/missing/duplicate module；
- missing/duplicate/unknown factory binding；
- `no-factories`/`no-providers` 不一致；
- missing build descriptor 或 Factory Declaration；
- unowned target 或 target type mismatch；
- duplicate entry point；
- absolute、非规范化、非 `asharia/` public header；
- unqualified/非法 C++ function token；
- incomplete、stale、TOCTOU 或内存篡改 evidence。

错误返回稳定排序 diagnostics，不返回部分 candidate 或部分 binding。

## 不做事项

- 不生成 composition-root C++ 或 CMake glue；
- 不定义 callback registry、factory context、instance 或 lifecycle API；identity-only recorder 由后继 #289 独立拥有；
- 不创建 factory instance，不执行 create/activate/quiesce/deactivate/destroy；
- 不生成构建后 Activation Binding Receipt；
- 不读取或验证最终 artifact bytes；
- 不把派生 Binding Plan 当作持久化 lock、依赖选择权威或构建后 receipt；
- 不实现 DLL loading、`dlsym`/`GetProcAddress`、hot reload/unload 或跨 generation ABI；
- 不把 CMake target、header 或 function 写回 portable Factory Declaration；
- 不把 source sidecar 变成 Project Lock 或 Engine Distribution Manifest 的第二份 package graph。

## 验证

- closed schema 与 provider API/version；
- exact package/module/factory/build-target cross binding；
- missing/duplicate/unknown/unowned/type mismatch negative cases；
- canonical order independence；
- Candidate Discovery exact snapshot；
- Locked Verification incomplete/stale/on-disk mismatch；
- Effective Session deep-copy 与 fingerprint tamper detection；
- Blueprint/Source Build Plan/selected target 的 exact handoff、missing sidecar、output self-integrity 与确定性；
- repository contract discovery 与 full regression gates。

## 后续边界

1. [Generated Static Composition Root v1](adr-generated-static-composition-root-v1.md)：#287 已消费 verified `StaticFactoryProviderBindingPlan` 并复验其 Source Build Plan/Blueprint fingerprints，生成薄 C++ registration source、compile-time provider signature checks 和受控 CMake attachment；
2. [Static Factory Registration v1](adr-static-factory-registration-v1.md)：#289 已让 generated root 注入 exact provider context，并将 provider 的 local factory IDs 记录为 canonical owning snapshot；
3. #290 Host Template/Build Adapter：拥有 final target、`main()` 与 verification mode；
4. #288 post-build Activation Binding Receipt：对证 Blueprint、registration snapshot 与 exact host artifact generation；
5. Host Runtime lifecycle：实现 typed callbacks、scope tree、factory context、activation lease、rollback 与 shutdown；
6. Bootstrap adapter：把 build/binding/runtime 结果映射为 Ready、PendingBuild、PendingRestart 或 SafeMode。
