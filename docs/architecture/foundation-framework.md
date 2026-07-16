# 基础框架与可扩展系统宿主架构

状态：目标架构。本文定义后续实现顺序和硬边界，不表示计划中的 targets/API 已经存在。

当前仓库事实是：`engine/core` 已提供 error/result、log 和严格 file IO baseline；`engine/platform` 仍是只传递
`asharia::core` 的 `INTERFACE` target；root CMake 仍静态加入当前 source packages；`engine/package-runtime` 已建立 package control
plane 的 headless v1 contracts；`engine/host-runtime` 已建立 registration、eligibility 与 root ProcessScope 的 headless C++ boundary。
#295 的 [Static Contribution Payload Accessors v1](adr-static-contribution-payload-accessors-v1.md) 冻结 selected contribution 的 payload
projection authority；#296 的
[ProcessScope Contribution Registry and Activation Lease v1](adr-process-scope-contribution-registry-and-activation-lease-v1.md) 已在
ProcessScope V2 内调用 accessor、建立 fixed-slot typed registry、weak generation view/handle 与 contribution-only lease。

其他 concrete Host scopes、production Host/Bootstrap adapter、完整 system-instance/jobs/subscriptions lease、Memory & Budget、Settings、
Runtime Storage、Tasks 等目标模块尚未实现。下一步必须以真实 Host + 真实 system/contribution 完成可观察 vertical feature，而不是继续
预先增加抽象 Foundation。本文不能被用来宣称生产 Bootstrap、Editor UI 或完整 Foundation Services 已经完成。

## 目的

Asharia 当前最优先目标不是继续增加 Physics、Audio、AI 等纵向功能，而是建立一套后续完整 System/Feature
Packages 都能复用的基础框架：

- Engine Distribution 与项目 package graph 可分别验证，并派生 Host-specific composition/activation plan；
- Editor Image 的最小 Shell、diagnostics、Package Manager、Build/Repair 与 Safe Mode 不依赖项目 graph 成功激活；
- Editor、Runtime、Dedicated Server 和 Tool Host 共享相同的系统创建、停止和失败回滚语义；
- 系统通过显式作用域和 typed contributions 扩展，不使用全局 service locator；
- 配置、IO、任务、时间、内存压力和诊断有统一的底层契约；
- Editor/runtime/script/native package 权限分开；
- 新系统可以被增加、验证、禁用和移除，而不要求修改 Kernel 或某个巨型 `Engine` 类。

“基础功能完整”表示一条基础能力具备 owner、lifetime、failure、shutdown、diagnostics 和 tests 闭环，不表示
把所有默认功能编进不可卸载 Kernel。

## 总体决策

```mermaid
flowchart TB
    EditorImage["Editor Image\nbootstrap shell / diagnostics / repair / Safe Mode"]
    Apps["Other Host executables\nRuntime / Server / Tool"]
    Distribution["Engine Distribution Manifest\nfixed EngineGenerationId + bundled inventory"]
    Project["Project Manifest + Lock\nproject-owned graph"]
    Profile["Host Profile"]
    Session["Effective Session Plan\nderived state + subplan handoff"]
    Binding["Deep-verified Host binding\nexact artifact + snapshot"]
    Launch["Verified launch handoff\nplanned adapter"]
    Eligibility["Activation Eligibility\nC++ boundary implemented; launcher adapter planned"]
    Host["engine/host-runtime\nregistration + eligibility + ProcessScope V2\nfixed-slot registry + contribution lease; other scopes planned"]
    Package["engine/package-runtime\nmanifest / solve / lock / session planning"]
    Kernel["Bootstrap Kernel\ncore + minimal platform primitives"]
    Foundation["Default Foundation System Packages\nMemory / Observability / Storage / Settings / Tasks / Data"]
    Domain["Domain System Packages\nContent / World / Input / Scripting / Rendering / Physics ..."]
    Features["Feature and Integration Packages"]

    EditorImage -->|boots before project activation| Session
    Apps -->|provides fixed composition root| Session
    Distribution -->|binds engine generation| Session
    Project -->|adds project graph| Package
    Profile -->|selects modules| Package
    Package -->|derives composition| Session
    Session -->|when Ready, supplies evidence| Eligibility
    Binding -->|binds exact Host generation| Eligibility
    Launch -->|binds live process instance| Eligibility
    Eligibility -->|admits recording and exact table| Host
    Host -->|uses primitives| Kernel
    Host -->|activates| Foundation
    Host -->|activates| Domain
    Host -->|activates| Features
    Package -->|uses bootstrap IO/config| Kernel
    Domain -->|depends on public contracts| Foundation
    Features -->|depends on public system APIs| Domain
```

依赖只允许向下。Editor Image 可以静态链接 bootstrap/foundation 实现，但不能等待项目 packages 才提供基础修复 UI。
`package-runtime` 不创建 World/Renderer/Script VM；`host-runtime` 不实现任何领域系统；
Foundation Systems 不依赖 Editor、Vulkan backend 或游戏规则。

发行/项目所有权、`EngineGenerationId`、Safe Mode 与静态原生组合的完整决策见
[Editor Image、Engine Distribution 与原生组合 ADR](adr-editor-engine-distribution-and-native-composition.md)。

## 三层基础

### L0：Bootstrap Kernel

不可卸载、不可由 Package Manager 替换的最小能力：

- error/result、stable ID/hash、version 和 bootstrap diagnostics；
- 最小 allocator contract 与进程启动所需内存分配；
- monotonic clock、线程/同步原语；
- 启动所需的本地文件读取、atomic write、路径、进程和 dynamic library primitives；
- 崩溃前仍可工作的最小 stderr/file log sink 与 fatal/crash capture hook。

Kernel 不拥有 VFS、asset、job graph、CVar、World、script VM、Renderer、Editor transaction 或产品设置。
package manifest/lock 的窄格式模型和解析由静态 bootstrap component `engine/package-runtime` 拥有；不能为了在启动期使用
它们而把 package schema 反向塞入 `engine/core`。

当前 `engine/core/include/asharia/core/error.hpp` 的 `ErrorDomain` 枚举已经列出 Vulkan、Shader、RenderGraph、Asset、
Scene、Material 等上层系统。它是现状兼容 API，但不适合作为可扩展 package 生态的最终模型：每增加系统都要求修改
Kernel。Foundation 收敛时应迁移为稳定 `DiagnosticDomainId`/package+component identity 与 domain-local error code；
Core 只保存通用错误载体和上下文链，不枚举全部未来系统。迁移前保留兼容映射和测试，不能一次性破坏现有调用者。

### L1：Host Foundation

`engine/host-runtime` 已建立
`host_runtime_contract -> host_runtime_registration -> host_runtime_activation_eligibility -> host_runtime_process_scope` 的向下 target
链，并应作为固定 Host 执行骨架静态进入 Editor Image closure；它属于 L1，不进入 L0 Kernel。完整 L1 最终只拥有：

- Host role 与 scope tree；
- ordered activation/deactivation；
- system/module factory context；
- typed contribution registry 的 owner 和 lease；
- application lifecycle state；
- frame/update safe-point orchestration；
- activation failure rollback、quiesce、shutdown drain 和结构化诊断。

它不得返回任意全局 service pointer，也不得成为 World、Renderer、Storage 或 Settings 的实现所在地。

当前 #296 implementation 只覆盖 root `ProcessScope`：它按值消费 admitted callback table，在 preflight exact-map sealed Blueprint process
projection 与 contribution runtime bindings，并建立 owner-ordered fixed slots。start 按 dependencies-first `create -> activate -> publish`
执行；只有 per-factory contribution lease 原子提交后，该 factory 才成为 dependency-visible，全部 factories 成功后 public registry 才开放。
它通过窄 factory contexts 暴露声明过且已 dependency-visible 的 dependencies，并由 Host 唯一拥有 instance token 与 contribution lease。

rollback/stop 的 gate 固定为 reverse quiesce → registry `Revoking` → reverse lease revoke → reverse deactivate/destroy → registry
`Revoked`。registry view/typed handle 只 weak-reference scope generation，所有 query/borrow 都复验 control thread、process epoch、phase、
cardinality、type 与 payload。该 implementation 仍由 PRIVATE test issuer 驱动；production current-process issuer、normal Host、Bootstrap
state mapping 与其他 scopes 均未实现。完整合同见 [ProcessScope Lifecycle v1](adr-process-scope-lifecycle-v1.md)、
[Static Typed Contribution Contract Bindings v1](adr-static-typed-contribution-contract-bindings-v1.md)、
[Static Contribution Payload Accessors v1](adr-static-contribution-payload-accessors-v1.md) 与
[ProcessScope Contribution Registry and Activation Lease v1](adr-process-scope-contribution-registry-and-activation-lease-v1.md)。

### Editor Image 的固定 Bootstrap Closure

Editor Bootstrap 的定位是：即使项目未能成功加载，也能诊断、构建、修复并重新启动项目的固定控制面。它不是完整 Editor，
也不是完整 Engine。除 L0 Kernel 外，Editor Image 固定包含：

- headless `package-runtime`：读取/验证 Distribution、Project Manifest/Lock 与 Host Profile，派生 Effective Session 和后继
  handoff；不依赖 Editor UI、Data Model、VFS、Renderer，不执行 CMake 或加载项目 DLL；
- L1 `host-runtime` 骨架：只管理固定 Image components 和 scope/lifecycle/rollback；Safe Mode 不激活项目 contributions；
- 窄 Project Bootstrap Reader：只定位并读取 `asharia.project.json`、项目 ID/Engine requirement 与 package 文件位置，
  不打开 asset database、World 或项目插件。其最终 owner 由独立 Slice 在固定 `project-core` 子集与独立
  `project-bootstrap` 之间决定；不得塞入 `package-runtime`；
- 最小 platform/window/event loop、UI Shell、内嵌基础资源、bootstrap/package/build diagnostics 与 Build/Repair/Restart 控制；
- UI 图形 backend 失败时仍可工作的 OS-native fatal dialog 或 console/log 降级路径。

Editor Image 的项目打开状态词汇为：

```text
NoProject -> Opening -> Ready | PendingBuild | PendingRestart |
                         RepairRequired | UpgradeRequired | SafeMode |
                         FatalDistributionError
```

当前 Effective Session v1 只拥有足够证据产生 `Ready`、`RepairRequired`、`UpgradeRequired` 与 `SafeMode`。
`PendingBuild` 需要 artifact freshness/build output evidence，`PendingRestart` 需要 current-process generation evidence；两者属于
Editor Bootstrap 状态机，但在这些检查器落地前不得由 session composer 猜测。

World/Scene、Viewport/game Renderer、Vulkan/RenderGraph/material、asset database/import/cook、完整 VFS/job graph、scripting、
physics/audio/networking、Inspector/transaction/document workspace、package 自定义 UI、PIE 与 native hot reload 全部在 Ready 后由
完整 Editor Profile 激活，不属于固定 Bootstrap Closure。最小 UI 能查看 package 文件和错误，不表示 Project/Editor Domain
已经启动。

### L2：Default Foundation System Packages

| 完整 System Package | Runtime owner | Editor/Tool module | 必须具备的基础闭环 |
| --- | --- | --- | --- |
| Memory & Budget | domain/tag registry、budget/pressure snapshot、trim protocol | memory profiler、budget/pressure inspector | bootstrap continuity、per-owner accounting、low-memory response、OOM evidence |
| Runtime Storage & IO | mount table、VFS、bundle/archive reader、async request/completion | mount/bundle inspector、IO diagnostics | priority、cancel、shutdown drain、corrupt/missing handling、user/cache/log mounts |
| Settings & Console | immutable effective settings、CVar/command registry、Device Profile selection | settings document、profile editor、transaction、preview | layered merge、hot/restart policy、shipping restrictions、deterministic snapshot |
| Tasks & Jobs | worker scheduler、dependency/cancel/shutdown | timeline/debug view | explicit inputs、owner cancellation、join、failure propagation、profiling |
| Data Model & Persistence | schema、archive、migration、binding | schema/migration diagnostics | version、unknown field、round-trip、corrupt input、deterministic writer |
| Observability & Validation | diagnostics router、counter/trace contracts、local crash evidence | log/profiler/automation/crash report UI | early/late sinks、bounded buffering、local crash report、test orchestration |

这些是默认/required-by-profile 的完整系统，不等于 Kernel。Minimal Host 可以只激活测试所需 contracts/implementations；
Standard Runtime 由 Feature Set 持续要求它们。

Memory & Budget 的 allocator/tag hook 必须在 package activation 前由 Kernel 提供，但 domain registry、预算策略、压力快照和
Editor 工具属于一个完整 Foundation System Package。Observability 只消费 Memory 的只读快照，不拥有 allocator、cache 或 trim。

## Host Scope 模型

作用域是 lifetime owner，不是命名空间或依赖注入容器标签。当前只有 root `ProcessScope` 的 headless lifecycle executor 已有 C++
implementation；下图其余 scope tree 仍是目标合同，不能据此宣称 Project/Editor/World 等 owner 已存在。

```mermaid
flowchart TB
    Process["ProcessScope"]
    Project["ProjectScope"]
    Editor["EditorScope"]
    Tool["ToolJobScope"]
    Session["GameSessionScope"]
    World["WorldScope"]
    User["LocalUserScope"]
    Document["EditorDocumentScope"]
    Preview["PreviewScope"]

    Process --> Project
    Process --> Tool
    Project --> Editor
    Project --> Session
    Editor --> Document
    Editor --> Preview
    Session --> World
    Session --> User
```

| Scope | 典型 owner | 示例实例 | 禁止 |
| --- | --- | --- | --- |
| `ProcessScope` | Host process | platform lifecycle、diagnostics router、package catalog snapshot | project/world mutable state |
| `ProjectScope` | opened project | effective package graph、project settings、content catalog | active game state |
| `EditorScope` | Editor host | document registry、selection service、commands/workspace | runtime shipping dependency |
| `ToolJobScope` | one import/cook/build job | job-local mounts、diagnostics、temporary outputs | active Editor/World pointer |
| `GameSessionScope` | game/application session | game flow、save/user services、network session | Editor transaction |
| `WorldScope` | one Edit/Play/Preview/runtime World | entity/component systems、spatial projection、simulation clock | process-global singleton state |
| `LocalUserScope` | one local player/user mapping | input user、accessibility/user settings、online identity binding | physical device ownership leak |
| `EditorDocumentScope` | one open source document | undo/redo、dirty、validation、source model | runtime resident/GPU ownership |

子 scope 只能依赖祖先 scope 的显式 public capabilities。销毁按 children-first、contributions-first、instance-last
执行；任何跨 scope handle 必须能检测 stale generation。

## Application Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Starting
    Starting --> Active
    Starting --> Faulted
    Active --> Suspending
    Suspending --> Suspended
    Suspended --> Resuming
    Resuming --> Active
    Active --> Stopping
    Suspended --> Stopping
    Faulted --> Stopping
    Stopping --> Stopped
    Stopped --> [*]
```

Platform adapter 产生 focus、quit、suspend、resume、low-memory、display/device change 等事实事件；Host 将它们投影为
有序 lifecycle snapshot/event。各系统在自己的 owner safe point 响应：Content 可以 eviction，Audio 可以暂停 device，
Renderer 可以停止 present，Save/User Data 可以提交有限写入。Platform 不直接调用这些系统。

Platform Support System 还发布 immutable `PlatformCapabilitiesSnapshot`，只覆盖 OS/architecture、CPU topology、memory
class/pressure、display/DPI、windowing、process 与 dynamic-library availability 等平台事实。GPU feature/limit 由 RHI device
capabilities 拥有，物理输入设备状态由 Input System 拥有；Platform 不建立跨领域的万能 capability bag。事实变化通过新
generation snapshot 在 Host safe point 发布，Settings/Device Profile 只消费所需 projection。

脚本只能收到经过 Host/Scripting scheduler 投影的高层事件，并受执行预算、capability 和取消约束；OS suspend 回调
不能等待任意脚本、网络或无限时任务完成。

## Package Plan 与系统激活

下图描述 F2 的目标执行流。verified Engine Distribution + Project Lock v2、Host Profile projection 与
[Effective Session v1](adr-effective-session-v1.md) 已实现；session 以 exact Profile bytes 和 graph fingerprints 产出
Ready/Upgrade/Repair/SafeMode，并把 verified graph 交给 [Host Composition Plan v1](adr-host-composition-plan-v1.md)。后者生成
canonical logical IR；[Host Activation Blueprint v1](adr-host-activation-blueprint-v1.md) 已在其上生成构建前 scope templates 与
factory order，但仍不是绑定 executable bytes 的可执行 plan。Effective Session 只派生状态和子计划 handoff，
不成为第三份 lock；`PendingBuild` / `PendingRestart` 尚无证据来源。

```mermaid
sequenceDiagram
    participant Image as Editor Image / Host executable
    participant Package as package-runtime
    participant Host as host-runtime
    participant Registry as typed registries
    participant System as system factory/instance

    Image->>Image: bootstrap shell/diagnostics/repair before project activation
    Image->>Package: verify Distribution + Project Lock + Host Profile
    Package-->>Image: Effective Session state + verified graph when Ready
    Note over Package,Host: Blueprint binds logical factories before build; receipt binds exact host artifacts after build
    Image->>Host: create Process/Project scopes
    Host->>System: create with explicit factory context
    System-->>Host: instance + activation lease
    Host->>Registry: publish owned contributions
    Registry-->>Host: contribution handles
    alt activation failure
        Host->>Registry: revoke handles in reverse order
        Host->>System: stop/dispose activated instances
        Host-->>Image: structured failure and SafeMode state
    else normal shutdown
        Host->>Registry: close scope to new work
        Host->>System: cancel, drain, stop, dispose
    end
```

### Factory Context

计划中的 factory context 只提供 manifest-declared dependencies 和当前 scope services，不能提供万能
`getService<T>()`。依赖在 descriptor/activation plan 中有稳定 identity，Host 在创建前验证 availability、version、scope
和 role。

### Activation Lease

每个系统/module 激活返回 owner lease，至少追踪：

- system instance lifetime；
- registered contributions；
- outstanding jobs/cancellation source；
- subscriptions/callback gates；
- diagnostics identity；
- reload/remove 时的 quiesce/restart requirement。

lease 撤销后 callback 必须被 gate，不能再把旧 generation 对象发布给新 scope。

## Typed Contribution 模型

可扩展性的主接口是 typed contribution，不是裸 callback 或任意脚本反射。

| Contribution | Registry owner | 典型 provider | 生效时机 |
| --- | --- | --- | --- |
| system factory | Host Runtime | System Package native module | scope creation |
| schema/migration | Data Model | System/Feature Package | ProjectScope activation 前半段 |
| importer/cooker | Content/Rendering tool module | System/Feature Package | ToolJobScope |
| component/system descriptor | World | gameplay/system package | WorldScope creation/safe point |
| renderer feature/pass program | Rendering | Rendering Feature Package | pipeline generation safe point |
| input device/action provider | Input | Platform/Feature Package | input update boundary |
| editor panel/command/inspector/gizmo | Editor Domain | package editor module/script | EditorScope safe point |
| diagnostics/test provider | Observability | any package | owner scope activation |

所有 registry 必须：

- 使用稳定 contribution ID、owner package/module/generation；
- 拒绝 duplicate/conflict 或按显式 priority policy 解决；
- 返回可撤销 handle；
- 提供只读 snapshot 给 Editor/diagnostics；
- 在 owner lease 撤销后不调用旧 provider；
- 不允许 provider 越过 public contract 取得 registry 内部对象。

## Editor、runtime、脚本和原生扩展

| 能力 | Editor / Tool | Runtime | 外部脚本 | 原生 Package |
| --- | --- | --- | --- | --- |
| package graph | UI、impact preview、apply/rollback | 只消费 locked graph | 不实现 resolver；可读取受限 snapshot | manifest 声明完整 package/modules/contributions |
| system lifecycle | 显示状态、safe mode、诊断 | Host 创建/停止真实实例 | 不创建底层系统；只能运行受控 entry point | system factory 创建本包实例并返回 lease |
| settings/device profile | transaction、profile authoring、preview | 选择 effective profile/snapshot | 读写公开 keys、请求 quality level | 注册 typed settings schema/provider |
| IO/storage | source/bundle tooling、mount inspector | VFS/async IO owner | sandboxed facade | 注册 protocol/archive provider，不持有 Host mount table |
| tasks/time | timeline/profiler、test controls | Tasks worker owner；Host phases；World simulation clock | 提交受 capability 限制的 jobs/timers | 声明 jobs、cancel/shutdown behavior |
| World/spatial | document、debug draw、selection/streaming preview | mutable World 与 spatial projections | scheduled mutation/query | 注册 component/system/query contribution |
| rendering | pipeline authoring、preview、Frame Debug | RG/backend/GPU owner | 组合已有 pass、改公开参数 | public renderer encoder/pass program；无 Vulkan 私有逃生口 |

外部 package 是否受信只影响允许加载哪些 native modules，不改变层次规则。签名通过的 package 也不能依赖其他系统
private headers、返回 Vulkan handle 或绕过 World mutation safe point。

## Capability 声明与执行

Capability policy 不是可全局查询的 `SecurityService`。第一阶段冻结窄 vocabulary：mount-relative file read/write、process
launch、network、Editor document mutation、World mutation、console command、native backend access 与 debug-only
operation。执行链固定为：

1. Package manifest 声明 module/contribution 所需 capabilities；
2. Package Runtime 验证声明和 Host role 是否可解析；
3. Host Profile 在 scope activation 时 grant/deny，并只把窄 capability token/facade 放入 factory context；
4. Storage、World、Editor、Settings 等实际 owner 在 public facade 执行检查并返回结构化 denied diagnostic。

Phase 1.5 只要求 vocabulary、grant/deny 和 negative tests；registry signature、remote source trust、license、下载完整性和更强
sandbox 留在外部 package 阶段。受信 package 仍须遵守 public API、scope、safe point 与 shipping closure。

## Settings、Device Profile 与运行时策略

Settings System 合并以下层级并发布 immutable effective snapshot：

```text
engine defaults
  -> package defaults
  -> project settings
  -> Build/Launch Profile
  -> platform/device profile
  -> user settings
  -> validated CLI/session override
```

每个 key 声明 type、owner system、default、hot/restart policy、shipping visibility 和 validation。Platform 提供设备能力与
thermal/power facts；Settings 选择 Device Profile；Rendering、Content、Audio、Animation 等只消费与自己相关的 projection，
不能共同修改一个全局 mutable map。

## Memory & Budget 所有权和压力响应

第一阶段不创建自定义通用 allocator。所有权拆分为：

- Kernel 提供进程启动期 allocator entry、tag hook 和 OOM/fatal 最小路径；
- Memory & Budget System 拥有 `MemoryDomainId` registry、预算/压力策略与只读 snapshot；
- Platform 只发布 memory-pressure facts；
- Content、Renderer、Audio 等 owner 释放自己的 cache/resource，并返回 trim result；
- Observability 只聚合 Memory snapshot 和 owner result，不替任何系统释放资源。

第一版合同冻结：

- `MemoryDomainId` / tag 与 owner system；
- committed、resident、peak、budget 和 pressure level；
- CPU、GPU、resource cache、transient/frame allocation 的分域统计；
- Platform memory-pressure event；
- 各系统按 priority 返回的 trim/eviction result；
- OOM/crash 前仍可输出的最小诊断。

GPU heap budget/usage 的事实由 RHI adapter 投影到 Memory contract，GPU allocation 与 destruction 仍由 RHI/Renderer owner
执行。Runtime Storage 只能为 IO buffer/cache 申请自己的 domain budget，不能成为通用内存管理器。

## Diagnostics 与本地 Crash Baseline

Kernel 的 bootstrap log/fatal hook 必须能在 Observability 激活前工作；Observability 激活后接管 bounded runtime router，
但保持早期记录连续。第一阶段本地 crash report 至少包含：crash GUID、build/package-lock identity、fatal reason、crashing
thread 与可获得的其他线程 callstack、bounded log tail、active Host Profile/scope/package generations、memory pressure snapshot
和用户预先允许的结构化 context。

本地落盘是 Foundation gate；独立 Crash Reporter、符号服务、用户授权 UI、远程上传和 telemetry policy 留到产品发布阶段。

## Time、Tasks 与 Safe Points

不创建一个包办所有时间语义的可安装 Time System。Kernel 拥有 monotonic/wall clock primitive，Host 拥有
frame/application phase、safe point 与 frame pacing，World 拥有 fixed/variable simulation clock、pause/time scale，Tasks
只拥有执行资源和 delayed job/timer。Rendering 的 GPU timeline 只作为 completion/profiling fact，不能反向成为 Host 时间源：

```text
PollPlatform
  -> PublishLifecycle/Input snapshots
  -> Apply scheduled mutations
  -> Fixed simulation steps
  -> Variable update / scripts
  -> Build immutable extraction snapshots
  -> Render record/submit
  -> Publish completions/diagnostics
```

第一版 Tasks 只要求 worker pool、dependency、priority、cancellation、completion queue 和 shutdown join；不提前实现工作窃取、
fiber scheduler 或任意 job 内 World mutation。异步 IO、import、decode、hash 和 snapshot build 可以逐步迁入。

Host phase order 必须可记录、可测试；fixed/variable update、timer completion 和 lifecycle delivery 不得依赖 worker 实际完成顺序。

## World Spatial 基线

完整基础框架需要稳定的空间契约，但不建立一棵全引擎共享 octree：

- World 拥有 entity bounds、spatial identity、region/overlap query 和 immutable spatial snapshot；
- Renderer 从 render extraction 建立自己的 visibility/culling projection；
- Physics 拥有 collision broadphase 和精确 scene queries；
- Navigation 拥有 nav mesh/volume query；
- future World Partition 消费 World spatial contract 与 Runtime Storage，不反向进入 Kernel。

第一阶段只要求 bounds registration/update/remove、AABB/region query、generation safety 和 debug snapshot。streaming cell、
large-world coordinates、HLOD 和通用 spatial database 继续延期。

## Project Upgrade 是基础工具链，不是 Runtime

Package/version/schema 已可演进后，Editor 必须通过统一 upgrade plan 打开旧项目：

1. preflight engine/package/schema/product compatibility；
2. 默认复制或备份，不直接原地破坏；
3. 在隔离 ToolJobScope 中执行 ordered migrations；
4. 重新生成 derived products/build plan；
5. 运行 project/package/system validation；
6. 成功后提交新 descriptor/lock/schema generation，失败保持旧项目可用。

外部 package 可以注册 versioned migration contribution，但不能控制整个升级事务或跳过备份/验证。

## 完整系统的 Definition of Done

一个 Foundation/System Package 只有同时满足以下条件才算“基础功能完整”：

- public contract 与当前 implementation 都存在；
- manifest 声明依赖、roles、factories、contributions、settings 和 shipping closure；
- Host 可以 headless 创建、运行、停止并在失败时回滚；
- runtime 不链接 Editor/tool/private backend；
- Editor/tool contribution 只通过公共 contract 操作；
- outstanding jobs、subscriptions、resources 和 handles 在 shutdown 后为零或有明确 externally-owned 证明；
- settings/schema/product 有 version/migration policy；
- diagnostics 能指出 owner package/module/scope/generation；
- package-local tests 加一个 Host-level smoke 证明 add/activate/use/deactivate/remove/update 行为。

空目录、只有 interface header、只有 Editor UI、只有 provider adapter 或只有 API wrapper 都不算完整系统。

## 分阶段实施门禁

| Foundation Gate | 交付 | 退出证据 |
| --- | --- | --- |
| F0 Current Facts | source-boundary manifest schema、target/package/module-role inventory、planned ownership roots、Kernel allowlist、Host roles | topology gate 能检测 identity/dependency/target/CMake 漂移；current 与 installable 不混淆 |
| F1 Package Plan | manifest vNext、resolver、lockfile、Host Profiles、logical composition、generated build/activation plan | Editor/CLI/CI 对同一输入得到字节等价 graph |
| F2 Host Runtime | scope tree、typed factory/contribution registry、activation lease、rollback、lifecycle | synthetic systems 验证 order/failure/cancel/drain/stale generation |
| F3 Foundation Services | Platform lifecycle/capability snapshot、Memory & Budget、Runtime Storage、Settings/Device Profile、Tasks baseline、Host time/update contract、Observability/local crash、capability grant/deny | Minimal/Runtime/Server/Tool Host headless smokes；pressure/phase/crash/capability negative evidence |
| F4 Data & Content | canonical schema/persistence、artifact/cache/resource runtime | source-free runtime 与 corrupt/migration/reload tests |
| F5 World Baseline | entity/component、clock、mutation、spatial bounds/query、snapshot | headless World + multi-view extraction smoke |
| F6 Authoring Host | Editor Domain、Package Manager UI、upgrade plan、safe mode | project open/add package/edit/build/launch/recover workflow |

F2/F3 未通过前，不把新的大型 System Package、脚本 VM 或复杂 RenderGraph extension 当作主线基础工作。现有渲染和
Editor 代码继续用于验证边界，但新增功能不得反向扩张 Kernel 或 app glue。

F0 的第一项已由 `asharia.package.json` schema v1 与 `tools/check_package_topology.py` 实现：当前 26 个清单全部是
`packageKind: source-boundary`、不可选择且不进入 catalog；每个 target/test target 有单一 role，多个边界可通过
`plannedOwnershipRoot` 聚合到未来完整系统。F0 尚未因为这项落地而整体完成：Kernel allowlist、public consumers、optional dependency
和完整 Host role 标注仍需后续 Slice；它们不得被猜测后一次性写入空字段。

F1 的数据合同基线已经覆盖 installable/Feature Set/Project Manifest/Package Lockfile 与五种 Host Profile v1，并提供
explicit-source Candidate Discovery v1、deterministic in-memory resolver、fail-closed locked graph verification/reuse 和 logical
module/contribution projection。Host Composition Plan v1 的 schema、pure planner、dependency ordering、entry/provenance 与 canonical IR
已经实现。[Source Build Plan v1](adr-source-build-plan-v1.md) 的 independent source descriptor、normalized CMake codemodel
snapshot 与 pure build-root planner 也已实现。它仍不代表 F1 完成：上游 catalog/index、lock update/apply、生产
lock/profile 尚未实现。[Package Product & Artifact Evidence v1](adr-package-product-artifact-evidence-v1.md) 的作者声明、候选
快照和 pure verifier 已落地；[Package Artifact Collection & Publication v1](adr-package-artifact-collection-publication-v1.md)
也已为 #278 实现显式 root、流式 staged verification 与不可变 artifact generation publication。
[Engine Distribution Manifest v1](adr-engine-distribution-manifest-v1.md) 进一步建立只读 Editor/Engine 发行库存、内容派生
`EngineGenerationId`、semantic validator 与 canonical writer；Project Manifest / Package Lock v2 已完成硬切并以
`engine-distribution` reference 绑定 exact generation。[Effective Session v1](adr-effective-session-v1.md) 已实现
Distribution/Project/Profile 对证、状态归类与 Host Composition handoff。
[Engine Distribution Assembly v1](adr-engine-distribution-assembly-v1.md) 已实现显式隔离输入、staged-byte inventory、深度复验和
不可变 generation publication；[Installed Distribution Repair Verifier v1](adr-installed-distribution-repair-verifier-v1.md) 已实现
外部 expected ID、disk-only artifact evidence、read-only installed-tree 深度复验与 `Healthy/RepairRequired` report。
[Package Factory / Scope / Lifecycle Declaration v1](adr-package-factory-scope-lifecycle-v1.md) 已实现 logical factory、owner scope、
required factory、contribution ownership、exact candidate snapshot 与 locked revalidation；它仍不创建 instance 或执行 lifecycle。
Host Activation Blueprint v1、generated static composition root 与
[Host Executable Binding Receipt v1](adr-host-executable-binding-receipt-v1.md) 已实现；
[Static Factory Callback Table v1](adr-static-factory-callback-table-v1.md) 已为 #291 实现 current-process typed callback binding。
[Static Typed Contribution Contract Bindings v1](adr-static-typed-contribution-contract-bindings-v1.md) 已为 #294 实现 Binding Plan v3、
provider v3、Composition renderer 4、private C++ type evidence 与 RegistrationSnapshot v2；它不提供 payload lookup 或 lease。
[Static Contribution Payload Accessors v1](adr-static-contribution-payload-accessors-v1.md) 已由 #295 实现 Binding Plan v4、provider v4、
Composition renderer 5 与 `StaticContributionBindingV2` 的后继硬切；该 ADR 自身只增加 private accessor evidence。
[Activation Eligibility v1](adr-activation-eligibility-v1.md) 已实现 sealed handoffs、按值线性消费、admitted recording wrapper 与
exact-table affinity，并完成 #292 Done evidence。[ProcessScope Lifecycle v1](adr-process-scope-lifecycle-v1.md) 已由 #293 建立 sealed process
projection、factory contexts、token ownership、startup rollback 与 explicit reverse stop；#296 又将 public surface 硬切到 V2，并实现
fixed-slot typed registry、weak handles、contribution-only lease、publication rollback 与 cleanup revocation gate。

production launch issuer、normal Host/Bootstrap 接线、其他 scope owners、完整 instance/jobs/subscriptions lease、轻量启动 receipt 与 repair
executor 仍未实现；生产路径不会仅因这些 headless contracts 存在就把逻辑 IDs 解释为已激活 instances。下一项实现应直接连接真实 Host
和真实 system/contribution vertical feature。

## 拒绝的替代方案

### 巨型 `engine` / `EngineContext`

拒绝。它会把所有系统变成隐式可达服务，使 package dependency、作用域、测试和移除行为不可证明。

### 所有基础能力都做成可卸载 package

拒绝。resolver、Host activation、bootstrap IO/log/time 自身不能依赖尚未解析和激活的 package。采用最小 Kernel 加
完整默认 Foundation Systems，避免 bootstrap cycle。

### 每个 package 自己实现生命周期和线程

拒绝。系统保留自己的状态 owner，但必须服从统一 scope、safe point、cancellation、diagnostics 和 shutdown contract。

### 先做通用 ABI/热插拔，再实现系统

拒绝。第一阶段允许静态链接或启动时注册；native hot unload 只有真实产品用例、quiesce 和 handle/generation 证据后再做。

## 资料依据

- Unreal Subsystems lifecycle: https://dev.epicgames.com/documentation/unreal-engine/programming-subsystems-in-unreal-engine
- Godot architecture (`Core` / `Main` / servers / platform): https://docs.godotengine.org/en/stable/engine_details/architecture/godot_architecture_diagram.html
- Godot application pause/quit lifecycle: https://docs.godotengine.org/en/stable/tutorials/inputs/handling_quit_requests.html
- O3DE Settings Registry: https://www.docs.o3de.org/docs/user-guide/settings/
- O3DE raw file access and asynchronous streaming: https://docs.o3de.org/docs/user-guide/programming/file-io/
- O3DE memory management and allocator tagging: https://docs.o3de.org/docs/user-guide/programming/memory-management/ and https://docs.o3de.org/docs/user-guide/programming/memory/allocators/
- Unreal Device Profiles and scalability: https://dev.epicgames.com/documentation/en-us/unreal-engine/scalability-and-device-profiles-in-lyra-sample-game-for-unreal-engine
- Unreal memory accounting: https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-low-level-memory-tracker-in-unreal-engine
- Unreal Tasks System: https://dev.epicgames.com/documentation/unreal-engine/tasks-systems-in-unreal-engine
- Unreal crash reporting: https://dev.epicgames.com/documentation/en-us/unreal-engine/crash-reporting-in-unreal-engine
- Unity Player loop and low-memory lifecycle: https://docs.unity3d.com/Manual/player-loop-customizing.html and https://docs.unity3d.com/ScriptReference/Application-lowMemory.html
- Unreal World Partition: https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine
- Unreal project upgrade: https://dev.epicgames.com/documentation/en-us/unreal-engine/updating-projects-to-newer-versions-of-unreal-engine

这些资料用于验证生命周期、配置、资源、空间和升级问题确实需要显式 owner；Asharia 采用本文的最小分层，不复制
任一引擎的对象模型、全局单例或插件 ABI。
