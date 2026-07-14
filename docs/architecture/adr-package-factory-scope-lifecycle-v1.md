# ADR：Package Factory / Scope / Lifecycle Declaration v1

## 状态

Accepted and implemented for #284。本文冻结可安装 package 的逻辑 factory、owner scope、required factory 与 contribution
归属合同；`package-factories-v1.schema.json`、author binding validator、Candidate Discovery snapshot、Locked Verification
与对应测试已经实现。

本文是 [Host Composition Plan v1](adr-host-composition-plan-v1.md) 和未来 Activation Plan 之间的作者输入边界。
它不创建 scope、system instance、registry、lease 或可执行 activation order。

## 问题

Host Composition Plan 已经能确定某个 Host 选择了哪些 exact packages、logical modules 与 contributions，但 logical module
依赖不能回答以下运行问题：

- 哪个 factory 创建真正的 system instance；
- instance 由 Process、Project、Editor、Tool Job、Game Session、World、Local User、Document 或 Preview 中哪个 scope 拥有；
- 一个 factory 在创建前必须取得哪些其他 factory instance；
- manifest 中声明的 contribution 由哪个 factory 的 activation lease 拥有；
- create/activate 失败或正常 shutdown 时由谁决定回滚顺序。

如果直接从 module DAG、`entryModules`、CMake target 或 DLL 文件名推断这些信息，package selection、build composition 与
runtime lifetime 会重新耦合，而且同一 loading phase 内仍无法得到可靠顺序。

## 资料验证

| 资料 | 官方行为 | 对 Asharia 的约束 |
| --- | --- | --- |
| [O3DE Gem Module System](https://docs.o3de.org/docs/user-guide/programming/gems/overview/) | module 具有已知 initialize/create/destroy/uninitialize 入口；monolithic build 可把 module 静态组合 | package factory 必须有稳定逻辑 identity，但不能被强制等同于 DLL 或通用 ABI 符号 |
| [O3DE System Components](https://docs.o3de.org/docs/user-guide/programming/components/system-components/) | system component 随 module activate/deactivate；required services 约束依赖先后 | runtime factory dependency 是独立于 package/module DAG 的图 |
| [Unreal Engine Modules](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules) | loading phase 只定义粗粒度时机，同一 phase 内 module 顺序不确定 | 不采用作者自定义 phase 作为确定性 activation order |

这些资料支持“显式 entry/factory + 固定 lifecycle + 显式 dependency”的方向，但不要求 Asharia 复制其 ABI 或插件机制。
Asharia v1 仍以构建期原生静态组合和启动期注册为基线。

## 决策

### 1. 独立作者 sidecar

可安装 package 可在 payload root 提供：

```text
asharia.package.factories.json
```

其 discriminator 为 `com.asharia.package-factories`，schema version 为 `1`。它与 exact
`asharia.package.json` 的 package ID、version 和全部 logical modules 绑定。

该 sidecar 独立于：

- installable manifest：继续只描述可移植 package/module/contribution 意图；
- `asharia.package.build.json`：只描述 source boundaries 与 CMake build roots；
- `asharia.package.products.json`：只描述 logical products；
- Artifact Manifest：只证明某次 generation 的 exact files/bytes；
- generated composition root：未来把 logical factory ID 绑定到已链接 C++ 实现。

因此本合同禁止 CMake target、文件路径、DLL 名、symbol、callback 名、C++ type、ABI、command 或 artifact 字段。

### 2. 合同形状

```json
{
  "schema": "com.asharia.package-factories",
  "schemaVersion": 1,
  "package": {
    "id": "com.asharia.system.example",
    "version": "1.0.0"
  },
  "lifecycleModel": "create-activate-quiesce-deactivate-destroy-v1",
  "modules": [
    {
      "moduleId": "contract",
      "activation": {
        "kind": "no-factories"
      }
    },
    {
      "moduleId": "implementation",
      "activation": {
        "kind": "factory-set",
        "factories": [
          {
            "id": "example-service",
            "scope": "project",
            "requires": [
              {
                "packageId": "com.asharia.system.other",
                "factoryId": "other-service"
              }
            ],
            "contributions": [
              "com.asharia.contribution.example-service"
            ]
          }
        ]
      }
    }
  ]
}
```

规则：

- 每个 author-manifest module 必须恰有一个 module record；没有实例的 module 显式写 `no-factories`；
- factory ID 在 package 内唯一；跨 package reference 使用 `(packageId, factoryId)`；
- `requires` 只有 required dependency，不支持 optional/soft dependency；
- external requirement 只能指向本 package 或 author manifest 的直接 package dependency，禁止隐藏 transitive coupling；
- 每个 manifest contribution 必须被一个且仅一个 factory claim，并且 factory 与 contribution 属于同一 module；
- factory 可以不发布 contribution；`entryModules` 仍是 Host 逻辑入口标签，不等于 factory root 或 lifecycle order。

跨 package factory 是否存在、跨 package cycle 与最终 Host selection 完整性，需要未来 Activation Plan 在 exact selected graph 上验证；
单 package author validator 不伪造这些全图事实。

### 3. Scope 是 lifetime owner

v1 只允许以下 scope identity：

| 声明值 | owner |
| --- | --- |
| `process` | `ProcessScope` |
| `project` | `ProjectScope` |
| `editor` | `EditorScope` |
| `tool-job` | `ToolJobScope` |
| `game-session` | `GameSessionScope` |
| `world` | `WorldScope` |
| `local-user` | `LocalUserScope` |
| `editor-document` | `EditorDocumentScope` |
| `preview` | `PreviewScope` |

每个 factory 在每个 owner scope instance/generation 中最多产生一个 instance。v1 不提供 `singleton`、`transient` 或任意
custom lifetime；这些词会隐藏真实 owner。

本地 factory 只能依赖相同 scope 或祖先 scope：

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

例如 `editor` factory 可以依赖 `project` factory；`project` factory 不能反向依赖 `editor` factory；`tool-job` 不能依赖
`project`。该规则保证 children-first shutdown 可证明，不把 child instance 泄漏给祖先。

固定 Editor Image / Bootstrap Services 不由项目 package graph 替换。本合同没有增加用户可覆盖的 `bootstrap` scope；
Bootstrap owner 继续由 Editor Image 架构决定。

### 4. Lifecycle 与失败语义由 Host 固定

`lifecycleModel` 只能是：

```text
create -> activate -> quiesce -> deactivate -> destroy
```

语义冻结为：

1. future planner 先验证所有 selected factory reference、scope 与 dependency DAG；
2. Host 按 dependencies-first 顺序调用 `create`，factory context 只提供已声明 dependencies 与窄 capability；
3. `activate` 成功后产生 Host-owned activation lease，lease 追踪 contributions、jobs、subscriptions 与 diagnostics；
4. create/activate 失败时，Host 对已成功 factory 执行 reverse dependency order rollback；失败 factory 不被视为 activated；
5. 正常停止先关闭新工作并 `quiesce`，随后 reverse order 撤销 contributions、`deactivate`，最后 `destroy` instance；
6. 所有 v1 lifecycle transition 由 owning-scope control executor 串行调度；package 不能声明任意 phase、线程亲和或
   “失败后继续”策略。

本文只冻结 future Host 必须遵守的行为；本 Slice 没有实现这些 callback、executor、lease 或 rollback。

### 5. 确定性顺序不是作者数组顺序

作者数组顺序没有执行语义。规范化按 UTF-8 bytes 排序：

- modules：`moduleId`；
- factories：factory ID；
- requirements：`packageId`，再 `factoryId`；
- contributions：contribution ID。

未来 Activation Plan 对 ready factories 使用 dependency-first topological order；多个同时 ready 的 factory 再按 canonical
factory reference 排序。不能使用 JSON 顺序、filesystem enumeration、CMake target 顺序或 registration timing 破坏重现性。

### 6. 证据链

Candidate Discovery 把 sidecar 当作 optional author contract：

```text
read exact bytes
  -> UTF-8 JSON/schema/semantic/author binding
  -> payload-tree integrity scan
  -> re-read and reject TOCTOU
  -> snapshot parsed data + exact bytes + SHA-256 integrity
```

Locked Verification 要求三件套（parsed data、exact bytes、integrity）全部存在或全部缺席，并重新验证：

- bytes 与 integrity；
- bytes 与 parsed data；
- package ID/version/module/contribution binding；
- payload 中 sidecar 的 presence 和 exact bytes。

Effective Session 深拷贝 verified candidate，并把 factory declaration data/bytes integrity 纳入 candidate binding fingerprint。
它只保存经过验证的后继输入，不生成 Activation Plan。

sidecar 在 Discovery 层保持 optional，是为了允许 Feature Set、数据包和迁移期 package 继续被解析。未来 Activation Planner
必须对当前 Host 已选择且需要原生 activation 的 modules 明确要求 declaration；该条件不提前塞进 resolver。

## 失败合同

实现提供稳定诊断，至少覆盖：

- schema/未知字段与不支持的 lifecycle model；
- package ID/version mismatch；
- missing/unknown/duplicate module；
- duplicate factory、self/unknown local dependency、local cycle 与 scope direction；
- undeclared external package dependency；
- unknown、wrong-owner、duplicate 或 unclaimed contribution；
- Candidate Discovery unreadable/invalid/TOCTOU；
- Locked Verification incomplete/integrity/data/disk mismatch。

验证为原子结果：任何 candidate declaration 失败时不返回部分 candidate batch；任何 selected candidate 复验失败时不返回可复用
locked graph。

## 不做事项

- 不生成或执行 Activation Plan；
- 不实现 Host Runtime、scope object、factory context、activation lease、registry 或 rollback；
- 不选择 artifact，不生成 CMake target 或 composition root；
- 不实现 DLL loading、hot reload/unload、stable C++ ABI 或跨 generation 通用插件；
- 不增加 optional dependencies、custom phases、custom lifetime、thread affinity 或 service locator；
- 不实现 Editor Package Manager、ImGui 或 Avalonia UI。

## 后继边界

1. Activation Plan：组合 Ready Effective Session、Host Composition、verified artifact generation 与本 factory declaration，完成
   全图 reference/scope/cycle 校验并生成 canonical executable order；
2. generated static composition root：把 plan 中 logical factory reference 绑定到 exact-build C++ registration；
3. Host Runtime：实现 scope tree、factory context、lease、typed registries、rollback 与 shutdown；
4. Editor Bootstrap：只消费上述 headless result 显示 Ready/PendingBuild/PendingRestart/SafeMode，不反向拥有 package semantics。

## 相关资料

- [Foundation Framework](foundation-framework.md)
- [Package-first](package-first.md)
- [Host Composition Plan v1](adr-host-composition-plan-v1.md)
- [Effective Session v1](adr-effective-session-v1.md)
- [Package Product & Artifact Evidence v1](adr-package-product-artifact-evidence-v1.md)
