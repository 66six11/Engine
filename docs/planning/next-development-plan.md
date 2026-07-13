# 整体路线图

更新日期：2026-07-13

本文是全项目下一阶段的唯一**功能阶段路线图**；目标系统框架、package/target 收敛方向、跨系统契约和架构迁移门禁见 `docs/planning/system-architecture-roadmap.md`，Kernel、Host Runtime、Foundation Systems、scope/activation 和基础门禁见 `docs/architecture/foundation-framework.md`。RenderGraph 当前语义见 `docs/rendergraph/mvp.md` 与 `docs/rendergraph/rhi-boundary.md`，可编程管线边界见 `docs/rendergraph/programmable-pipeline.md`；Editor 当前事实见 `docs/architecture/editor.md`；资产系统见 `docs/systems/asset-architecture.md`；shader/material authoring 见 `docs/systems/shader-material-authoring.md` 及 V2 specs。实际 Slice 顺序、状态、阻塞和 Done evidence 维护在 GitHub Issues / Project，不在本文重复。

## 规划依据

### 当前项目事实

- 已有 package-first 基线：`rendergraph` 后端无关，`rhi-vulkan` 不依赖 RenderGraph，Vulkan/RG 翻译在 `rhi_vulkan_rendergraph`，`renderer_basic` 不暴露 Vulkan。
- Vulkan 主路径已覆盖 dynamic rendering、synchronization2 barrier、descriptor/pipeline wrapper、transient image pool、buffer upload、compute dispatch、offscreen RenderView、Frame Debug replay 和 editor viewport sampled texture。
- Editor 已具备 Unity-like workbench shell、Scene View camera/grid/debug-line、Live RG View、Frame Debugger、Asset Browser snapshot-backed catalog 和多项 smoke。
- `asset-core` / `asset-pipeline` / `project-core` / `material-core` / `scene-core` 已是 CPU/headless 数据模型或 baseline package，但尚未形成“真实 scene object -> material/mesh/texture product -> GPU resource -> editor authoring”的完整闭环。
- 当前风险不是缺少大系统名词，而是 route 太多：渲染、资产、scene、editor、material、play/session 必须按可验证切片合流。

### 外部案例结论

- Unity SRP 把 pipeline configuration asset 与 pipeline instance 分开；Asharia 可以借鉴“配置数据驱动 renderer feature”，但不应现在实现完整 RenderPipelineAsset / RendererFeature 系统。
  参考：<https://docs.unity3d.com/6000.4/Documentation/Manual/scriptable-render-pipeline-introduction.html>
- Unreal RDG 用 pass 参数和资源声明推导 lifetime、barrier、culling 和执行；Asharia 的 RenderGraph 应继续强化 schema/slot/access/diagnostics，而不是让 renderer callback 隐式捕获资源。
  参考：<https://dev.epicgames.com/documentation/unreal-engine/render-dependency-graph-in-unreal-engine>
- O3DE Atom 把 Scene、Render Pipeline、View、Feature Processor 分开，支持多 viewport / 多 pipeline；Asharia 应把 Scene/Game/Preview View 作为同一 renderer/RG 后端上的不同 view request，而不是复制渲染路径。
  参考：<https://docs.o3de.org/docs/atom-guide/dev-guide/rpi/working-with-scene-and-rendering-pipeline/>
- Unity、Unreal、Godot 的资产系统都强调 source discovery、metadata/import settings、import/reimport、asset registry/catalog 与 runtime reference 的分离；Asharia 应先稳定 deterministic product/cache 和 resource handle，再做 watcher、热更新或完整 importer UI。
  参考：<https://docs.unity3d.com/6000.4/Documentation/Manual/AssetDatabaseRefreshing.html>、<https://dev.epicgames.com/documentation/unreal-engine/asset-management-in-unreal-engine>、<https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/import_process.html>
- Vulkan 官方资料继续支持当前方向：dynamic rendering 减少预声明 render pass/framebuffer，synchronization2 要求明确 stage/access/layout；VMA 负责 allocator/lifetime 基础，但 transient/pool/counter 策略仍由引擎验证。
  参考：<https://docs.vulkan.org/samples/latest/samples/extensions/dynamic_rendering/README.html>、<https://docs.vulkan.org/guide/latest/synchronization_examples.html>、<https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/>

## 规划原则

- 先做可运行闭环，再扩系统表面。每个阶段必须能用 smoke、CPU test、benchmark 或 editor smoke 证明。
- RenderGraph 只表达 graph facts；RHI/Vulkan 只表达 backend facts；renderer feature 只消费上游 snapshot / resource handle / material signature。
- Asset source、metadata、import settings、product cache、runtime handle、GPU resource 是不同层，不能互相穿透。
- Editor 是 authoring host，不是 runtime owner。Selection、transaction、dirty state、Inspector mutation 必须在 editor-owned command/event 路线上发生。
- Scene/world 是 headless runtime 数据模型；renderer 消费 immutable frame snapshot 或 draw packet，不捕获 `World*` / `Entity*` / editor pointer。
- 外部案例用于校准边界，不作为一次性照搬目标。
- Foundation Gate 优先于新的大型纵向系统：package plan、Host scope/activation、Storage、Settings、Tasks baseline、memory/diagnostics 没有闭环前，不继续扩大 app-local glue。

## 当前基线

| 主线 | 当前状态 | 下一步缺口 |
| --- | --- | --- |
| Foundation / Host | `core` 有 error/log/file baseline，`platform` 仍为空 INTERFACE；package/Host/Settings/Storage/Tasks 为目标设计 | inventory + resolver/lock/Host Profiles；Host scope/lease/registry/rollback；Storage/Settings/Tasks/Observability headless smoke |
| RenderGraph / RHI / Vulkan | 已有 typed pass、slot/schema、abstract access、transient image/buffer、debug labels、timestamp、Frame Debug replay | 更细 compiler diagnostics、backend lifetime/cache 继续收敛，避免新增 graph 外 GPU work |
| Renderer / RenderView | 已有 Scene/Game/Preview keyed request、world grid、debug line、offscreen sampled target、多 view diagnostics、scene draw packet contract | 引入真实 mesh/material/resource-backed scene rendering 和 lighting/postprocess feature |
| Asset / Project | 已有 project descriptor、source scan、metadata discovery、product manifest、dry-run/execute asset-processor baseline、texture product upload smoke、runtime resource handle baseline | texture/mesh importer 最小闭环、dependency invalidation、GPU resource owner 收敛 |
| Material | 已有 CPU-only signature、descriptor contract、pipeline key hash smoke、renderer binding smoke、shader reflection adapter、CPU-only `.ashader` parser/document diagnostics、generated Slang skeleton、generated Slang compile/reflection smoke、generated entry manifest、CPU-only `.amat` minimal IO、#156 deterministic `.amat` product blob 和 #158 deterministic `.ashader` generated Slang product blob | #163 Slang compile/reflection product、material product dependency invalidation、renderer material product 消费和 editor preview |
| Scene / Editor | 已有 scene-core entity/transform baseline、selection/dirty/state event contracts、Unity-like shell、Asset Browser | scene persistence、Hierarchy/Inspector real data、transaction-backed edits、selection outline/gizmo |
| Workflow / Project | Project fields 完整；#20 是 roadmap/docs sync 入口 | 重复 Project item 候选需单独审查，计划变更后同步 #20 |

## 推荐阶段

### Phase Foundation：可扩展基础框架

目标：先建立所有后续完整 System/Feature Packages 共用的启动、作用域、扩展、配置、IO、任务、内存和诊断框架，
避免 Content、World、Scripting、Rendering 和 Editor 各自形成第二套生命周期。

范围：

- current target/package/module inventory 与 Kernel allowlist；
- `package-runtime` manifest/resolve/lock/Host Profile/generated activation plan baseline；
- 计划中的 `host-runtime` scope tree、factory context、activation lease、typed registries、failure rollback；
- Platform lifecycle facts；Runtime Storage/VFS/async IO；Settings/Device Profile；Tasks cancel/join baseline；
- memory domain/budget/pressure 与 bootstrap-to-runtime diagnostics；
- synthetic Minimal/Runtime/Server/Tool Host composition tests。

第一项从 machine classification 开始：当前 manifests 只表示 `source-boundary`，通过 target role 与
`plannedOwnershipRoot` 映射未来完整系统，并由 topology gate 对证 CMake；物理目录合并、resolver 和 Host activation
分别属于后续 Slice，不能在这一步混做。

验收：

- Editor/CLI/CI 对同一 manifest 生成相同 lock/activation graph；
- synthetic systems 验证 dependency order、duplicate contribution、activation failure、rollback、cancel/drain 和 reverse disposal；
- runtime closure 不含 Editor settings/storage/tool modules，不存在 process-wide service locator；
- shutdown 后 scope-owned instances、jobs、IO requests、subscriptions 和 contribution handles 清零；
- 完整门禁满足 `docs/architecture/foundation-framework.md` F0-F3。

该阶段是当前最高优先级；它不要求停止已经可验证的 bugfix/收敛工作，但阻止新增依赖 app-local glue 的大型功能面。

### Phase A：路线图与 Project 收敛

目标：让本地 docs 只保留当前事实、架构合同和下一步路线；GitHub Project 记录历史进度、跨 PR 状态和 Done evidence。

验收：

- `docs/README.md`、本文、`project-management.md` 不再维护重复进度表。
- Project audit 无缺失 `Status` / `Priority` / `Size`。
- 重复标题候选有单独 Project / Issue 处理结论。

### Phase B：Asset Resource Bridge

目标：把 asset-processor 的 deterministic product 输出接到 runtime resource handle 和 graph-visible GPU upload，而不是继续停留在 catalog/report。

范围：

- 最小 texture product：source snapshot + metadata/settings hash + product manifest + product blob。
- Runtime resource handle：不暴露 source path；能表达 pending / ready / failed。
- GPU upload：texture staging -> image -> sampled view，所有 copy/barrier 进入 RenderGraph diagnostics。
- Asset Browser 只显示 product readiness 和 diagnostics，不直接执行 importer。

验收：

- 新增 `--smoke-texture-upload` 或 package-local smoke，验证 product -> GPU sampled texture。
- product/cache hit、source hash drift、missing product、upload failure 都有 deterministic diagnostics。

当前进度：

- #122 已完成最小 `--smoke-texture-upload`：用 deterministic placeholder Texture2D product payload 验证
  staging buffer -> GPU image -> sampled view -> readback，copy command 与 final transition 进入
  RenderGraph diagnostics。
- #124 已完成 CPU-only runtime resource handle baseline：稳定 pending / ready / failed、generation 和
  source-path-free diagnostics。
- #127 已完成 product records -> runtime resource state：把 exact ready、missing、stale/mismatched 和 invalid
  product record 转成 source-path-free runtime state/diagnostics。
- #101 已关闭 source-format / texture profile / catalog sub-asset / product-runtime-GPU 边界 guardrail。
- #129 已完成 texture product blob read + upload diagnostics：把 placeholder product blob 读取和 malformed/missing
  payload 诊断从 sample-viewer ad hoc 逻辑收敛到 asset-pipeline helper，并接入 texture upload smoke。
- #131 已完成 CPU texture importer contract：先用 raw `.rgba8` fixture 验证显式 CPU bytes、`texture.profile`
  / dimensions / format / settings version 和 payload-size diagnostics。
- #133 新增 PNG-first decoder：通过 Conan `stb/cci.20240531` 把 `.png` source bytes 解码为同一套 normalized
  RGBA8 CPU texture payload/result，decoder 代码只在 `asset-pipeline`，不进入 `asset-core`、`resource-runtime`、
  editor、RenderGraph、RHI 或 GPU owner。
- #135 新增 PNG Texture2D product writer：`asset-pipeline` product execution 对 PNG Texture2D request 写出
  deterministic `texture2d-product.v1` blob，记录 source/import/profile/settings/format/尺寸/mip/payload hash；
  `asset-processor --smoke-product-execution` 和 `--smoke-texture-upload` 均从 product blob reader 消费该 payload。
- #137 正在收敛 KTX/KTX2/Basis/HDR/DDS/compressed texture policy：先定义 source/import、product container、
  transcode/cook、runtime format facts 和 GPU owner 边界，不引入新 decoder、Conan dependency 或 Vulkan owner。
- 仍未完成完整 GPU resource owner、dependency invalidation 和
  mesh product/runtime 闭环。

### Phase C：Scene Draw Packet MVP

目标：让 `scene-core` 的最小 world 能产生 renderer 可消费的 immutable draw packet，打通多个 mesh object 的 Scene View 渲染。

当前进度：

- #139 已完成 backend-neutral scene draw packet contract；后续真实 mesh/material/resource-backed scene rendering
  继续从当前 renderer/resource owner 缺口拆分。

范围：

- scene snapshot：entity id、transform、mesh resource handle、material handle。
- renderer draw packet：不持有 scene/editor 指针，只持有稳定 id、transform、resource/material key。
- Scene View / Game View 可同帧使用不同 camera/view request。

验收：

- 新增 `--smoke-scene-draw-packet` 或 editor smoke，验证两个 object 进入 RenderGraph pass / execution event。
- invalid mesh/material handle fail early，并保留 entity/resource context。

### Phase D：Material And Pipeline Binding

目标：把 `material-core` 的 signature/key 从 CPU smoke 推到 renderer 实际 descriptor/pipeline binding。

范围：

- material asset IO 最小格式。
- shader reflection signature 与 material signature compatibility。
- pipeline layout cache / pipeline cache / descriptor set update 以 material key 驱动。
- fullscreen、draw-list、scene mesh 不再依赖硬编码 descriptor 假设。

验收：

- `asharia-material-core-smoke-tests` 继续覆盖 negative paths。
- 新增 material render smoke，验证 material 参数改变会改变 descriptor/params，但不重建无关 pipeline。

### Phase E：Scene Authoring MVP

目标：让 editor 从只读 shell 进入最小可写 scene authoring，但仍保持 command/transaction/dirty/event 边界。

范围：

- scene file save/load：entity hierarchy、transform、mesh renderer、camera/light component baseline。
- Hierarchy 消费真实 scene snapshot。
- Inspector 提供 transform/material reference 的最小可写字段。
- 所有 mutation 走 command/transaction；dirty state 与 validation event 可观察。

验收：

- editor 能新建、保存、加载一个最小 scene。
- `--smoke-editor-shell` 或新增 scene authoring smoke 覆盖 select -> edit transform -> dirty -> save -> reload。

### Phase F：Lighting And Postprocess Baseline

目标：在 renderer feature 层形成第一个非玩具画面路线：G-buffer / lighting / HDR scene color / tone mapping。

范围：

- MRT/G-buffer deferred MVP。
- 最小 punctual light snapshot。
- HDR scene color、tone mapping fullscreen pass。
- profiling counters 标记 per-view / per-pass CPU/GPU cost。

验收：

- `--smoke-gbuffer`、`--smoke-lighting`、`--smoke-postprocess`。
- 至少一个动态 light 影响 scene object，并能在 Frame Debug / RG View 中看到 pass/resource。

### Phase G：Play Session And Diagnostics

目标：建立 Edit Mode / Play Mode 状态机，让 editor 可运行 runtime world copy 或 snapshot，而不污染编辑场景。

范围：

- Edit/Play state machine。
- Game View 使用 runtime world copy 或 snapshot。
- Scene View 和 Game View 同帧共存。
- Diagnostics panel 汇总 frame profile、RenderGraph errors、asset/product/material errors。

验收：

- 进入/退出 Play 不修改编辑 scene dirty state。
- Game View 和 Scene View 可使用不同 camera/view/pipeline flags。

Phase G 只建立 Editor 内隔离 Play Session，不把它当作产品启动验证。后续系统架构 Phase 8 将建立独立的 Project Build/Launch control plane：同一 `asharia.build.json` profile 经 Editor、CLI 或 CI 完成 Build、Cook、Stage，并由 Standalone 子进程走真实 runtime bootstrap。详细边界和 vertical slice 见 `docs/architecture/project-build-and-launch.md`。

### Phase H：Plugin / Script / Advanced GPU

进入条件：Phase B-G 的数据合同和验证稳定。

这里后置的是 ScriptHost/VM、managed plugin execution、hot reload 和高级 GPU 实现；first-party 完整
System Package 的 `asharia.packages.json`、`asharia.packages.lock.json`、resolver、Host Profile 与 Editor Package Manager 基础由
`docs/planning/system-architecture-roadmap.md` 单独规划，不需要等到 Phase H 才定义。

候选方向：

- script VM / plugin manifest / hot reload。
- SRP-like renderer feature authoring。
- bindless / descriptor indexing。
- async compute / transfer queue。
- transient aliasing / graph template cache。

这些方向必须先有设计 ADR、feature query、fallback、smoke 和 profiling evidence；不在当前主线提前铺宽 API。

## 暂缓事项

- 不做完整 Unity SRP / Unreal-style renderer feature authoring。
- 不做 asset watcher、后台 importer farm、package/cook profiles 或完整 asset database UI。
- 不做脚本 VM、热更新插件、第三方扩展市场。
- 不做 bindless、async compute、多 queue、transient aliasing、graph template cache。
- 不做完整 Play Mode、physics/audio/network integration。
- 不把 editor UI 状态、source asset path、import settings、Vulkan handle 或 scene mutable pointer 传进 renderer hot path。
