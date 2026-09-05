# 整体路线图

更新日期：2026-09-06

本文是全项目下一阶段的唯一**功能阶段路线图**；目标系统框架、package/target 收敛方向、跨系统契约和架构迁移门禁见 `docs/planning/system-architecture-roadmap.md`，Kernel、Host Runtime、Foundation Systems、scope/activation 和基础门禁见 `docs/architecture/foundation-framework.md`；每项能力的最早/最迟接入窗口、Integration Gates 和 Owner Card 见 `docs/workflow/architecture-health.md`。RenderGraph 当前语义见 `docs/rendergraph/mvp.md` 与 `docs/rendergraph/rhi-boundary.md`，可编程管线边界见 `docs/rendergraph/programmable-pipeline.md`；Editor 当前事实见 `docs/architecture/editor.md`；资产系统见 `docs/systems/asset-architecture.md`；shader/material authoring 见 `docs/systems/shader-material-authoring.md` 及 V2 specs。实际 Slice 顺序、状态、阻塞和 Done evidence 维护在 GitHub Issues / Project，不在本文重复。

## 规划依据

### 当前项目事实

- 已有 package-first 基线：`rendergraph` 后端无关，`rhi-vulkan` 不依赖 RenderGraph，Vulkan/RG 翻译在 `rhi_vulkan_rendergraph`，`renderer_basic` 不暴露 Vulkan。
- Vulkan 主路径已覆盖 dynamic rendering、synchronization2 barrier、descriptor/pipeline wrapper、transient image pool、buffer upload、compute dispatch、offscreen RenderView、Frame Debug replay、editor viewport sampled texture，以及真实 RenderView indexed scene-mesh pass。后者使用 Color/Depth + VertexRead/IndexRead、`DrawIndexed` 和 draw packet context，并支持 per-view Solid/Wireframe。
- native Dear ImGui Editor 已具备 production workbench shell、Scene View camera/grid/debug-line、Live RG View、Frame Debugger、Asset Browser snapshot-backed catalog 和多项 smoke。Avalonia Studio 已完成 R0 硬切；#352 建立真实 ProjectSession，#353 已接通 `SceneDocument -> EditWorld -> Hierarchy/Inspector -> dirty/save/reopen`，#359 建立 UI-neutral `ViewportSession`/EngineBridge typed frame lease，#385 以共享 `editor-content` query 接入只读 catalog-backed Resource Browser，#388 再以 Application-owned typed selection 把资产只读详情接入统一 Inspector，#398 让已呈现的 Transform proxies 可被确定性点击，#402 把实际呈现的 validation model bounds 接入同一 typed selection，#404 把单个可见选中 mesh 投影为固定 2 px 橙色描边，#405 再以 Alt-modified orbit/pan/drag-dolly 与 wheel dolly 建立 mouse-only Scene camera navigation，#409 以一次性 ProjectSession edit 和 renderer-owned V8 packet 接入单选世界轴 Translate Gizmo，#411 再以 V9 discriminated packet 接入世界轴 Rotate Gizmo 与 `W` / `E` 模式切换，#413 以 V11 rotation packet 接入局部轴 Scale Gizmo 与 `R`。当前 Viewport V11、Document ABI v3、Catalog ABI v1 与 Scene schema v2 均为硬切合同；最近项目、模板、Studio 对 cooked model product/runtime CPU lease 的消费、GPU 闭环、thumbnail、多 viewport、plane/center-uniform/local translate-rotate/snap/multi-select gizmo、preview 与 Play Mode 尚未接入。
- `asset-core` / `asset-pipeline` / `project-core` / `material-core` / `scene-core` 已是 CPU/headless 数据模型或 baseline package。#367 已闭合 authored typed mesh GUID -> backend-neutral extraction -> validation product binding -> indexed scene raster -> Frame Debug source revision 的受限路径；#386 已把通用 mesh product/受限 source import 闭合到 artifact reader，runtime GPU resource、reload/deferred deletion、material authoring仍未完成。
- #386 已冻结 canonical Mesh Product v1、受限 `.glb` static importer、真实 artifact/manifest 与 bounded
  reader；#394 已完成 verified artifact 与 generation-safe RuntimeResource typed CPU lease。这仍不等于 GPU mesh、
  Scene View consumer 或 thumbnail 已完成。
- 当前风险不是缺少大系统名词，而是 route 太多：渲染、资产、scene、editor、material、play/session 必须按可验证切片合流。

### 外部案例结论

- Unity SRP 把 pipeline configuration asset 与 pipeline instance 分开；Asharia 可以借鉴“配置数据驱动 renderer feature”，但不应现在实现完整 RenderPipelineAsset / RendererFeature 系统。
  参考：<https://docs.unity3d.com/6000.4/Documentation/Manual/scriptable-render-pipeline-introduction.html>
- Unreal RDG 用 pass 参数和资源声明推导 lifetime、barrier、culling 和执行；Asharia 的 RenderGraph 应继续强化 schema/slot/access/diagnostics，而不是让 renderer callback 隐式捕获资源。
  参考：<https://dev.epicgames.com/documentation/unreal-engine/render-dependency-graph-in-unreal-engine>
- O3DE Atom 把 Scene、Render Pipeline、View、Feature Processor 分开，支持多 viewport / 多 pipeline；Asharia 应把 Scene/Game/Preview View 作为同一 renderer/RG 后端上的不同 view request，而不是复制渲染路径。
  参考：<https://docs.o3de.org/docs/atom-guide/dev-guide/rpi/working-with-scene-and-rendering-pipeline/>
- Unreal 把 transform/attachment-only `USceneComponent` 与拥有 mesh、创建 scene proxy 的
  `UStaticMeshComponent` 分开；Godot 同样区分 `Node3D` 与 `MeshInstance3D`，并把 wireframe 放在 Viewport
  debug draw policy；O3DE 让 renderer-owned Feature Processor 生成 Draw Packet。Asharia 采用“显式 mesh component/
  resource reference -> immutable draw packet -> renderer-owned GPU state”和 per-view Solid/Wireframe。采用该 owner
  分离，拒绝照搬 Unreal proxy/UObject API、Godot scene tree 或 O3DE Feature Processor/service；当前复用需求只证明
  `scene-rendering` 的 CPU extraction 和 explicit binding。继续拒绝空 Entity、bounds/debug lines 冒充 mesh，也拒绝
  editor/Avalonia 持有 GPU resource 或 source path。
  参考：<https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/Components/USceneComponent>、
  <https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UStaticMeshComponent>、
  <https://docs.godotengine.org/en/stable/classes/class_node3d.html>、
  <https://docs.godotengine.org/en/stable/classes/class_meshinstance3d.html>、
  <https://docs.godotengine.org/en/stable/classes/class_viewport.html>、
  <https://docs.o3de.org/docs/atom-guide/dev-guide/frame-rendering/>
- Unity、Unreal、Godot 的资产系统都强调 source discovery、metadata/import settings、import/reimport、asset registry/catalog 与 runtime reference 的分离；Asharia 应先稳定 deterministic product/cache 和 resource handle，再做 watcher、热更新或完整 importer UI。
  参考：<https://docs.unity3d.com/6000.4/Documentation/Manual/AssetDatabaseRefreshing.html>、<https://dev.epicgames.com/documentation/unreal-engine/asset-management-in-unreal-engine>、<https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/import_process.html>
- Unreal Project Browser、Unity Hub、Godot/O3DE Project Manager 把工程选择、版本、构建或恢复与编辑器内 asset browser 分开；Asharia 当前采用 Shell-owned launch/recovery surface 作为过渡，不把这些动作放进 Project asset panel，等安装/版本管理需求成熟后再判断是否拆独立进程。
  参考：<https://dev.epicgames.com/documentation/en-us/unreal-engine/opening-an-existing-unreal-engine-project>、<https://docs.unity.com/en-us/hub/project-manage>、<https://docs.godotengine.org/en/stable/tutorials/editor/project_manager.html>、<https://www.docs.o3de.org/docs/user-guide/project-config/project-manager/>
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
| Foundation / Host | 已有 package contracts、resolver/lock、Host planning、artifact verification 与 ProcessScope/static factory/contribution 实现及 synthetic tests；生产包组合与公共服务完整性不能由这些测试直接推定 | 核对真实 consumer 与失败退出边界；按资源链需求补 IO、取消、预算和诊断，避免重建现有控制面 |
| RenderGraph / RHI / Vulkan | 已有 typed pass、slot/schema、abstract access、transient image/buffer、VertexRead/IndexRead、`DrawIndexed`、debug labels、timestamp、Frame Debug replay；`fillModeNonSolid` 是 optional typed capability | 更细 compiler diagnostics、backend lifetime/cache 继续收敛，避免新增 graph 外 GPU work |
| Renderer / RenderView | 已有 Scene/Game/Preview keyed request、world grid、debug line、offscreen sampled target、多 view diagnostics、真实 validation scene-mesh pass、draw packet context 和 per-view Solid/Wireframe | 把 validation product 升级为 asset/runtime resource-backed mesh/material，再扩 lighting/postprocess feature |
| Asset / Project | 已有 project descriptor、source scan、metadata discovery、product manifest、dry-run/execute asset-processor baseline、texture product upload smoke、Mesh Product v1 + 受限 `.glb` importer/reader、verified artifact 与 generation-safe typed CPU mesh lease | renderer GPU mesh owner、dependency invalidation、Scene View/thumbnail consumer 收敛 |
| Material | 已有 CPU-only signature、descriptor contract、pipeline key hash smoke、renderer binding smoke、shader reflection adapter、CPU-only `.ashader` parser/document diagnostics、generated Slang skeleton、generated Slang compile/reflection smoke、generated entry manifest、CPU-only `.mat` minimal IO、#156 deterministic `.mat` product blob 和 #158 deterministic `.ashader` generated Slang product blob | #163 Slang compile/reflection product、material product dependency invalidation、renderer material product 消费和 editor preview |
| Scene / Editor | 已有 SceneDocument-owned EditWorld、默认场景持久化、Hierarchy/Inspector、逻辑 dirty/savepoint、Transform Undo/Redo、production workbench shell、可见 Scene View、presented-model typed selection、固定 2 px selected-mesh outline、mouse-only orbit/pan/dolly、单选世界轴 Translate/Rotate 与局部轴 Scale Gizmo | plane/center-uniform/local translate-rotate/snap/multi-select gizmo 按独立需求证明；WASD fly、focus-selected 与 navigation preferences 单独接入 |
| Workflow / Project | Project fields 完整；#20 是 roadmap/docs sync 入口 | 重复 Project item 候选需单独审查，计划变更后同步 #20 |

## 当前执行优先级（2026-09-05）

当前顺序是基础设施完整性与真实资源消费优先，编辑体验扩展后置。Issue 的 open 状态不能替代主线代码审计：
#270 的合同实现及后续控制面代码已经进入主线（#336 包含其实现历史），不能再安排一轮 Schema/Resolver 重写。
更广的 Foundation integration record 仍需逐项核对，不能由 #270 的合同验收推定整个 #264 已完成。

### 已完成的首个资源切片：CPU Mesh lease 到 renderer-owned GPU Mesh（#419）

#419 已关闭，以下保留其边界；不重新实现 GPU Mesh owner。当前后继优先级见下方 Shader 接入评估。

- 输入只接受已有 verified `MeshResourceLease` 与 immutable upload facts，复用 Mesh Product v1、双 generation 和
  active/candidate 语义；不重新实现 importer、CPU store 或直接从 renderer 读取 `.glb`。
- renderer 定义不含 Vulkan handle 的 resource identity/binding；Vulkan owner 创建 vertex/index buffer 并记录上传，
  upload/access/barrier 可由 RenderGraph diagnostics 追踪。复用现有固定 validation material 先证明 Mesh lifetime。
- 同一资源被多个 draw 引用时复用 GPU allocation；新 generation 完成上传后才替换可见 binding，旧资源直到最后一次
  GPU 使用完成才退役。失败保留 last-known-good；明确首次加载失败与无有效 binding 的 typed/no-draw 行为。
- 首轮用真实已验证 Mesh Product 在现有 RenderView 边界完成 DrawIndexed/readback；覆盖 submesh/index range、
  vertex layout 与当前 material input 的兼容性，不把“上传成功”当作“正确绘制”。
- 负向门禁包括 corrupt/missing input、stale completion、upload failure、替换时在途引用、重复请求与 shutdown drain。
  显式记录 retained/retired allocation 和 upload bytes，约束单次输入及在途请求，不提前实现通用 streaming/eviction。
- SceneDocument/Studio 的通用资源解析接入是后继切片；首个 GPU owner 可以在无 Avalonia 的 native smoke 中完整验证。
  #270 不构成此切片的新 blocker；如发现具体缺失的 IO/lifetime contract，再记录真实依赖。

### 后继基础设施顺序

| 顺序 | 切片边界 | 出口证据 |
| --- | --- | --- |
| 1 | Mesh GPU owner、上传、版本替换与延迟退役 | 真实 product indexed draw、像素/命令证据、失败保留旧资源、关闭后清零 |
| 2 | Shader 编译/反射产物与最小无光照 material binding | layout/signature 兼容诊断；参数变化不重建无关 pipeline；缓存键包含必要编译输入 |
| 3 | 已有 Texture2D product 的运行时消费 | format/sRGB/mip/sampler 合同、上传/替换/释放与材质采样证据 |
| 4 | Scene authored GUID 到资源 binding、保存重开 | 不持久化 runtime key；缺失/过期诊断；重开保持引用与绘制结果 |
| 5 | 跨资源依赖失效与受控替换 | shader/material/texture 依赖变化准确传播；失败保留旧版本；在途帧安全 |

上述是候选拆分顺序，不表示第 5 步之前可以忽略版本与依赖身份：这些字段和本资源的失效/取消边界从第一个
consumer 起就要定义；第 5 步闭合跨资源传播。公共 IO、Tasks、预算、Host teardown 和诊断随真实 consumer 补齐，
不把完整 Foundation 框架作为所有现有资源工作的隐式前置，也不为单条路径新建全局服务或另一套生命周期。
每条资源链都必须有脱离 Editor 的验证；lighting/PBR、材质编辑器、thumbnail、导航偏好与更多 Gizmo 后置。

### Shader 三种创作方式的接入评估（2026-09-06）

依据主线 `9cc04a6e` 的代码审计，当前适合继续资源消费闭环，暂不开始三种创作方式的整体实现。
用户确认的目标与取舍已记录在[Shader authoring 架构](../systems/shader-material-authoring.md#三种创作方式与公共函数自动注册已确认目标尚未实现)。
本段是候选顺序及实施入口条件；实际执行任务与 Done evidence 仍由 GitHub Issues 管理，不提前创建不稳定的远期 Slice。

| 已核对能力 | 当前证据与缺口 |
| --- | --- |
| 文档与生成 | `shader-authoring` 有 parser、raw Slang、引用和源码映射；graph reference 不等于 graph lowering |
| 编译与反射 | `shader-slang` 已有编译和资源/参数布局反射；尚无公共模块函数目录或签名自动发现 |
| 材质参数 | #424/#426 已完成 `.mat` numeric packing 与真实 Slang 布局验证；这是 CPU 字节与兼容性证据 |
| GPU Mesh | #419 已完成 owner，但 `BasicGpuMesh::validate` 仍只接受固定默认无光照材质 key |
| 真实绘制 | RenderView scene mesh 仍检查 `Hidden/RenderViewSceneMesh` / `DefaultUnlit`；未形成 authored Shader/material GPU 消费闭环 |

下一步候选切片：**在现有无光照 Mesh/RenderView 路径消费已验证的 Shader 产物及 `.mat` 数值参数**。

- 先核对已有 product reader/运行时输入是否能组成完整的已验证 Shader binding；若缺最小产物读取/身份合同，
  先以该合同为首个 PR 出口，不在同一 PR 扩成通用材质资源管理器。
- GPU 接入前统一 binding layout version：生成器 `GeneratedSlangOptions.materialSet` 默认 0，而 runtime V2 文档
  规划 material set 1。显式确定并验证采用的合同，不直接修改默认值导致现有测试/产物失配。
- 复用已有 reflected packing；用 authored fragment + 一个 float4 颜色参数证明 `.mat` 值进入真实 GPU draw。
  renderer-owned buffer/descriptor/pipeline 管理生命周期，上游只传已验证的 CPU 数据与资源身份。
- 验收：同一 mesh 使用两个参数值产生可测像素差异；仅数值变化不重新编译 Shader、不重建无关 pipeline；
  布局错误与过期结果被拒绝；候选失败保留有效旧绑定，在途帧完成后才释放被替换资源。
- 门禁：参数/布局失败用例、真实 compile/reflection、spirv-val、RenderView 像素 readback、Vulkan validation、
  相关双编译器和 frame-loop smoke；最终执行范围以新 Slice 验收为准。先不扩 textures、PBR、通用热更新或 Material Editor。

该闭环之后，优先以独立 CPU/编译工具切片验证 **公共模块公开函数 → 自动签名目录 → 生成函数调用 → Slang 编译**；
包括未被入口引用的公开函数、internal 排除、重名/不支持签名、依赖变化与诊断。它在技术上可以现在独立验证，
GPU binding 只是基础设施优先的交付顺序，不建立虚假的技术 blocker。发现能力成立后再做最小图的类型检查、
lowering 与代码/图调用结果一致性，最后接统一 Studio 文档编辑与预览。

以下 P0/P0.5 与 Phase 段落保留各能力的既有范围和基线；当前执行顺序以上述资源切片及 GitHub 实际依赖为准。


### P0：可复用 Viewport foundation 与首个可见 Scene View

#353 的项目/场景编辑闭环已经成立，#359 已收敛不依赖 Avalonia 的 viewport foundation：

- `ViewportSessionId + Scene/Game/Preview + DocumentScene target + camera + request sequence`；
- 每个 session 单 in-flight、dirty-only invalidation 合并与 stale revision completion 拒绝；
- 最多 256 个 Scene Transform debug proxies，超过上限显式 truncated；
- EngineBridge exact-once `ViewportFrameLease`，raw handles 不进入 Application/ViewModel；
- native V11 request 与真实 Vulkan多stream/持久slot、Scene/Game/Preview smoke。

#361 已闭合单个可见 Scene View：绑定 `ViewportPresentation`、Avalonia composition capability/import、
surface generation、resize/detach/drain，并把当前 SceneDocument Transform 轴线呈现在 Studio。该 presentation 保留
session 与 panel/dock 解耦，因此以后增加第二 Scene View、Game View、材质预览和动画预览时不复制 renderer 路径；材质/
动画预览都是 `Preview` render kind + 独立 preview target/world，而不是新的 renderer kind。

#398 在这个既有边界上只增加最小 input 闭环：以 current presented front 为门禁，对当前 frame packet 同源的有界 Transform axes 执行
managed screen-space picking，再发布 `SceneObjectSelectionTarget`；#402 随后让实际呈现的 validation model bounds 成为主要
选择入口，Transform proxy 只保留为无模型实体的回退。两者都不引入 PhysicsWorld、triangle/BVH picking、GPU ID buffer、
asset/runtime GPU owner 或通用 viewport input framework。

#404 已把 Application-owned 单选状态以独立 `ViewStateRevision + ObjectId` 投影到 viewport request；native renderer 只给匹配的
当前可见 draw packet 生成 `Selection Mask -> Outline Composite`，输出固定橙色 2 px 描边。selection 仍不写回
`SceneDocument`、dirty、Undo/Redo 或 save，也没有引入 x-ray、hover、多选、Physics 或通用 post-effect/overlay framework。
完成这条反馈闭环后，#405 以 `Alt+LMB/MMB/RMB + wheel` 建立 mouse-only orbit/pan/dolly：Avalonia 只桥接 focus/capture
和 logical surface-normalized delta，Application 生成 immutable camera snapshot，再复用 `ViewportSession.SetCamera` 与既有 camera
packet。它不写 document/selection，不引入 Physics、WASD fly、focus-selected 或 settings framework。#409 随后只接入
world X/Y/Z translate：move sample 是不推进 hard presentation fence 的 transient preview，release 才以 stable ObjectId、起始 revision
与单个 ProjectEditId 写一次 ProjectSession；Escape/capture/focus/stale revision/failure 均取消或回滚。#411 在相同 transaction
边界增加 world-axis rotate：有符号角度更新 normalized quaternion，近平行时固定退化到 screen tangent；#413 再以 V11
为 discriminated Transform Gizmo packet 增加 normalized rotation，并接入 local-axis non-uniform scale。缩放使用固定起点的
screen-axis 正比例因子，只改一个 local scale 分量，保留镜像符号且不穿过零；renderer 复用已有 debug world-line route，
不新增 Vulkan resource/pass/sync。plane/center-uniform/local translate-rotate/snap、多选、Physics、通用工具 registry、
Avalonia overlay 与 `Update(dt)` 继续拆为独立需求。

Code-first authoring 试点继续保留，但排在首个可见 Scene View 之后；它不阻塞 viewport 或场景闭环。

### MCP 扩展门禁（不进入 #361）

当前 MCP 固定为六项本机只读观测 tools。实体创建、名称/Transform 修改与 Save 以后可以成为受控 MCP 操作，但接入顺序固定为：

```text
authoritative Application command/use-case
-> headless success/failure/revision/dirty/Undo evidence
-> current-user Host capability + explicit Mutate grant
-> typed CLI
-> narrow MCP tool
```

MCP 不成为业务 owner，也不新增第二条 mutation 路径。禁止暴露 native/GPU handle、任意文件或 shell、Dock/ViewModel object、
P/Invoke/Vulkan、runtime shutdown，或绕过 ProjectSession、expected revision、dirty/savepoint 与 Undo。写入 tool 必须有窄 schema、
bounded result、取消、幂等 operation ID、typed receipt 与 audit；remote 默认不开放。该工作应在 SceneDocument command/transaction
合同成熟后建独立 Slice，不能与 Scene View presentation 或 Code-first UI 试点捆绑。

### 已完成基线：项目可真实编辑的最小闭环

下一条主线 Slice 明确定义为：

> `ProjectSession Ready -> 默认 SceneDocument -> EditWorld -> 实体编辑 -> 保存重开`

范围按一个可验收的纵向闭环收敛：

- `ProjectSession Ready` 后创建或打开一个 `SceneDocument`；文档明确拥有 `SceneWorld/EditWorld`，关闭项目时按依赖逆序释放；
- 新建项目原子生成默认场景；支持创建实体、修改名称与 Transform、保存，并在关闭项目后重新打开恢复同一数据；
- Studio 先提供真实 Hierarchy、Inspector、Save 命令与 dirty 状态，不以完整 Asset Browser、停靠系统或漂亮视口替代编辑闭环；
- `Asharia.Studio.EngineBridge` 正式接入和部署 `asharia_scene_native.dll`；Application 拥有文档/世界生命周期，Avalonia 不直接持有或操作原生句柄；
- 成功路径与损坏场景、缺失 native、保存失败、关闭时在途操作均有 typed failure 和可测试的释放语义。

验收序列必须端到端成立：

```text
创建项目
-> 自动创建默认场景
-> 进入 SceneDocument
-> 创建/修改实体
-> 保存
-> 关闭并重新打开
-> 数据一致
```

引擎参考边界：

- Unreal 把 `UWorld` 定义为 map/Actor 的顶层 owner，并明确 Editor 中可以同时存在多个 world；Asharia 采用
  SceneDocument-owned EditWorld，拒绝 process-global current world：<https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UWorld>。
- Unreal Level 与 O3DE Level 都把 create/open/save 作为实体编辑的前置文档动作；Asharia 采用“文档 Ready 后才允许实体命令”，
  但不引入多 Level、World Partition 或 Prefab：<https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-levels-in-unreal-engine>、
  <https://www.docs.o3de.org/docs/user-guide/editor/editor-automation/>。
- Godot 明确 Scene Tree/Inspector 投影与 unsaved 标记必须由编辑命令维护；O3DE 的 Entity Outliner/Inspector 以选择驱动名称和
  Transform 编辑。Asharia 采用同一 snapshot/selection/dirty 命令链，拒绝 UI 直接修改 native world：
  <https://docs.godotengine.org/en/stable/tutorials/plugins/running_code_in_the_editor.html>、
  <https://docs.o3de.org/docs/user-guide/editor/entity-inspector/>。

### P0.5：受限 Code-first authoring 试点

Code-first 不进入当前 Hierarchy/Inspector：前者是大列表，后者包含密集文本与数值编辑，两者继续使用 compiled XAML。
在 P0 编辑闭环与真实 Dock consumer 收口后，立即建立独立 Slice，先恢复最小内部 Code-first kernel 与 Avalonia host，
首个 consumer 是只读、低频、有界的 `Document Diagnostics Summary`：投影当前 Project/Document identity、revision、
dirty/savepoint、scene native availability 与最近少量 diagnostics，并提供 presentation-only 的 Copy Summary action。

该试点的门禁：

- 只保留首个 consumer 所需的 section、text、status、key/value row 与 action；不恢复旧 28-kind DSL、通用 layout/style API；
- stable key、duplicate-key validation、每 dispatcher turn 最多一次 rebuild、attach/detach/dispose 与 Headless semantics 都有测试；
- 不支持 editable text、虚拟化列表、raw Avalonia control 嵌入、frame tick、插件加载或 public `Asharia.Editor` facade；
- 完整 Console/Problems、Hierarchy、Inspector、Scene View 仍使用 compiled XAML 或专用 Avalonia control；
- P1 package resolution summary 作为第二个内部 consumer；两个 consumer 都稳定后才冻结可复用 schema，出现第二个真实外部 consumer 前不提升为 public extension API。

该接入点由 [ADR-0010](../../apps/studio/docs/adr/0010-limited-code-first-authoring.md) 提议；它区分 Avalonia
code-only control composition 与 Asharia 的受限 Code-first schema，不把两者误建模为同一能力。

### P1：项目能力解析与加载状态

- 接入项目级 `asharia.packages.json` 与 `asharia.packages.lock.json`，声明默认完整 Feature Set，并能复现确定的引擎能力组合；
- 将 `NoProject/Ready` 扩展为 `NoProject -> Opening -> ResolvingPackages -> LoadingDocument -> Ready`，并用
  `Degraded/SafeMode` 表达缺包、场景损坏或原生模块缺失，而不是统一退化为“打开失败”。

### 当前不优先

- `.asmdef` 与动态程序集重载、插件市场、多种项目模板、最近项目/自动重开；
- 完整 Asset Browser、Dock 布局持久化/全局命令与 Play Mode。

这些能力不得扩大 P0 Slice；只有在上述保存/重开闭环完成后再进入独立 Issue。

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

该阶段仍是所有后续系统必须遵守的架构门禁，但不抢占上面的 P0 编辑闭环；P0 必须复用现有 package、Application owner
与显式 lifecycle，不得借机扩大 app-local glue。

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
- #386 已实现并验证 Mesh Product v1 与受限 `.glb` source → artifact → reader 纵切；#394 再完成
  manifest-relative path/size/V1 hash verification、typed CPU payload、双 generation、active/candidate 与 lease-safe reload。
  格式/支持矩阵见 [`mesh-product-v1.md`](../systems/mesh-product-v1.md)。仍未完成最终 ArtifactId/store、GPU resource
  owner、dependency invalidation、GPU deferred retirement 或 thumbnail。

### Phase C：Scene Draw Packet MVP

目标：以可审计的最小边界把 authored scene mesh snapshot 提取成 renderer 可消费的 immutable draw packet，并把其 revision
证据带到 Frame Debug；不把 validation fixture 误扩展为 runtime asset pipeline。

当前进度：

- #367 的当前实现使用新 package `asharia::scene_rendering`（`scene-core` + `asset-core` + `renderer_basic`）：输入 scene
  revision、object id、transient `EntityId`、TRS、optional typed mesh reference 和 explicit product binding；输出拥有
  生命周期的 immutable `BasicDrawListItem` vector 与逐项 contextual diagnostics。row-major model matrix 是 `T * R * S`。
- Scene schema v2 / Document ABI v3 是硬切：scene 只持久化 authored mesh GUID/type，不写 runtime entity、product hash/
generation、Basic resource/material key 或 GPU handle；不保留旧 schema/ABI compatibility。
- #366 已把该合同接到真实 `builtin.render-view-scene-mesh`：RenderGraph 显式声明 Color/Depth attachment、
  VertexRead/IndexRead buffer 和 `DrawIndexed`，Vulkan execution event 保留 draw item index 与
  `BasicDrawPacketContext`；Solid/Wireframe 是 per-view policy。
- Wireframe 只在 logical device 已启用 optional `fillModeNonSolid` 时使用 `VK_POLYGON_MODE_LINE`；不可用时 V11
  submit 在复制/入队前返回 typed `FeatureUnavailable`，stream 保持 Open 且可由后续 Solid request 恢复；不让
  capability 成为 context 启动硬要求，不重试稳定失败，不回退 Solid，也不启用 `wideLines`。
- 当前 directional-wedge OBJ -> deterministic generated product 只属于 repository fixture/tool，用于证明真实
  vertex/index data 与像素结果。native resolver 仅为该封闭 fixture 提供 explicit binding；它不是通用 OBJ importer、
  runtime mesh product schema、asset/resource registry 或 service。空 Entity/Transform 也不会隐式产生 mesh。
- `BasicRenderViewSceneDesc.sourceRevision` 进入 diagnostics，并由 Frame Debug capture/JSON/panel 冻结和回显；Scene/Game
  共享 authored mesh input，Solid/Wireframe 仍是 per-view raster policy。

当前范围：

- scene snapshot：object id、transient entity id、transform、optional typed mesh asset reference；不含 runtime/GPU key。
- renderer draw packet：不持有 scene/editor 指针；只从 caller-explicit product binding 取得 resource/material key 与 draw item。
- Scene View / Game View 可同帧使用不同 camera/view request。
- Scene View / Game View 可同帧使用不同 Solid/Wireframe policy，且不修改 scene object/material/source asset。

验收：

- `asharia-scene-rendering-smoke-tests` 覆盖空输入、ready、missing/wrong-kind/stale/invalid binding、revision replacement 和
  matrix/diagnostics；`--smoke-render-view-scene-mesh` 验证真实 RenderGraph scene-mesh pass、indexed execution event 与 Vulkan draw。
- missing/wrong-kind/stale/invalid binding 逐 item no-draw，并保留 scene object/asset/revision context；空 scene 不生成
  scene-mesh pass。malformed V11 packet 必须拒绝整帧，不提交部分 draw。
- 下一 Slice 不再重新定义 mesh product 或 CPU store；应由 renderer 消费 `MeshResourceLease`/immutable upload facts，
  建立 GPU vertex/index owner、revision swap、fence-based deferred deletion 与 material compatibility。不能让 Scene View 或 ThumbnailService
  直接解析 `.glb` 来绕过该链路。

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

当前第一条 Slice 覆盖 `ProjectSession Ready -> 默认 SceneDocument -> EditWorld -> 名称/Transform 编辑 -> 保存重开`，
并用恢复后的最小 Dock 承载真实 Hierarchy、Scene、Inspector 与 Project panel；mesh/material reference、完整资产浏览、
Dock layout persistence、viewport authoring 和 Play Mode 都留给后续 Slice。

范围：

- 前端遵循 `apps/studio/docs/architecture/studio-frontend-framework.md` 的 contribution/backend/lifecycle
  合同和 `apps/studio/docs/architecture/studio-workbench-experience.md` 的默认体验：Scene View 保持中心
  document，Hierarchy/Project 负责查找与选择，Inspector 负责检查与编辑，底部 Diagnostics 按需展开；
- 当前 Hierarchy/Inspector 使用 compiled XAML；P0.5 之后的新 panel 再按 authoring 决策表选择：低频、
  小规模 standard-tool 才使用经真实 consumer 验证的 Code-first schema；compiled XAML 与 code-only Avalonia
  共用同一 content backend；复杂、高频、大列表或文本编辑密集 UI 不扩 Code-first primitive；
- scene file save/load：默认场景、entity identity/name 与 transform baseline。
- Hierarchy 消费真实 scene snapshot。
- Inspector 提供 entity name 与 transform 的最小可写字段。
- 所有 mutation 走 command/transaction；dirty state 与 validation event 可观察。
- EngineBridge 部署并封装 `asharia_scene_native.dll`，Application 的 SceneDocument 拥有原生 world；Avalonia 不接触句柄。

验收：

- 新建项目自动创建默认 scene，editor 能创建实体、编辑名称/transform、保存、关闭项目并重新打开，数据一致。
- `--smoke-editor-shell` 或新增 scene authoring smoke 覆盖 create project -> default document -> create/select entity ->
  edit name/transform -> dirty -> save -> close -> reopen -> equal data。

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
