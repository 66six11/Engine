# 引擎系统架构地图

研究日期：2026-05-08

本文记录 Asharia Engine 在渲染、内存、脚本、插件、线程之外仍必须提前定义边界的引擎系统。它不是当前
MVP 的实现清单，而是避免后续 asset、material、editor、scene 和 script 反向压坏现有 package-first
架构的设计地图。所有结论优先来自官方文档；社区文章只可作为辅助理解，不能作为架构约束。

## 一手资料核对结论

| 引擎 | 已核对事实 | 对 Asharia Engine 的约束 |
| --- | --- | --- |
| Godot | Godot 官方架构把系统分为 Scene layer、Server layer、Drivers/Platform、Core 和 Main；Server 层承载 rendering、audio、physics 等子系统。Godot 4 渲染器建立在 OpenGL 和 RenderingDevice 之上，RenderingDevice 是 Vulkan/D3D12/Metal 抽象。SceneTree 不是线程安全的，Server API 更适合跨线程使用。 | 保持 `rhi_vulkan`、`rendergraph`、renderer package 和 host app 分层；scene/editor 对象默认主线程拥有，跨线程只传数据包或 server-style command。 |
| Unreal | UObject 反射支撑属性、序列化、自动引用更新和 GC；渲染采用 GameThread、RenderThread、RHIThread 分层，并用 RDG 描述 pass/resource 生命周期和屏障；Module 和 Plugin 是扩展边界。 | 不照搬完整 UObject，但需要最小 schema/binding/persistence 元数据；渲染线程以后消费 render snapshot，不直接读 gameplay/editor object。 |
| Unity | SRP 是通过 C# 调度和配置渲染命令的薄 API，底层图形架构再提交到图形 API；Job System 使用 worker threads、work stealing 和 safety system；AssetDatabase 是 editor 侧资产访问接口，Unity 序列化影响 inspector、scene 和性能。 | 脚本/工具只能在 record/build 阶段生成显式声明；并行任务处理 blittable/plain data；资产和序列化规则必须可审查。 |
| O3DE | Atom RPI 是平台无关渲染接口，建在 RHI 之上；Pass System 支持 C++、JSON 或混合 authoring；Asset Processor 后台处理 source asset、生成 product asset、维护依赖和热重载通知；Behavior Context 把 C++ 暴露给 Script Canvas/Lua。 | 先用 C++ typed pass 稳定语义，再考虑数据驱动 pass；asset import 应是独立 product cache 流程；脚本绑定依赖 schema/script context 而不是直接暴露 backend。 |
| Bevy | Bevy 所有 app logic 使用 ECS；系统默认在可行时并行，必要顺序显式声明；所有引擎功能都是 plugin，DefaultPlugins 只是常用组合；RenderGraph 是 nodes/edges/slots 结构，由 graph runner 每帧按依赖执行。 | runtime 热路径优先 data-oriented；package/plugin 应是能力组合，不是 editor 强依赖 runtime；graph 执行依赖显式边，而不是隐式全局状态。 |
| Vulkan | Vulkan host threading 由应用负责外部同步；同一个 command pool 不得多线程同时访问，descriptor pool 分配/释放也不能多线程同时操作；Khronos sample 使用 per-frame/per-thread command pool、descriptor pool/cache 和 buffer pool。 | 多线程录制的第一版必须是 per-frame/per-thread 资源；queue submit/present 先保持单 owner。 |

## 不可缺少的系统

| 系统 | 必须提前决定的边界 | 当前建议 | 暂不做 |
| --- | --- | --- | --- |
| Schema 与持久化 | 稳定 type/field id、typed metadata、C++ binding、archive value、版本和迁移规则。 | 先支持 POD/resource/component 配置；schema/persistence 用于 inspector、asset metadata、script binding 和 scene save/load。 | 不做全局 UObject/GC 宇宙，不强迫所有 C++ 对象反射化。 |
| Asset pipeline | source asset、import settings、product asset、cache、依赖图、GUID、热重载。 | 建议新增 `asset-core` 时使用 `AssetGuid`、`AssetHandle<T>`、importer、product hash 和 dependency graph。 | 不让 runtime 直接依赖源文件路径；不在当前 renderer MVP 引入完整 AssetDatabase。 |
| Scene/World/Entity | 场景文件存储、运行时对象拥有者、transform hierarchy、component 数据布局、prefab/instance override。 | editor 可用对象树，runtime hot path 用 data-oriented arrays；渲染只消费 frame snapshot。 | 不让 renderer callback 捕获可变 scene object；不提前定完整 ECS。 |
| Resource lifetime | CPU handle、GPU allocation、upload、retirement、hot reload、fallback resource。 | 用 generation handle、async load state、deferred destruction queue 和 frame retirement；GPU resource 创建/销毁只走 RHI owner。 | 不在析构函数里假设 GPU 已经不用；不散落 `vkDestroy*` 到 renderer feature。 |
| Frame loop/time | OS event、input、fixed update、game/script update、physics、animation、render packet、submit/present、cleanup 顺序。 | 在 `docs/architecture/frame-loop-threading.md` 固化单线程基线和后续 RenderThread 分阶段方案。 | 不让脚本 VM、asset import 或 editor operation 随机插入 command recording 阶段。 |
| Input | device state、logical action、binding、context、priority、rebinding。 | 从 GLFW callback 提升为 `InputSnapshot` + `InputAction` + `InputContext`；Game、EditorViewport、UI、Console 分 context。 | 不把 raw key code 直接散到 gameplay/editor tool。 |
| Event/command | 事实事件、请求命令、局部 signal/callback 的区别。 | 只提供窄接口：window/input/frame/asset/editor transaction 各自有明确 channel。 | 不做全局万能 EventBus；不让 event 成为隐藏依赖系统。 |
| Editor | editor host、selection、inspector、asset browser、viewport、gizmo、undo/redo、editor-only package。 | editor 只是 host；所有编辑操作走 transaction/command，runtime package 不依赖 editor package。 | 不让 editor 成为 engine core 的 owner；不让 inspector 直接改 Vulkan/backend 对象。 |
| Material/shader/pipeline | shader program、variant、material layout、material instance、pipeline key、descriptor binding model、render queue/pass tag。 | 在 Slang reflection 和 RenderGraph typed pass 稳定后，建立 material layout 与 pipeline key；pass type 仍表示执行模型，不表示业务 shader tag。 | 不提前复制 Unity ShaderLab 或 Unreal Material Graph；bindless 暂缓。 |
| Diagnostics | CPU scope、GPU timestamp、asset trace、memory report、validation report、crash dump、structured log。 | `packages/profiling` 继续作为只读观测底座；后续 GPU timestamp 必须延迟读回。 | 不为了当前帧面板阻塞 GPU；不把 profiler 绑死到 renderer 内部对象。 |
| Build/cook/package | source build、asset cook、runtime product、platform capability、shipping config。 | Conan/CMake 继续只管代码依赖；资产 cook 以后独立为 tool/package 流程。 | 不提交 generated product；不让 editor-only 数据进入 runtime build。 |
| Testing/automation | smoke、benchmark、encoding、diff check、render capture、asset import regression。 | 维持 `asharia-sample-viewer.exe --smoke-*` 入口；每新增系统先有 CLI/smoke，再谈 editor UI。 | 不等完整测试框架才验证；不把人工截图当唯一回归证据。 |

## 推荐分层

```text
apps/
  sample-viewer        current smoke host
  editor               future editor host
engine/
  core                 logging/result/path/assert/type-id/minimal containers
  platform             OS abstraction and app loop hooks
packages/
  rendergraph          backend-agnostic frame graph
  rhi-vulkan           Vulkan backend, queues, swapchain, VMA, command/sync
  renderer-basic       backend-neutral pass contracts
  renderer-basic-vulkan
  shader-slang         shader build/reflection/SPIR-V validation
  profiling            CPU/GPU/profile data model
  asset-core           future GUID/import/product/cache handles
  schema               future type/field/version metadata
  archive              future ArchiveValue / JSON IO facade
  cpp-binding          future C++ object binding
  persistence          future save/load/migration
  scene-core           future world/entity/component/prefab model
  input                future device/action/context snapshots
  editor-core          future editor services and transactions
tools/
  asset-processor      future offline/background asset processing
  shader-build         existing/future shader tooling
```

`engine/core` 只放跨 package 的基础设施，不吸收 asset database、scene graph、editor UI、Vulkan 或 script VM。
每个能力用 package 暴露 public API；host app 只组合需要的 package。

## 阶段顺序

1. 当前阶段：继续完成 RenderGraph diagnostics、deferred destruction、descriptor/transient/pipeline cache。
2. P4 之前：补 `docs/architecture/resource-lifetime.md`，把 GPU retirement、upload、fallback 和 hot reload invalidation 写清楚。
3. Mesh/material 前：[docs/systems/asset-architecture.md](../systems/asset-architecture.md) 已记录 `asset-core` 的 GUID、import settings、product hash、cache 和 dependency 边界；`docs/systems/reflection-serialization.md` 已记录 schema、archive、C++ binding、persistence、metadata projection 和 Inspector/script binding 边界。
4. Editor 前：`docs/systems/scene-world.md` 已记录 scene/world、selection、transaction、render snapshot 和 Play Mode 边界，后续 editor UI 必须消费这些服务而不是直接修改 runtime package。
5. Script 前：`docs/systems/scripting.md` 已记录 ScriptHost、binding registry、execution context、权限和诊断边界；脚本只作为 scene/editor/asset/RenderGraph record API 的前端，不进入 command recording 阶段。

## 审查规则

- 每个新增系统文档必须写明：owner、thread affinity、lifetime、serialization、package dependency、failure path、smoke/benchmark。
- 任何跨线程访问必须说明是 immutable snapshot、message queue、job data 还是主线程回调。
- 任何 GPU resource handle 必须说明 frame retirement 或 explicit ownership。
- 任何 editor-facing 对象必须说明 runtime 是否可见，以及是否参与 save/cook。
- 任何脚本暴露 API 必须说明它能否在 editor、runtime、worker thread、render thread 上调用。
- 文档可以记录未来方向，但实现只能进入当前 milestone 的最小可验证闭环。

## 参考资料

Godot：

- Godot architecture overview: https://docs.godotengine.org/en/stable/engine_details/architecture/godot_architecture_diagram.html
- Godot internal rendering architecture: https://docs.godotengine.org/en/stable/engine_details/architecture/internal_rendering_architecture.html
- Godot thread-safe APIs: https://docs.godotengine.org/en/stable/tutorials/performance/thread_safe_apis.html
- Godot Object class: https://docs.godotengine.org/en/stable/engine_details/architecture/object_class.html
- Godot import process: https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/import_process.html
- Godot GDExtension overview: https://docs.godotengine.org/en/stable/tutorials/scripting/gdextension/what_is_gdextension.html
- Godot editor plugins: https://docs.godotengine.org/en/stable/tutorials/plugins/editor/making_plugins.html

Unreal：

- Unreal parallel rendering overview: https://dev.epicgames.com/documentation/en-us/unreal-engine/parallel-rendering-overview-for-unreal-engine
- Unreal Render Dependency Graph: https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine
- Unreal Object Handling: https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-object-handling-in-unreal-engine
- Unreal asynchronous asset loading: https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine
- Unreal modules: https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules
- Unreal plugins: https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-plugins-in-unreal-engine

Unity：

- Unity SRP fundamentals: https://docs.unity3d.com/Manual/ScriptableRenderPipeline.html
- Unity Job System overview: https://docs.unity3d.com/Manual/JobSystemOverview.html
- Unity script serialization: https://docs.unity3d.com/Manual/script-serialization.html
- Unity AssetDatabase API: https://docs.unity3d.com/ScriptReference/AssetDatabase.html
- Unity Input System actions: https://docs.unity.cn/Packages/com.unity.inputsystem%401.10/manual/Actions.html

O3DE：

- O3DE Atom RPI overview: https://docs.o3de.org/docs/atom-guide/dev-guide/rpi/rpi/
- O3DE Pass System: https://www.docs.o3de.org/docs/atom-guide/dev-guide/passes/pass-system/
- O3DE memory allocators: https://docs.o3de.org/docs/user-guide/programming/memory/allocators/
- O3DE Behavior Context: https://www.docs.o3de.org/docs/user-guide/programming/components/reflection/behavior-context/
- O3DE Asset Processor: https://docs.o3de.org/docs/user-guide/assets/asset-processor/
- O3DE Gems: https://www.docs.o3de.org/docs/user-guide/gems/

Bevy：

- Bevy ECS quick start: https://bevy.org/learn/quick-start/getting-started/ecs/
- Bevy plugins quick start: https://bevy.org/learn/quick-start/getting-started/plugins/
- Bevy RenderGraph API: https://docs.rs/bevy/latest/bevy/render/render_graph/struct.RenderGraph.html

Vulkan：

- Vulkan Guide threading: https://docs.vulkan.org/guide/latest/threading.html
- Khronos command buffer usage and multi-threaded recording sample: https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html
