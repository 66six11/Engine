# ADR：Generated Static Composition Root v1

## 状态

Accepted and implemented for #287。本 ADR 冻结从 verified static provider handoff 到可直接编译 C++ 组合根之间的
生成、所有权、CMake 接入、确定性和失败边界；closed schema/model、pure renderer、content-addressed atomic publisher、
最小 Host Runtime provider type contract 与双编译器 synthetic CMake evidence 已落地。

它只建立构建期注册入口，不实现 Host Runtime lifecycle，也不验证最终 executable bytes。
[Static Factory Registration v1](adr-static-factory-registration-v1.md) 后续为 #289 扩展 renderer revision 2，使同一薄 TU
注入 exact composition/provider context，并通过 identity-only recorder 产生 owning registration snapshot；该扩展不改变本 ADR
对 final target、activation order 或 artifact receipt 的所有权。
[Windows Development Host Template v1](adr-windows-development-host-template-v1.md) 已为 #290 消费该 generation，创建第一个固定
final Host target，并执行受控构建与 registration-only verification；这些职责仍不回流到本 generator。

[Static Factory Callback Table v1](adr-static-factory-callback-table-v1.md) 曾为 #291 实现 renderer revision 3：保持同一个薄 TU 与
exact provider calls，只把 provider contract 硬切到 v2，使一次 registration 同时形成 frozen callback table 与既有 identity
snapshot；registration 阶段仍不得调用 lifecycle callback。#291 generation 的 schema、generator 与 deep verifier 只接受
revision 3 + provider API v2；旧 revision/provider 不保留兼容路径。

[Static Typed Contribution Contract Bindings v1](adr-static-typed-contribution-contract-bindings-v1.md) 又为 #294 将 active renderer
硬切到 revision 4/provider v3：同一个薄 TU 额外生成 exact selected factory/contribution ID/kind expectations 与 Capacity/Context v2，
但不 include contribution contract type、不增加 per-contribution TU。revision 3/provider v2 现在同样只属于历史记录。

[Static Contribution Payload Accessors v1](adr-static-contribution-payload-accessors-v1.md) 已由 #295 实现 revision 5/provider v4 的
后继硬切目标。generated source 只把 provider compile-time check 更新为 `StaticFactoryProviderV4`，仍不 include contract type、生成
accessor 或增加 TU；typed accessor 由 provider 的 `StaticContributionBindingV2` 自身携带。

## 问题

当前控制面已经分别证明：

- `SourceBuildPlan` 选择了哪些 exact CMake target roots 与 closure；
- `HostActivationBlueprint` 选择了哪些 logical factories 及其 activation order；
- `StaticFactoryProviderBindingPlan` 将每个 selected factory 对证到一个 selected static target、public header 与
  qualified C++ provider function。

但这些合同尚未生成真正参与编译的 source。若直接把下一步塞进现有 app CMakeLists 或 Host Runtime，会产生几类错误所有权：

- generator 自己发明最终 executable target；
- package-specific header/function 被硬编码进 app；
- CMake configure 时重新解释 Project Lock 或 Host Profile；
- 静态 constructor/global registry 代替显式 provider call；
- registration order 被误当成 factory activation order；
- 每次生成都重写大量 source，放大增量构建成本。

还存在一个时序事实：`SourceBuildPlan` 需要已配置 CMake graph 的 File API codemodel，而 exact composition root 又必须在最终
Host target 编译前存在。因此 v1 不能假装一次 configure 同时完成 discovery、plan、generation 与 final target composition。

## 外部依据

| 资料 | 官方行为 | 对 Asharia 的约束 |
| --- | --- | --- |
| [CMake 3.28 `target_sources`](https://cmake.org/cmake/help/v3.28/command/target_sources.html) | source 可以显式以 `PRIVATE` 方式加入既有 target | generated root 应附着到 Host owner 创建的 target，不自己猜测 target |
| [CMake 3.28 `target_link_libraries`](https://cmake.org/cmake/help/v3.28/command/target_link_libraries.html) | target dependency 同时承载链接项与 usage requirements | root 应链接 verified provider targets，不猜测 `.lib`/`.a` 文件路径 |
| [CMake 3.28 `add_custom_command`](https://cmake.org/cmake/help/v3.28/command/add_custom_command.html) | generated output 必须有明确 owner；同一 output 不能被多个并行 target 独立生成；`VERBATIM` 用于稳定参数传递 | v1 采用控制面先生成、最终 configure 再消费，避免隐藏的多 owner build-time generation |
| [O3DE Gem Module System](https://www.docs.o3de.org/docs/user-guide/programming/gems/module-system/) | monolithic/static composition 可以保留显式 module entry points | 静态链接不要求 global constructor 或 DLL symbol discovery |

这些资料只证明 generated source 和显式 target dependency 的构建机制；Asharia 的 plan、fingerprint、Host target owner 与
lifecycle 仍由本地合同决定。

## 决策

### 1. 输入权威

public generator 接受：

```text
generateStaticCompositionRoot(
    verified SourceBuildPlan,
    verified HostActivationBlueprint,
    verified StaticFactoryProviderBindingPlan,
    ContractValidators
) -> complete Generation | stable diagnostics
```

generator 必须：

- 复验三份 plan 的 canonical self-integrity；
- 复验 Binding Plan 中保存的 Source Build Plan 与 Blueprint fingerprints；
- 复验 Host kind、target platform、Engine Generation 与 configuration/toolchain facts；
- 只消费 Binding Plan 已选择的 provider targets/headers/functions；
- 不读取 filesystem、Project Manifest、Project Lock、Host Profile 或 package source tree；
- 不调用 resolver、Conan、CMake、compiler 或 linker。

Binding Plan 是 provider 选择权威；Blueprint 是 activation order 权威；Source Build Plan 是 CMake target/configuration
权威。generator 不建立第四套选择逻辑。

### 2. 两阶段 CMake handoff

v1 固定以下流程：

```text
Conan bootstrap
    ↓
Preflight CMake configure（package/source target graph）
    ↓
CMake File API codemodel → Source Build Plan
    ↓
Activation Blueprint + Provider Binding Plan
    ↓
Generate content-addressed static composition tree
    ↓
Generate immutable Windows Development Host template
    ↓
Final CMake configure（Host template + generated attach fragment）
    ↓
File API exact-target binding → single-target build
    ↓
Restricted registration verification
```

这是同一个 build generation 的两个明确 configure 阶段，不是两套 package graph。第二阶段必须复用相同 Conan toolchain、
platform、configuration 与 package inputs；任何 fingerprint 漂移都回到 plan/generate，而不是在 CMake fragment 中静默修正。

v1 不把 generator 隐藏在 `add_custom_command()` 中。Binding Plan 只在 preflight configure/codemodel 后可用；显式控制面步骤更容易
做原子发布、缓存、诊断和 Editor/CLI/CI parity。未来若引入 build-time adapter，仍必须保留同一 output owner 与 fingerprints。

### 3. 生成树与 manifest

每个 Host composition 生成一个 content-addressed tree：

```text
generated/static-composition/<generation-id>/
├─ asharia.static-composition-root.json
├─ include/asharia/generated/static_composition_root.hpp
├─ src/static_composition_root.cpp
└─ asharia-static-composition.cmake
```

`generation-id` 从 canonical input fingerprints、Host/build facts、provider API、provider calls 与固定输出路径计算；格式为
`sha256-<64 lowercase hex>`。它不包含绝对目录、timestamp、process ID 或 filesystem enumeration order。

manifest 至少记录：

- schema/version、renderer revision 与 generation ID；
- Source Build Plan、Blueprint、Provider Binding Plan integrity；
- Engine Generation、Host kind、platform、configuration 与 toolchain evidence；
- provider API 与 canonical provider/factory mappings；
- header/source/CMake fragment 的 package-neutral relative path、size、role 与 SHA-256；
- manifest 自身 canonical integrity。

manifest 证明 generated source bytes，不证明 compiler output、observed registration snapshot 或 final executable artifact。后者已由
[Host Executable Binding Receipt v1](adr-host-executable-binding-receipt-v1.md) 独立绑定。

#289 曾将 renderer revision 提升为 `2`。#291 随后把当时的 provider API 硬切到 v2，并把 renderer 提升为 `3`；该 generation
的 schema、generator、build 与 deep verifier 只接受 revision 3/provider v2，不保留 revision 1/2 compatibility。
#294 的 revision 4/provider v3 generation 同样已成为历史；当前 #295 revision 5/provider v4 generation 由本文开头记录的后继决策定义。

发布采用 staging directory + write exact bytes + re-read/hash + atomic directory commit。若同 generation 的完整 bytes 已存在，
返回 reuse；若目录存在但内容不一致，fail closed，不原地修补。

### 4. Host Runtime provider contract 与 identity recorder

为了让 provider headers 与 generated root 使用同一 C++ 类型，#287/#289 最初建立 identity-only provider v1。#291 又把当时的
surface 硬切为 descriptor registration：

```cpp
namespace asharia::host_runtime {

class StaticFactoryRegistrar final {
public:
  void registerFactory(
      std::string_view localFactoryId,
      StaticFactoryCallbacksV1 callbacks) noexcept;
  // construction/copy/move are not provider-owned
};

using StaticFactoryProviderV2 =
    void (*)(StaticFactoryRegistrar& registrar) noexcept;

} // namespace asharia::host_runtime
```

该窄 header 由 `asharia::host_runtime_contract` 公开；provider implementation 通过 PRIVATE
`asharia::host_runtime_provider_bridge` 构造/消费 opaque token。provider 只能登记 module-local factory ID 与完整 descriptor；完整
package/version/module/entry point 与 expected factory IDs 仍由 generated root 注入。concrete recorder、frozen table、owning snapshot
和 failure state 由 `asharia::host_runtime_registration` 实现。registration 不调用 callback，也不创建 instance、scope 或 lifecycle。

provider implementation 依赖该 contract 是允许的向内依赖；`engine/core`、`engine/platform` 与 `package-runtime` 不反向依赖
Host Runtime。

### 5. Generated C++ 形状

renderer revision 3 的 generated header 声明两个稳定的 target-private entry points：

```cpp
namespace asharia::generated {

[[nodiscard]] host_runtime::StaticFactoryRegistrationCapacityV1
staticFactoryRegistrationCapacity() noexcept;

void recordStaticFactoryProviders(
    host_runtime::StaticFactoryRegistrationRecorder& recorder) noexcept;

} // namespace asharia::generated
```

generated source：

1. 去重并按 UTF-8 bytes 排序 public provider headers；
2. 对每个 provider 生成 function-pointer type check；
3. 生成 provider/factory/text 的 exact capacity；
4. 注入 generation ID、Blueprint SHA-256、package/version/module/entry point 与 expected factory IDs；
5. 在一个显式函数内通过 recorder 按 canonical provider order 直接调用每个 provider 一次。

概念形状：

```cpp
#include <array>
#include <span>
#include <string_view>
#include <type_traits>

#include "asharia/example/static_factory_provider.hpp"
#include "asharia/generated/static_composition_root.hpp"
#include "asharia/host_runtime/static_factory_provider.hpp"

static_assert(std::is_same_v<
              decltype(&asharia::example::provideStaticFactories),
              asharia::host_runtime::StaticFactoryProviderV2>);

asharia::host_runtime::StaticFactoryRegistrationCapacityV1
asharia::generated::staticFactoryRegistrationCapacity() noexcept {
  return kRendererComputedCapacity;
}

void asharia::generated::recordStaticFactoryProviders(
    host_runtime::StaticFactoryRegistrationRecorder& recorder) noexcept {
  recorder.beginComposition({
      .generationId = "sha256-<composition-generation>",
      .hostActivationBlueprintSha256 = "<host-activation-blueprint-sha256>",
      .capacity = staticFactoryRegistrationCapacity(),
  });
  constexpr std::array<std::string_view, 1> expected{"runtime"};
  recorder.invokeProvider(
      {.packageId = "com.asharia.example", .packageVersion = "1.0.0",
       .moduleId = "runtime", .entryPoint = "asharia::example::provideStaticFactories",
       .expectedFactoryIds = expected},
      &asharia::example::provideStaticFactories);
  recorder.endComposition();
}
```

renderer template 遵循仓库 clang-format 风格，生成 fixture 仍必须经过双编译器编译。qualified function 只来自 #286 的
受限 token，不作为任意 C++ expression 拼接。

provider call order 只保证 deterministic observation。provider 内 `registerFactory()` 顺序也不会成为 snapshot 或 activation
语义。factory create/activate order 仍来自 Blueprint；Host Runtime 不得用 recording order 替代 factory DAG。

### 6. CMake attachment contract

生成的 `asharia-static-composition.cmake` 定义一个 generator-owned function：

```text
asharia_attach_static_composition(<existing-host-target>)
```

它必须：

- 要求 target 已存在且 v1 类型为 `EXECUTABLE`；
- 通过 target property 拒绝同一 target 重复 attachment；
- 复验每个 provider target 存在且为 `STATIC_LIBRARY`；
- 使用 absolute generated source path 执行 `target_sources(host PRIVATE ...)`；
- PRIVATE 添加 generated include root；
- `target_link_libraries(host PRIVATE asharia::host_runtime_registration <exact-provider-targets...>)`；registration target 再公开依赖窄 `asharia::host_runtime_contract`；
- 把 generation ID/fingerprint 写入 target property，供 final configure diagnostics 使用；
- 不调用 `add_subdirectory()`、`find_package()`、resolver、shell 或 package-specific script。

最终 Host target name、`main()`、application template、subsystem flags 和 runtime output layout 由
[Windows Development Host Template v1](adr-windows-development-host-template-v1.md) 提供；未来通用 Build Profile/platform adapter
再拥有 icon/bundle metadata 与 stage destination。generated root 不拥有这些职责。

### 7. 确定性、失败与原子性

规范化顺序：

- unique includes：header UTF-8 bytes；
- provider calls：Binding Plan canonical provider key；
- provider targets：target name/type UTF-8 bytes；
- manifest files：relative path UTF-8 bytes。

以下情况不产出部分 generation：

- 任一 input 类型、schema、self-integrity 或 cross-fingerprint 不匹配，包括 Binding Plan 与 Blueprint 的 Effective Session integrity；
- Host/build facts 不一致；
- provider API 未知；
- provider target 不在 exact Source Build Plan roots/closure；
- header/function 不再满足受限合同；
- duplicate provider call/factory mapping；
- 输出路径非规范化、绝对、逃逸 root 或与固定 layout 不符；
- publication root 或 generation tree 穿过 symlink/junction/reparse point；
- staging write/re-read/hash/atomic commit 失败；
- 已存在 generation directory 与 expected bytes 不同。

diagnostics 稳定排序；planner/render failure 不写 filesystem，publisher failure 不暴露 committed partial tree。

### 8. 编译效率边界

v1 明确限制增量成本：

- 每个 Host composition 只有一个很薄的 generated `.cpp`，不按 package/factory 生成额外 TU；
- include headers 去重，root 不包含 package implementation headers 或 `src/`；
- generation ID 同时绑定 authoritative inputs、provider mappings、固定 layout 与 renderer revision；任何会改变生成 bytes 的模板修改必须提升 renderer revision；
- content-addressed + write-if-changed，完全相同输入不更新时间戳；
- graph/provider 变化只重编该薄 TU，并触发必要 final Host relink；
- 不启用 unity/PCH，不修改 module scanning、clang-tidy jobs 或全局 build policy；
- generator 自身保持 CPU-only，开发期优先跑 focused Python/render tests，提交前仍跑双编译器 gate。

这不承诺大型项目的最终链接速度；link cache、incremental linker、exact-build DLL 或 project contribution library 是有数据后再做的
独立优化。

## 拒绝的方案

### Generator 创建最终 executable target

拒绝。final target、`main()`、platform metadata 与 stage policy 属于 Host Template/Build Profile owner；#290 已以独立 adapter
实现第一个固定 Windows Development 模板。把它们塞进本 generator 会重新混合 provider source generation 与产品构建策略。

### 每个 package 生成一个 composition TU

拒绝。provider static libraries 已经是增量编译边界；额外 TU 只扩大扫描、clang-tidy 和对象管理成本。

### 用 global registry 或 static constructor 自动发现

拒绝。它绕过 verified provider list，并让 link/load/initialization timing 变成隐藏依赖。

### 运行时按字符串查找 function

拒绝。v1 是同一 exact build generation 的静态组合；直接 C++ 引用可以同时得到 compile-time signature 和 link-time presence 证明。

### 一次 CMake configure 完成全部流程

拒绝。Source Build Plan 的真实 target/type/closure evidence 来自 configured File API codemodel；final composition 又依赖该 plan。
隐藏这个阶段边界会形成不透明循环或迫使 planner 猜测 CMake graph。

## 不做事项

- 不实现 factory callbacks、instances、scopes、leases、rollback 或 shutdown；#289 的 concrete registrar 只记录 identity；
- 不生成 `main()`、Build Profile、Host Template 或 final executable target；
- 不验证 final object/library/executable bytes；
- 不生成 post-build Host Executable Binding Receipt；
- 不实现 dynamic plugin、hot reload/unload 或 stable cross-generation ABI；
- 不运行 Conan/CMake/compiler/linker；
- 不修改 Project Lock、Host Profile、Factory Declaration、Source Build Plan、Blueprint 或 Provider Binding 合同的所有权；
- 不实现 Bootstrap UI、ImGui/Avalonia、cook、stage、package 或 deploy。

## 验证要求

- canonical generation manifest/schema/model 与 self-integrity；
- byte-identical header/source/CMake rendering under reordered equivalent inputs；
- stale/tampered/cross-plan mismatch（包括 Effective Session）negative tests；
- output path traversal、link/reparse root、partial publication、existing-directory conflict 与 reuse tests；
- synthetic valid Host target 使用生成 fragment 成功 configure/compile/link；
- renderer revision 3 synthetic Host 运行 generated recorder，得到 frozen callback table 与 exact owning registration snapshot；
- dual Host fixture 使用五个 abort-probe callbacks，C++ registration tests 使用 phase counters，共同证明 registration/receipt 路径
  对 lifecycle callbacks 的调用次数为零；
- wrong provider signature 在 `static_assert` 处编译失败；
- missing/wrong provider target 与 duplicate attachment 在 final configure fail closed；
- ClangCL 与 MSVC 都执行 generated-root compile evidence；
- full Python/contracts/topology/encoding/docs/diff/Vulkan 与双编译器 gates。

## 后续边界

1. [Static Factory Registration v1](adr-static-factory-registration-v1.md) 记录 #289 的 historical single-argument/identity-only
   registrar contract；#291 当时保留 recorder/registrar，但将其硬切为 descriptor registration 并产出 callback table；
2. [Windows Development Host Template v1](adr-windows-development-host-template-v1.md)：#290 已实现固定 final target、`main()`、
   console/runtime layout、受控 final configure/build、File API target binding 与 registration-only verification；
3. [Host Executable Binding Receipt v1](adr-host-executable-binding-receipt-v1.md)：#288 已对证 generation manifest、
   same-index CMake target/compiler、registration snapshot 与 exact staged final Host artifact；
4. [Static Factory Callback Table v1](adr-static-factory-callback-table-v1.md)：#291 用 renderer revision 3 绑定完整 typed callbacks，
   但不执行 lifecycle；
5. Activation Eligibility 与 concrete Host Runtime lifecycle：授权 callback access，并实现 scope tree、activation、rollback 与 shutdown；
6. Bootstrap/Session adapter：把 generation/build/receipt 状态映射为 Ready、PendingBuild、PendingRestart 或 SafeMode。
