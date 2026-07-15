# ADR：Host Activation Blueprint v1

## 状态

Accepted and implemented for #285。本文冻结构建前 Host factory/scope template 蓝图、全图验证、确定性顺序和
evidence contract。`host-activation-blueprint-v1.schema.json`、pure planner 与 synthetic handoff tests 已实现。

Blueprint 不创建任何 system instance，也不是已经绑定 executable bytes 的最终 Activation Plan。

## 问题

[Package Factory / Scope / Lifecycle Declaration v1](adr-package-factory-scope-lifecycle-v1.md) 已经让 package 作者声明：

- logical factory；
- lifetime-owner scope；
- required factories；
- contribution owner；
- 固定 lifecycle model。

但单 package validator 无法回答当前 Host 的完整问题：

- 外部 factory reference 是否落到 exact selected graph 中；
- target factory 所在 module 是否被 Host Profile 选中；
- 跨 package requirement 是否形成 cycle 或错误 scope direction；
- Host 已选择的 contribution 最终由哪个 selected factory 拥有；
- World、Document、Tool Job 等按需 scope 应怎样复用同一套 factory order。

此前文档又把 `verified artifacts`、Activation Plan 和 generated static composition root 写得过近。静态 composition root
必须在 C++ target 编译/链接前生成；最终 host/artifact bytes 只能在链接后验证。如果一个“最终 Activation Plan”既依赖
这些 bytes，又负责生成 composition root，就会形成：

```text
Activation Plan -> composition root -> build/link -> artifact bytes -> Activation Plan
```

## 外部依据

| 资料 | 官方行为 | 对 Asharia 的约束 |
| --- | --- | --- |
| [CMake `add_custom_command`](https://cmake.org/cmake/help/latest/command/add_custom_command.html) | generated source 先由 custom command 产生，再作为 target source 编译 | factory closure 的构建输入必须早于最终 executable artifact evidence |
| [O3DE Gem Module System](https://docs.o3de.org/docs/user-guide/programming/gems/overview/) | monolithic build 可把 modules 静态链接进 executable，同时保留已知 module entry points | logical factory identity 和静态组合可以共存，不要求全部做成 DLL |
| [O3DE component lifecycle](https://docs.o3de.org/docs/user-guide/programming/components/overview/) | dependency 决定 activation 顺序；deactivation 使用逆序 | Host 必须从显式 factory DAG 派生顺序，不能依赖注册时机 |
| [Unreal Engine Modules](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules) | 同一 loading phase 内 module order 不确定 | phase、link order 或 static initialization order 不能替代依赖图 |

这些资料支持“两段绑定 + 显式依赖”的方向，但不要求 Asharia 复制其 ABI、module manager 或 lifecycle API。

## 决策

### 1. 将笼统 Activation Plan 拆成构建前蓝图与构建后绑定

v1 pipeline 固定为：

```text
Ready Effective Session
+ matching Host Composition Plan
+ verified Package Factory Declarations
                ↓
Host Activation Blueprint（本 ADR）
                ↓
verified Static Factory Provider Binding Plan
                ↓
generated static composition root
                ↓
compile / link
                ↓
identity-only static registration recording
                ↓
owning registration snapshot + artifact verification
                ↓
Host Executable Binding Receipt
                ↓
Host Runtime lifecycle
```

`Host Activation Blueprint` 只证明逻辑 factory closure、scope templates、requirements、selected contributions 和输入
fingerprints。它是 build-time derived state，可以规范化呈现用于 diagnostics/cache/test，但不是新的 lockfile。

[Host Executable Binding Receipt v1](adr-host-executable-binding-receipt-v1.md) 已把 Blueprint fingerprint 对证到：

- [Static Factory Registration v1](adr-static-factory-registration-v1.md) 输出的 exact owning registration snapshot；
- exact staged Host executable artifact；
- Engine generation、platform、configuration 和 toolchain；

当前进程实际加载的 native generation 仍属于后继 Session/Bootstrap state，不由 Blueprint 或 registration snapshot 推断。

因此本 Blueprint 不读取 Package Artifact Manifest，也不输出 `PendingBuild` / `PendingRestart`。

### 2. 输入必须来自同一 Ready 会话

public planner 为：

```text
planHostActivationBlueprint(
    Ready EffectiveSessionPlan,
    matching HostCompositionPlan,
    ContractValidators
) -> complete Blueprint | stable diagnostics
```

planner：

- 复验 Ready Effective Session 的 Distribution/Project/Lock/Profile/candidate fingerprints；
- 从该 session 重新派生 canonical Host Composition 并与调用方 handoff 做 byte comparison；
- 只读取 Effective Session 已深拷贝的 verified candidate snapshots；
- 不调用 resolver，不读取 filesystem，不执行 build，不写文件。

Host Composition 与 session 不匹配时 fail closed；不能把两个合法但来自不同输入 generation 的 plan 拼接起来。

### 3. selected installable package 必须显式声明零或多个 factories

Candidate Discovery 继续允许 optional `asharia.package.factories.json`，因为 discovery 还要识别 Feature Set、迁移 payload
和非激活用途。但 Blueprint 使用 hard cut：

> 每个 Host Composition 中的 selected installable-capability package 都必须带 verified Factory Declaration。

没有 instance 的 module 必须显式写 `no-factories`。Planner 不把“sidecar 缺失”猜成“没有 factory”，也不保留 v0 fallback。

每个 snapshot 必须同时具备 parsed data、exact bytes 和 SHA-256 integrity；planner 在内存中复验 bytes/integrity/data 和
author-manifest binding。`factoryDeclarationSetIntegrity` 对按 exact package ID/version 排序的声明 evidence 计算。

### 4. Host Composition 选择 modules，Blueprint 选择这些 modules 的全部 factories

选择规则固定为：

- Feature Set 只保留 resolved graph evidence，没有 factories；
- installable package 保留 Host Composition 已选择的 modules；
- selected module 的 `factory-set` 中全部 factories 进入 Blueprint；
- unselected module 的 factories 不进入 Blueprint；
- `entryModules` 仍只是逻辑入口标签，不成为 factory root 或 phase。

external requirement 必须解析为当前 exact selected graph 中唯一 factory。即使 target package 在 lock 中，如果 target module
没有被当前 Host 选择，该 factory 仍视为 unavailable。

### 5. Scope template 不是 concrete scope instance

Blueprint 输出固定 `scopeTemplates`，而不是一次平铺的 startup list。每个 template 表示：

> 未来 Host Runtime 每创建一个该 scope 的 concrete instance/generation，就按此 template 创建该 scope 自己的 factories。

例如 `WorldScope`、`LocalUserScope`、`EditorDocumentScope`、`PreviewScope` 和 `ToolJobScope` 可以在进程运行期间创建多次。
Blueprint 不包含 World ID、document path、user index、job ID 或 instance generation。

固定拓扑继续为：

```text
process
├─ project
│  ├─ editor
│  │  ├─ editor-document
│  │  └─ preview
│  └─ game-session
│     ├─ world
│     └─ local-user
└─ tool-job
```

Host kind 可用 scope policy 为：

| Host kind | scope templates |
| --- | --- |
| `minimal` | process |
| `runtime` | process, project, game-session, world, local-user |
| `dedicated-server` | process, project, game-session, world |
| `asset-worker` | process, tool-job |
| `editor` | process, project, editor, editor-document, preview, game-session, world, local-user, tool-job |

模板按固定 root-before-child vocabulary 顺序输出；未包含 factory 的允许 scope 仍以空 template 出现，使 Host policy 可对证。

### 6. 全图 requirement 与确定性顺序

Factory exact reference 为：

```text
(packageId, packageVersion, moduleId, factoryId)
```

author requirement 仍以 `(packageId, factoryId)` 表达；Planner 在 exact selected graph 中补全 version/module。规则为：

- target 必须存在且 module 已选择；
- target scope 必须是 source factory 的相同 scope 或祖先 scope；
- 全 factory graph 必须无 cycle；
- 同 scope factory 按 dependencies-first 排序；
- 多个 ready factory 按 package ID、exact version、factory ID 的 UTF-8 bytes 稳定打破平局；
- requirement arrays 与 contribution arrays 规范化排序。

ancestor-scope dependency 不会把所有 factories 变成单次启动序列。Host 创建 child scope 前必须已有 ancestor scope instance，
随后 factory context 只能取得 Blueprint 声明的 exact dependencies。

### 7. Contribution 绑定使用 Host 已筛选集合

Factory Declaration 需要 claim package 的全部 author contributions；Host Composition 按 Host Profile 选择兼容 contribution。
Blueprint 只绑定两者交集：

- 每个 Host-selected contribution 必须由一个且仅一个 selected factory claim；
- selected contribution 的 package version 与 owner module 必须和 factory exact match；
- 属于其他 Host kind、被 Profile 筛掉的 contribution 不进入 Blueprint；
- selected factory 即使当前没有 selected contribution，仍可以作为纯 service factory 激活。

这样 Runtime 不会携带 Editor panel，Asset Worker 不会携带 runtime/editor contribution，同时也不要求作者按 Host 拆成多个
installable packages。

### 8. Canonical evidence 与失败合同

Blueprint `inputs` 包含：

- `effectiveSessionIntegrity`；
- `hostCompositionIntegrity`；
- `factoryDeclarationSetIntegrity`。

Blueprint 还携带 exact `EngineGenerationId`、Host kind、target platform、fixed lifecycle model 和自身 `integrity`。self-integrity
覆盖除 `integrity` 字段外的 canonical fields。

输出禁止：

- absolute/source/build/artifact path；
- artifact hash 或 runtime binary selection；
- CMake target、command、DLL/symbol/C++ type；
- concrete scope instance ID；
- timestamp、filesystem order、registration timing；
- author-defined phase、thread affinity、optional dependency 或 failure policy。

任一输入、reference、scope、cycle、contribution、schema 或 integrity 错误都返回空 Blueprint 和稳定排序 diagnostics；不暴露
partial plan，也不修改 session、Host Composition、candidate 或 declaration snapshots。

## 合同形状

简化示例：

```json
{
  "schema": "com.asharia.host-activation-blueprint",
  "schemaVersion": 1,
  "inputs": {
    "effectiveSessionIntegrity": { "algorithm": "sha256", "digest": "..." },
    "hostCompositionIntegrity": { "algorithm": "sha256", "digest": "..." },
    "factoryDeclarationSetIntegrity": { "algorithm": "sha256", "digest": "..." }
  },
  "host": {
    "engineGenerationId": "sha256-...",
    "hostKind": "editor",
    "targetPlatform": "com.asharia.platform.windows"
  },
  "lifecycleModel": "create-activate-quiesce-deactivate-destroy-v1",
  "scopeTemplates": [
    {
      "scope": "project",
      "parentScope": "process",
      "factories": [
        {
          "reference": {
            "packageId": "com.asharia.system.example",
            "packageVersion": "1.0.0",
            "moduleId": "implementation",
            "factoryId": "example-service"
          },
          "requires": [],
          "contributions": []
        }
      ]
    }
  ],
  "integrity": { "algorithm": "sha256", "digest": "..." }
}
```

真实 canonical Editor Blueprint 还包含全部允许的空 scope templates；示例为便于阅读而省略。

## 不做事项

- 不生成 C++、CMake source/target 或 static composition root；
- 不选择或验证 package/host artifacts，不产生 binding receipt；
- 不实现 Host Runtime、scope object/instance、factory callback/context、lease、registry、rollback 或 shutdown；
- 不实现 Editor Bootstrap、Safe Mode 状态机、ImGui/Avalonia UI 或 Package Manager；
- 不实现 dynamic loading、hot reload/unload、stable C++ ABI 或 exact-build DLL；
- 不修改 resolver、Project Lock、Host Profile、Source Build Plan 或 author Factory Declaration。

## 验证

- closed schema 与 self-integrity tamper tests；
- 五种 Host scope policy；
- selected/unselected module factory projection；
- missing declaration、missing/unselected external target、external scope direction、cycle；
- contribution filtering 与 exact owner；
- same-scope dependency order、stable tie-break、candidate iterable permutation；
- stale session/Host Composition/declaration evidence、atomicity 与 input immutability；
- mock guards 证明 planner 不调用 resolver 或 filesystem；
- Candidate Discovery → Resolve/Locked Verification → Effective Session → Host Composition → Blueprint synthetic handoff。

## 后继边界

1. [Static Factory Provider Bindings v1](adr-static-factory-provider-bindings-v1.md) 已为 #286 冻结 logical factory 到 selected static target/public header/type-safe function 的 exact source evidence，并派生 verified Binding Plan handoff；
2. [Generated Static Composition Root v1](adr-generated-static-composition-root-v1.md)：#287 已消费 Source Build Plan、Blueprint 与 verified provider Binding Plan，生成薄 C++ registration source 和受控 target attachment；
3. [Static Factory Registration v1](adr-static-factory-registration-v1.md)：#289 已由 generated root 注入 generation、Blueprint、provider context 与 expected local factory IDs，并输出 identity-only canonical owning snapshot；
4. [Windows Development Host Template v1](adr-windows-development-host-template-v1.md)：#290 已实现固定 final executable、`main()`、
   受控构建、File API target binding 与 registration-only verification；
5. [Host Executable Binding Receipt v1](adr-host-executable-binding-receipt-v1.md)：#288 已在构建后把 Blueprint fingerprint、
   registration snapshot 与 exact staged Host artifact 对证；该 receipt 不证明 lifecycle activation；
6. Host Runtime lifecycle：实现 concrete scope tree、factory context、lifecycle callbacks、activation lease、typed registries、rollback 与 shutdown；
7. Bootstrap adapter：把上述 headless states 映射到 Ready/PendingBuild/PendingRestart/SafeMode UI。
