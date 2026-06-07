# 下一阶段开发方案

研究日期：2026-04-29

更新日期：2026-06-03

RenderGraph 后续专项路线图见 `docs/rendergraph/roadmap.md`。本文保留全项目阶段计划和当前基线，RenderGraph 语义修正、typed pass 收敛、compiler diagnostics、后端生命周期和缓存的执行顺序以专项路线图为准。
性能诊断底座和未来编辑器性能面板的技术细节见 `docs/systems/performance-profiling.md`；当前
`apps/editor` 已以 host 和消费者身份接入通用 RenderTarget / RenderView / ImGui sampled texture
registration 契约，不拥有 renderer 或 Vulkan 后端对象。Editor host、ImGui shell、panel/action/event、
viewport texture registry、输入路由和 editor 子阶段拆分见独立开发文档
`docs/planning/editor-development-plan.md`。
引擎级系统边界和线程路线见 `docs/architecture/engine-systems.md`、`docs/architecture/frame-loop-threading.md`。其中反射、资产、scene/world、editor、script、input 和 build/cook/package 按本文阶段逐步进入；资产基线的 GUID、metadata、product/cache 和 dependency 边界见 `docs/systems/asset-architecture.md`。它们用于约束后续设计不要反向污染 RenderGraph、RHI 和 package 边界。

2026-05-14 边界校准：下文 20 之后是全引擎路线，不代表 render 层继续拥有 scene、editor、asset 或
material 的业务状态。Render 主线只负责 RenderGraph/RHI/renderer feature contract、GPU 资源生命周期、
RenderView 输出和可审查的 command/resource 语义；scene/editor/asset/material 系统作为上游 owner，向
render 侧提供 immutable snapshot、draw packet、resource handle 或 material/pipeline contract。

| 主线 | 拥有者 | render 侧只消费/输出 |
| --- | --- | --- |
| RenderGraph / RHI / renderer feature | `packages/rendergraph`、`rhi-vulkan(_rendergraph)`、`renderer-basic(_vulkan)` | graph 声明、状态转换、barrier、GPU recording、RenderView sampled/present 输出 |
| Scene / World | 未来 `packages/scene-core` | immutable frame snapshot、camera/view、draw packet；不捕获 `World*` / `Entity*` / mutable component |
| Editor / Selection / Inspector | `apps/editor` 和未来 `packages/editor-core` | editor viewport 只消费 RenderView 输出；selection outline 通过 view flags / render packet 表达 |
| Asset / Import / Product cache | `packages/asset-core`、`packages/project-core` 和未来 tools | project asset source roots、resource handle、product data、upload request；source path/import settings 不进入 renderer hot path |
| Material / Pipeline key | 未来 material package 与 renderer feature 层 | shader/resource signature、descriptor contract、pipeline key；`pass.type` 仍只表示执行模型 |

## 当前基线

第一版 MVP 已经具备可运行、可审查、可复现的稳定基线：

- 无参数 sample viewer 可以持续渲染 triangle。
- `--smoke-frame`、`--smoke-rendergraph`、`--smoke-dynamic-rendering`、`--smoke-resize`、
  `--smoke-triangle`、`--smoke-depth-triangle`、`--smoke-mesh`、`--smoke-mesh-3d`、
  `--smoke-draw-list`、`--smoke-mrt`、`--smoke-descriptor-layout`、
  `--smoke-fullscreen-texture`、`--smoke-compute-dispatch`、`--smoke-buffer-upload`、
  `--smoke-renderer-format-contract`
  已作为回归入口。
- Slang shader 构建会输出 SPIR-V、执行 `spirv-val`，并生成记录工具路径和版本的 metadata。
- `packages/shader-slang/tools/slang_reflect.cpp` 已接入 Slang API reflection，triangle shader
  会生成 `basic_triangle.vert.reflection.json` 和 `basic_triangle.frag.reflection.json`。
- `packages/shader-slang` 已提供 reflection JSON 读取模型，`BasicTriangleRenderer` 会在创建时校验
  triangle shader entry/stage、vertex inputs、descriptor bindings 和 push constants 契约。
- `VulkanPipelineLayoutDesc` 已能接收 descriptor set layouts 和 push constant ranges，当前
  triangle shader 仍显式使用空 resource signature。
- `rhi-vulkan` 已提供最小 descriptor allocator-backed pool / descriptor set allocation / buffer +
  sampled image + sampler descriptor write helper；`--smoke-descriptor-layout` 已从 layout-only smoke
  扩展为 layout + allocator + set + descriptor write + allocator counter 验证。
- `VulkanBuffer` 已提供 create/upload/readback counters；triangle、mesh、mesh3D、draw-list、
  descriptor layout、fullscreen texture 和 compute-dispatch smoke 会验证 host-upload、device-local
  与 host-readback buffer 统计。
- `VulkanFrameLoop` 已接入 `VK_EXT_debug_utils` command label helper 和 label counters；renderer-basic-vulkan
  使用 RenderGraph pass name 标记 GPU command 区间。
- `VulkanFrameLoop` 已接入 timestamp query pool、`VulkanTimestampScope` 和 fence-confirmed delayed
  readback；renderer-basic-vulkan 使用 RenderGraph pass name 记录最近一帧 frame/pass GPU duration。
- `--smoke-fullscreen-texture` 已把 descriptor set bind、sampled image descriptor update 和 fullscreen
  dynamic-rendering draw 接入真实 Vulkan 录制路径。
- `--smoke-offscreen-viewport` 已把 editor viewport 的核心离屏路径接入真实 Vulkan 录制路径：持久
  color target 由 RenderGraph imported image 写入，再作为 sampled texture 合成回 swapchain；该 target
  现在可使用独立 viewport extent，在 resize 时通过 frame-loop deferred deletion 回收旧 image/view，
  并通过 renderer 输出 sampled image/view/layout 供当前 editor ImGui backend 注册显示。
- `asharia-editor` 已接入 Dear ImGui host、dockspace/menu、panel/action/event registry、
  Scene View sampled viewport、viewport overlay flags、overlay texture metadata 闭环、on-demand Scene View refresh、
  RenderView diagnostics snapshot、Frame Debug capture/pause state、Live RG View、FrameDebuggerPanel Frame/RenderGraph
  views、Asset Browser snapshot-backed catalog route、Lucide/custom asset icon resolver、ImGui texture registry、
  `--smoke-editor-shell`、`--smoke-editor-asset-browser`、`--smoke-editor-viewport`、
  `--smoke-editor-viewport-resize` 和 `--smoke-editor-frame-debugger`。
- `--smoke-draw-list` 已把后端无关 `BasicDrawListItem`、`builtin.raster-draw-list` schema、
  typed params payload、transient depth attachment 和多 item indexed draw 接入真实 Vulkan 录制路径。
- `--smoke-mrt` 已把 `builtin.raster-mrt` schema、两个 named color slots、两张 transient color
  attachments 和 dynamic rendering multi-color clear 接入真实 Vulkan 录制路径。
- `--smoke-compute-dispatch` 已把 `builtin.compute-dispatch` schema、compute shader reflection、
  storage buffer descriptor、compute pipeline、`vkCmdDispatch` 和 GPU 写入 readback 接入真实 Vulkan
  录制路径。
- `--smoke-buffer-upload` 已把 host-upload staging buffer -> device-local buffer -> host-readback buffer
  的两段 copy 接入 `builtin.transfer-copy-buffer`、`CopyBuffer` command summary、RenderGraph buffer
  transition 和真实 `vkCmdCopyBuffer` 录制路径；该 smoke 只消费显式 upload payload，不读取 source path、
  `.ameta`、importer 或 product cache。
- RenderGraph pass 已具备可选 `type` 字段和 `RenderGraphExecutorRegistry` 执行入口。当前它是
  C++ 快速路径和 typed pass 分发点，后续会演进为脚本/工具前端可生成的 pass 声明、参数和受控
  command context 的共同入口。
- RenderGraph command context skeleton 已接入：pass 可记录 `ClearColor`、`FillBuffer`、`CopyImage`、
  `CopyBuffer`、`SetShader`、`SetTexture`、`SetFloat/SetInt/SetVec4`、`Dispatch` 和
  `DrawFullscreenTriangle` 等后端无关 command summary；当前已由部分 builtin pass 在 Vulkan 录制时消费
  selected commands，其余仍作为 compiled pass、executor context 和 debug table 的可审查 IR。
- Conan 依赖已通过 `conan.lock` 锁定 recipe revision。

下一阶段目标不是立刻接入脚本或扩成完整 SRP，而是先把 RenderGraph 声明模型、shader
layout、资源绑定、transient 资源生命周期和同步边界做稳。未来脚本系统只应复用同一套
C++ builder / command context / compiled graph 语义，而不是引入另一套渲染路径。

2026-05-05 全量诊断结论已合并到本文当前基线、`docs/architecture/flow.md` 和
`docs/workflow/review.md`，不再维护单独的一次性诊断文档。当前主线已通过 MSVC/ClangCL
Debug 构建和完整 smoke 清单；draw list MVP、RenderGraph 最小 dependency sort、负向编译
smoke、显式 culling/side-effect 标记、renderer-basic 共享 builtin schema、builtin schema 负向 smoke
和 callback slot-name binding lookup 已落地，下一步优先级调整为：补 RenderGraph 更细诊断、
deferred destruction、descriptor/transient/pipeline cache 和 multi-view 边界。
不要在下一阶段提前接入脚本 VM、完整 asset database、bindless 或 async compute。
在进入 cache 和 lifetime 优化前，先插入轻量性能诊断底座：CPU scope、benchmark CLI、RenderGraph compile counters 和后续 Vulkan timestamp/debug label 的生命周期设计。这样 P4 的 deferred deletion、descriptor allocator、pipeline cache 和 transient pool 能用数据验证，而不是凭感觉优化。

## 2026-06-03 内部设计审查门禁

2026-05-23 的内部设计审查把内部代码设计提升为与 package boundary 同级的前置门禁。依据来自本地代码审查和
`docs/research/sources.md` 中的一手资料：Unity / Unreal RenderGraph 强调 pass data 与资源使用声明先于执行；
O3DE Atom RPI 把 Scene、Render Pipeline 和 View 分开建模；Khronos/Vulkan 资料要求 format、feature、
layout、stage、access 和 descriptor/resource 合同显式、可验证。

以下门禁插入阶段 21 后续和阶段 22 之前；它们不是新产品功能，而是防止 editor、renderer、RenderGraph
和 RHI 在内部设计上继续累积隐式状态。除非表中写明已完成，否则这些都是 route control gate，不代表实现已经落地：

| 门禁 | 必须先解决的问题 | 本地事实依据 | 验收标准 |
| --- | --- | --- | --- |
| A. Format / capability contract | swapchain format、RenderView target format、RenderGraph image format 和 Vulkan image create 必须统一；不支持的 format 要早失败或完整映射 | A.1 已落地：`basicRenderGraphImageFormat()` 返回 `Result<RenderGraphImageFormat>`，所有 renderer backbuffer / RenderView target 入口显式处理 unsupported format；`--smoke-renderer-format-contract` 验证 unsupported `VK_FORMAT_R8G8B8A8_UNORM` 会在 graph import 前失败 | 当前 `VK_FORMAT_B8G8R8A8_SRGB` 合同已闭合；后续新增 format 必须扩展同一 helper、Vulkan adapter 映射和 negative smoke，不能重新引入 `Undefined` fallback |
| B. RenderView real input contract | camera、overlay、debug line、frame params 不能只进入 diagnostics；需要 renderer-owned per-view constants / pass input 和可见 debug-line/grid pass | B.1-B.3 renderer slice 已落地：Scene View request 携带 camera 与 overlay intent；grid intent 进入 `builtin.render-view-world-grid`，debug-line route 仍可通过 `EditorViewportOverlayProvider` 生成 `BasicDebugWorldLine`；overlay intent 进入 RenderView diagnostics，存在 world line 时 `recordViewFrame()` 才插入 `builtin.render-view-overlay` pass，把 camera/frame/line-count 作为 typed params 与 command summary 记录，并由 `renderer_basic_vulkan` 用 debug-line shader/pipeline 和 per-frame vertex upload buffer ring 绘制 line-list，记录 `DrawDebugWorldLines` execution event；world-grid LOD 已由 RenderView policy 只按 camera 到 grid plane 的垂直距离计算整帧统一的 `GridLodSettings`，不按水平距离或片元距离改变 LOD，低高度锁定 base spacing，拉高后才在 1/2/5/10 spacing 间平滑切换，shader 只消费该参数且默认不做距离淡出；`--smoke-render-view-grid-readback` 已验证四组 camera 的 grid pixel spread、camera-difference、近距离/低高度远水平距离 base LOD command 和高视角可见性；world-grid desc/source overlay id diagnostics 已记录 grid 参数、Scene/debug flags 与 provider packet stable id，Frame Debug replay 会使用 capture 中的 world-grid desc；Scene View `sceneGrid` user settings 已覆盖 plane、minor/major spacing、fade、opacity 和 color，并通过 Scene View request / coordinator bridge 进入 `BasicRenderViewOverlayDesc::worldGrid`；Scene grid overlay contribution 已声明同一份 built-in 默认值，settings bootstrap 用它处理新 settings 和旧 settings 缺省迁移；Editor Settings 已有左侧目录/右侧内容的 grid settings v0；`render_view_pass_policy.inl` 已承载 RenderView pass enablement / typed params policy，`render_view_recording.inl` 只承载 world-grid/debug-line overlay pass insertion | 外部 manifest loader / 热更新暂缓到后续脚本或插件系统设计；当前只保持 built-in contribution 默认值合同。smoke 必须继续验证默认 Scene View 不产生空 overlay pass，tool contribution 默认值与 settings bootstrap 一致，自定义 Scene View grid request 进入 RenderView diagnostics，存在 debug line 时 graph 含 overlay pass，Game/Preview 不接收 Scene-only pass |
| C. Multi-view request model | viewport request 不能是单个 optional；Scene/Game/Preview/多面板同帧需要 per-view id、结果、diagnostics 和 refresh reason | C.1 已落地：`EditorViewportCoordinator` 按 `panelId + kind` 收集 keyed slot，slot 拥有独立 request、presented/pending texture、diagnostics snapshot 和 descriptor owner key；`recordRequestedViews()` 可同帧录制多个 RenderView 并合并 wait stage mask | `--smoke-editor-viewport` 已验证合成 Scene + Game + Preview 同帧请求、Game debug overlay retention、Preview overlay stripping、Scene keyed diagnostics 和 on-demand reuse；后续真实 Game View panel / asset preview UI / per-view profiler 仍需在该 keyed contract 上扩展 |
| D. Graph-visible GPU work | clear、fill、upload、copy、barrier 不能长期藏在 graph 外；外部 pre-pass 必须显式进入 diagnostics | compute storage fill 已进入 `FillStorageBuffer` RenderGraph pass，并通过 `FillBuffer` command summary 出现在 diagnostics / Frame Debug；buffer upload baseline 已把两段 `CopyBuffer` pass 放进 diagnostics，并由 `--smoke-buffer-upload` 验证 staging -> device-local -> readback 数据一致 | 后续新增 GPU work 必须进入 RenderGraph pass/command，或作为 named external pre-pass 在 diagnostics、Frame Debug 和 review 输出中可见；新增 smoke 验证 execution event 顺序 |
| E. Editor state and command model | panel、context、app bootstrap 不能继续膨胀为 service locator / god object；持久 mutation 需要 command/transaction 或 capability-scoped context | `EditorFrameContext` 已从扁平服务集合收敛为 `ui` / `diagnostics` / `settings` / `tools` / `input` / `renderGraph` / `viewport` capability groups；action dispatch 使用 `EditorActionServices` 生成 `EditorActionInvokeContext` 发事件，callback 只接收 `EditorActionContext`，不再接收完整 app context；过渡期 `EditorContext` facade 已删除，app loop / shell / smoke validation 均显式传入所需 event、diagnostics、i18n、settings、workspace、tools、panel 和 frame debugger 服务；run path、smoke layout/settings isolation、i18n resource directory 和 locale env 解析已拆到 `editor_app_config`，panel/action/tool app-level registration 已收进 `editor_app_registration`，editor event queue、diagnostics log、frame debugger、i18n、settings/workspace controller、panel/action/tool registry 和 `EditorActionServices` construction 已收进不可 copy/move 的 `editor_app_services` bundle，Vulkan/window/frame submission helpers 已拆到 `editor_vulkan_host`，shell capability context 适配已拆到 `editor_shell_host`，主循环、每帧 frame context 构造、panel draw dispatch、input/shortcut routing 和 smoke loop state 已拆到 `editor_loop_host`，startup/registration/command smoke gates 已收进 `validateEditorStartupGates()`，command/transaction smoke 已拆到 `editor_command_smoke`，registration/settings/action/tool smoke 已拆到 `editor_registration_smoke`，input run-level smoke 已拆到 `editor_input_smoke`，shortcut registration/run-level smoke 已拆到 `editor_shortcut_smoke`，startup/layout/i18n/font/theme smoke 已拆到 `editor_startup_smoke`，viewport presentation/overlay/camera/diagnostics/resize smoke 已拆到 `editor_viewport_smoke`，Frame Debugger capture/preview/replay/resume smoke 已拆到 `editor_frame_debugger_smoke`，post-loop smoke/layout/shutdown/summary 已拆到 `editor_app_run_completion`，ImGui runtime、fullscreen renderer 和 viewport coordinator 创建/拥有已拆到 `editor_render_runtime`，该 owner 不接收 editor service bundle，且 public header 用 PImpl 隔离 backend-heavy 依赖；这些模块都不重新聚合 editor services；panel 窗口准备阶段已改用只暴露 `ui` 的 `EditorPanelWindowContext`；panel `draw()` 虚接口已改用由 registry 组装的 opaque `EditorPanelDrawContext`，顶层 `EditorFrameContext` 只停留在 `EditorPanelRegistry::drawPanels()` 适配边界，宽 dispatch bundle 的字段布局只存在于 `editor_panel.cpp`；具体内置 panel 已迁移到 `ImGuiSceneViewEditorPanel` / `ImGuiLogEditorPanel` / `ImGuiRenderGraphEditorPanel` / `ImGuiFrameDebuggerEditorPanel` / `ImGuiEditorSettingsPanel` / `ImGuiUiStylePreviewEditorPanel`，Scene View、Log、Live RG、Frame Debugger、Editor Settings 和 UI Style Preview 只覆写各自 per-panel context；helper 不再传播完整 frame context；`EditorStatusBarContext` 只接收 `EditorFrameUiContext`，不再拿完整 `EditorFrameContext`；`imgui_editor_shell` 已改用 `EditorDockspaceContext` / `EditorMenuContext` / `EditorCommandBarContext` / `EditorStatusBarContext` | 继续避免 app service bundle 扩展为宽 service locator；新增或更新 editor smoke 覆盖 action、event、panel 与 viewport 状态 |
| F. RenderGraph API / implementation split | 大型 public API 在继续扩展 cache、alias、multi-queue 或 unsafe/native pass 前需要拆分策略 | `packages/rendergraph` 已改为 STATIC target；types、command list、pass context、execution registry、builder、compile result 和 diagnostics 已拆成窄公共头，`render_graph.hpp` 现在是聚合兼容头；command list、schema/executor registry、非模板 `PassBuilder` / resource declaration API、public compile/execute overload 和 diagnostics formatting 已进入 `src/` 并拆到独立 TU；`render_graph.cpp` 现在只保留 RenderGraph 生命周期与 copy/move facade；声明状态和私有数据已收敛到 `src/render_graph_internal.hpp` + `RenderGraph::Impl`，public builder header 只保留稳定 API 和 pimpl 边界，且只 forward declare schema/executor registry；compile/execute 内部 operation 入口已收敛到 `render_graph_operations.hpp`，public compile facade 已移入 `render_graph_compile.cpp`，public execute facade 已移入 `render_graph_execution.cpp`，两个 TU 不再定义 `RenderGraph::Impl` 成员；public diagnostics facade 已下沉到 `render_graph_diagnostics.cpp`；`RenderGraph::Impl` 已退为 image/buffer/pass 声明容器，不再声明 compile/execute/diagnostics/debug table 成员入口；不访问 `Impl` 私有状态的 debug name / command detail helper 已拆到内部 `render_graph_debug_names.hpp/.cpp`，debug summary label/table helper 已收敛到 `render_graph_debug_summary_tables.cpp` 文件局部实现，diagnostics snapshot 构造声明已拆到内部 `render_graph_diagnostics_snapshot.hpp`、实现已下沉到 `render_graph_diagnostics_snapshot.cpp`，debug table formatting 声明已拆到内部 `render_graph_debug_tables.hpp`、总装实现保留在 `render_graph_debug_tables.cpp`，summary/detail 表格实现已分别下沉到 `render_graph_debug_summary_tables.cpp` 与 `render_graph_debug_detail_tables.cpp`，这些 diagnostics/debug helper 通过 `RenderGraphDeclarationView` 消费 image/buffer/pass 只读 spans，facade 通过 `makeRenderGraphDeclarationView()` 构造 view，factory 实现已下沉到 `render_graph_declaration_view.cpp`，`render_graph_declaration_view.hpp` 只保留 view/factory 声明且保持 `std::span<const Pass>` 自包含，不需要命名 private nested `RenderGraph::Impl`，且不再模板化接收完整 graph/Impl，旧 `render_graph_debug.cpp` 已删除，无状态 schema/slot/command validation helper、validation-only slot group、image/buffer access conflict message、validation-only slot/access/stage helper、execution-only callback error message、compile-only culled-pass materialization 已收敛为只消费 pass span 的文件局部 helper，pass declaration state 已解耦到 `render_graph_pass.hpp`，public pass execution context/callback 已拆到 `render_graph_pass_context.hpp`，pass validation 入口已转为 `rendergraph_internal::validatePass()`，跨 compile/dependency/lifetime 使用的 pass/resource query 与 transition value helper 声明已收敛到 `render_graph_pass_queries.hpp`、实现已下沉到 `render_graph_pass_queries.cpp`，且这些 query helper 与 validation-only slot/access helper 均只消费具体内部 `Pass`、不再保留泛型 `template <typename Pass>` 入口；schema registry lookup 已拆到 `render_graph_schema_queries.hpp/.cpp`，所以 pass query / culling / dependency / lifetime helper 头不再传递 include 完整 execution registry 头；schema/command/required-slot validation 声明已收敛到 `render_graph_schema_validation.hpp`、实现已下沉到 `render_graph_schema_validation.cpp`，`render_graph_validation.cpp` 通过 `validatePassSchema()` 委托 schema gate；pass slot/access/stage validation 声明已收敛到 `render_graph_slot_validation.hpp`、实现已下沉到 `render_graph_slot_validation.cpp`，`render_graph_validation.cpp` 通过 `validatePassSlots()` 委托 slot/access gate；transient allocation、declared-use 与 transition helper 声明已收敛到 `render_graph_lifetime.hpp`、实现已下沉到 `render_graph_lifetime.cpp`，且 lifetime helper 只消费 image/buffer/pass/compiled pass spans，不再模板化接收完整 graph/Impl；handle validation 与 image/buffer graph validation 声明已收敛到 `render_graph_validation.hpp`、实现已下沉到 `render_graph_validation.cpp`，且 validation helper 只消费 image/buffer spans、不再模板化接收完整 graph/Impl，validation 入口声明 forward declare 内部 `Pass` 与 schema registry，不再依赖完整 `render_graph_pass.hpp` 或 execution registry 头；dependency producer inference 与 add/read/build 声明已收敛到 `render_graph_dependency_builder.hpp`、实现已下沉到 `render_graph_dependency_builder.cpp`，且 dependency build 入口只消费 `DependencyBuildInputs` 中的 image/buffer/pass spans，不再模板化接收完整 graph/Impl；dependency topo sort 与 cycle reporting 声明已收敛到 `render_graph_dependency_sort.hpp`、实现已下沉到 `render_graph_dependency_sort.cpp`，且排序入口只消费 pass span 与 dependency 列表，不再模板化接收完整 graph/Impl；active-pass culling 与 imported-resource write check 声明已收敛到 `render_graph_dependency_culling.hpp`、实现已下沉到 `render_graph_dependency_culling.cpp`，且 culling 入口只消费 pass/image/buffer spans 与 dependency 列表，不再模板化接收完整 graph/Impl；这些内部 helper 头不再 include `render_graph_internal.hpp`，均不再作为 `Impl` 成员声明，旧 `render_graph_dependencies.cpp` 编译单元已删除 | 继续保持新增 graph feature 先进入 internal implementation header / `.cpp`，不要把 compile/execute、diagnostics、diagnostics snapshot、debug table、validation、schema validation、slot validation、pass/resource query、declaration view factory、dependency builder、dependency sort、dependency culling 或 lifetime helper 实现重新暴露到 header；继续防止 `render_graph.cpp` 回退成 lifecycle、compiler、executor、diagnostics 混合大文件；继续防止 helper 声明重新回流到 `RenderGraph::Impl`；package header tests 继续锁住 public contract，包括 `RenderGraph` copy/move facade |

E 补充（2026-06-02）: `EditorToolManager` 已作为 editor-only lifecycle owner 接入 `EditorAppServices`，从
`EditorToolRegistry` 同步 built-in tools，按 viewport 记录 primary active tool，并通过 startup registration smoke
验证 unknown-tool 拒绝、activate/deactivate begin-complete 前置条件、layout reset 稳定和同一 viewport tool 切换；同日已让
`EditorToolDesc` 声明 activation policy / activation viewport ids，manager 会拒绝 Frame Debugger 这类非 viewport tool
激活到 Scene View，并用 descriptor 负例 smoke 锁住空、缺失和重复 viewport id。Scene View overlay strip、tool property
model、input behavior routing 和 scene/asset mutation 仍不视作完成，后续必须继续走 capability-scoped context 与
command/transaction 边界。

F 补充（2026-06-01）: Phase 5-BA 已把 image slot validation 与 buffer slot validation 分别拆到 `render_graph_image_slot_validation.hpp/.cpp` 和 `render_graph_buffer_slot_validation.hpp/.cpp`，`render_graph_slot_validation.cpp` 只保留跨 image/buffer duplicate slot name check 与 pass-slot 编排，避免重新形成 validation 大实现单元。

F 补充（2026-06-01）: Phase 5-BB 已把 compiled pass materialization 与 per-pass transition append 拆到 `render_graph_compiled_pass.hpp/.cpp`，`render_graph_compile.cpp` 继续收敛为 compile operation 编排，避免把 pass 展开、access transition 和全局 dependency/final resource 汇总揉在同一实现单元。

F 补充（2026-06-01）: Phase 5-BC 已把 image dependency producer inference 与 buffer dependency producer inference 拆到 `render_graph_image_dependency_builder.hpp/.cpp` 和 `render_graph_buffer_dependency_builder.hpp/.cpp`，`render_graph_dependency_builder.cpp` 只保留 dependency build 编排入口。

F 补充（2026-06-01）: Phase 5-BD 已把 diagnostics pass/command/access/before-transition node 构造拆到 `render_graph_diagnostics_pass_snapshot.hpp/.cpp`，`render_graph_diagnostics_snapshot.cpp` 只保留 resource/dependency/final-transition 与 snapshot 总装，避免 diagnostics snapshot 重新变成跨所有节点类型的大实现单元。

F 补充（2026-06-01）: Phase 5-BE 已把 debug table slot/command/transition/transient 明细表拆到 `render_graph_debug_detail_tables.hpp/.cpp`；该阶段 `render_graph_debug_tables.cpp` 先保留 resource/pass/dependency/culled pass 总览与 Markdown 总装，避免 debug table 重新变成所有诊断表格的单一实现单元。

F 补充（2026-06-01）: Phase 5-BF 已把 resource slot/required-slot schema gate 与 command whitelist schema gate 分别拆到 `render_graph_resource_schema_validation.hpp/.cpp` 和 `render_graph_command_schema_validation.hpp/.cpp`，`render_graph_schema_validation.cpp` 只保留 schema lookup、params contract 与 gate 编排。

F 补充（2026-06-01）: Phase 5-BG 已把 graph image/buffer resource declaration 与 pass declaration/default pass state 构造分别拆到 `render_graph_resource_declarations.cpp` 和 `render_graph_pass_declarations.cpp`，`render_graph_builder.cpp` 只保留 `PassBuilder` fluent mutation 实现。

F 补充（2026-06-01）: Phase 5-BH 已把 debug table resource/pass/dependency/culled pass 总览表拆到 `render_graph_debug_summary_tables.hpp/.cpp`，`render_graph_debug_tables.cpp` 只保留 summary/detail table 的 Markdown 拼装顺序。

F 补充（2026-06-01）: Phase 5-BI 已把 image/buffer 同一 resource 的 access conflict validation 拆到 `render_graph_image_access_validation.hpp/.cpp` 和 `render_graph_buffer_access_validation.hpp/.cpp`，image/buffer slot validation 文件只保留 name/handle/shader-stage 基础校验。

E 补充（2026-06-01）: Step 2b-w 已把 Frame Debug image preview / replay recording 从 `editor_viewport_coordinator.cpp` 拆到 `editor_frame_debug_preview.cpp`；viewport coordinator 主实现保留普通 viewport request、slot、texture lifetime 与 diagnostics 编排。

E 补充（2026-06-01）: Step 2b-x 已把 Editor UI section header、property table、status pill 与 color swatch 绘制 helper 从 `editor_ui.cpp` 拆到 `editor_ui_widgets.cpp`；`editor_ui.cpp` 继续保留主题 catalog、当前主题状态、color token 与 ImGui style 应用。

E 补充（2026-06-01）: Step 2b-y 已把 Frame Debug replay pass/event/image 选择、preview request consume/publish/unavailable 状态更新从 `editor_frame_debugger.cpp` 拆到 `editor_frame_debugger_replay.hpp/.cpp`；`editor_frame_debugger.cpp` 保留 capture/resume/fence 状态机和只读查询，GPU preview 录制仍在 `editor_frame_debug_preview.cpp`。

E 补充（2026-06-01）: Step 2b-z 已把 RenderGraph snapshot enum/name、resource/pass label 与 access cell 数据整形从 `render_graph_snapshot_view.cpp` 拆到 `render_graph_snapshot_format.hpp/.cpp`；`render_graph_snapshot_view.cpp` 保留 ImGui summary、timeline matrix 与 detail table 绘制。

E 补充（2026-06-01）: Step 2b-aa 已把 RenderGraph snapshot access events、resource、pass 与 dependency 明细表绘制从 `render_graph_snapshot_view.cpp` 拆到 `render_graph_snapshot_details.hpp/.cpp`；`render_graph_snapshot_view.cpp` 保留 summary、access timeline matrix 与 hover tooltip，后续不再为了行数继续强拆。

B 补充（2026-06-02）: Scene View tool/overlay contribution 已先经过 `EditorExtensionRegistry` v0 再发布给
`EditorToolRegistry` query facade；smoke 验证 stable id、reload-style replace、重复 tool id 拒绝、失败 reload 不改变旧状态，以及发布到 tool facade 时保持 all-or-nothing。它仍不是 external manifest loader，panel/action callback 也仍由现有 registries 显式注册。

阻塞规则：

- A 必须保持通过；扩大 swapchain/offscreen target format、material/pipeline format key 或 texture preview 范围时，先扩展 format helper、Vulkan adapter 映射和 negative smoke。
- B 的可见 debug-line renderer pass、camera-aware grid visibility、world-grid/source overlay diagnostics、Scene View
  `sceneGrid` settings bridge、built-in contribution default、grid settings UI v0 和 built-in extension registry v0 已完成；脚本/插件系统边界、provider 合同和真实 external manifest/reload path 未完成前，不继续做复杂 gizmo、selection outline 或依赖 Scene-only authoring pass 的产品体验。C.1 keyed request model 已关闭单 request 覆盖问题，但真实 Game View、asset preview viewport 和
  per-view profiler UI 仍必须复用该 keyed contract。
- D 的 compute fill 已闭合；继续禁止新增 graph 外 compute/upload/copy 快捷路径。
- E 未完成前，不进入 asset browser、material editor、脚本热更新、持久 editor layout 或复杂 inspector mutation。
- F 未完成前，不推进 async compute、transient alias、unsafe/native pass 或多队列 RenderGraph。

## 后续总路线

本轮审查后的核心调整是：编辑器不再只是远期暂缓项。它应在通用 RenderTarget / RenderView 和 ImGui sampled
texture registration 契约稳定后尽早接入，作为渲染层真实消费方；但 editor 不能成为 renderer 或 RHI 的 owner。
后续路线按两条线推进：render 侧先完成通用渲染目标、RenderView、RenderGraph buffer/storage/MRT/compute
和 renderer feature contract；系统侧再接入 scene/editor/asset/material owner。两边的接口是 snapshot、
draw packet、resource handle、material contract 和 RenderView target，不是跨 package 访问实现对象。

1. **通用 RenderTarget / RenderView / ImGui texture registration contract**：把当前 offscreen viewport 小闭环提炼为通用 RT、view target 和 editor ImGui backend 可消费的 sampled target 契约。
2. **Editor app skeleton / ImGui shell / viewport**：新增 editor host，先显示 RenderView 输出，不做 gizmo、inspector 或 asset browser。
3. **RenderGraph Buffer / Storage / MRT / Compute**：补 buffer resource、storage access、buffer barrier、多 color attachment 和最小 compute dispatch。
4. **最小 Scene/Object/Selection 层**：提供 object identity、transform、selection model 和 Scene View debug flags，让 gizmo/inspector 有稳定对象来源。
5. **Gizmo / Grid / Debug Draw**：作为 Scene View 专用 pass 进入 graph，不能污染 Game View。
6. **Asset-core + asset-pipeline + resource upload baseline**：`asset-core` 建立 GUID、source metadata、handle 和产品 key 数据模型；`packages/project-core` 只建立 project identity 与 asset source roots descriptor；`asset-pipeline` 先落地 deterministic source tree scan、显式 source/.ameta metadata discovery、diagnostics、source file snapshot/hash、product manifest IO、import planning baseline、scan-to-planning bridge 和 deterministic product execution baseline；`tools/asset-processor` 先提供 read-only dry-run CLI 报告和受控 `execute` product output smoke；后续 `asset-processor` / pipeline slice 再负责真实 importer、import 调度和 dependency invalidation；renderer/RHI 只接 mesh/texture upload 请求与已解析 product data。
7. **Material / Pipeline key**：建立 material signature、descriptor contract、pipeline state key、layout/pipeline cache 和 mismatch 诊断。
8. **Material editor / Asset browser**：editor 消费 asset/material API，不直接访问 renderer/RHI 内部对象。
9. **Lighting baseline**：优先用 MRT/G-buffer deferred MVP 验证 RenderGraph 的资源和 barrier 价值，再评估 Forward+。
10. **Scene/world persistence**：保存/加载 scene、entity hierarchy、mesh renderer、camera/light component。
11. **Postprocess / Temporal**：HDR、tone mapping、bloom、history textures、frame params。
12. **Play Session / Multi-view diagnostics**：Edit/Game 分离，Game View 与 Scene View 同帧共存，profiling/debug table 按 view 输出。

阶段归属速查：

| 阶段 | 归属判断 |
| --- | --- |
| 13-15 | render/RHI contract：通用 RenderTarget、RenderView target 和 sampled output contract |
| 16-17 | editor host/integration：ImGui shell、viewport texture registration；不进入 renderer core |
| 18-19 | render 主线：RenderGraph buffer/storage/MRT/compute 和 Vulkan adapter/recording |
| 20-21 | scene/editor 主线：object、selection、gizmo/grid/debug draw 的数据 owner；render 只消费 Scene View packet |
| 22 | asset/resource 主线：asset-core 拥有 GUID/import/cache 数据模型，asset-pipeline 从 metadata discovery 进入 import/cache 更新，RHI/renderer 执行 GPU upload 和 lifetime |
| 23 / 25 / 27 | renderer feature + material contract：材质、lighting、postprocess 可以扩展 graph 语义，但不拥有 asset/editor UI |
| 24 / 26 / 28 | editor/scene/app 主线：asset browser、scene persistence、play session；render 只接收最终 view/render packet |

高级能力继续放入暂缓池：ray tracing、bindless/descriptor indexing、async compute/multi queue、多线程 command recording、transient memory alias、shader hot reload、完整脚本 VM 和完整 asset database。进入条件是已有 smoke/benchmark 证明前置小闭环稳定，并且新增能力能独立验收。

## 一手资料结论

- Slang reflection 应通过 Slang compilation API 获取，`ProgramLayout` 通常由
  `IComponentType::getLayout()` 得到；现有 `slangc` 命令行 metadata 只能作为工具链证据，
  不能替代反射数据。
  资料：https://docs.shader-slang.org/en/stable/external/slang/docs/user-guide/09-reflection.html
- Vulkan dynamic rendering 通过 `vkCmdBeginRendering` 在命令录制时指定 attachment，
  适合继续扩展 color/depth attachment，不需要回退到传统 render pass/framebuffer。
  资料：https://github.khronos.org/Vulkan-Site/features/latest/features/proposals/VK_KHR_dynamic_rendering.html
- 新增 RenderGraph 状态、transient image、depth attachment 或 texture binding 时，必须同步定义
  layout、stage、access 和 execution/memory dependency；同步问题优先用 validation 和
  synchronization2 路径验证。
  资料：https://github.khronos.org/Vulkan-Site/spec/latest/chapters/synchronization.html
- RenderGraph transient image 可以由 VMA 负责创建和绑定内存；VMA 推荐使用
  `VMA_MEMORY_USAGE_AUTO` 一类策略让 allocator 根据用途选择合适 memory type。
  资料：https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
- Descriptor indexing/bindless 能力很强，但会引入 non-uniform indexing、update-after-bind 等新约束；
  当前阶段先做固定 descriptor set/binding 契约，bindless 放到 material/texture 扩展后。
  资料：https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html
- Dear ImGui Vulkan backend 已提供 editor texture 注册点；当前 pinned Conan 依赖使用
  `imgui/1.92.7-docking`，其 Vulkan backend 的 texture registration API 是
  `ImGui_ImplVulkan_AddTexture(VkSampler, VkImageView, VkImageLayout)`。因此 Asharia Engine 不应自建通用
  `UiTextureHandle` 或 `packages/ui` 来替代 ImGui backend 的 texture id 机制；renderer 只需要输出
  sampled target 的 image view、sampled layout、format 和 extent，editor ImGui integration 负责
  sampler 选择、注册/注销。
  资料：https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_vulkan.h
  资料：https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples
- Vulkan descriptor image info 仍是 image view、layout 和 sampler 这类采样资源边界；resize 后的
  image view descriptor 生命周期必须由 editor integration 显式更新或注销，不能继续引用 deferred
  deletion 的旧 view。
  资料：https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorImageInfo.html
- Conan lockfile 用于依赖快照和可复现依赖解析；当前已完成，后续依赖变更时审查 lockfile diff。
  资料：https://docs.conan.io/2/tutorial/versioning/lockfiles.html
- Unity SRP/URP RenderGraph 的实用边界是：C# 在 record 阶段可以使用普通控制流创建 pass、填充
  PassData 并显式声明资源使用；RenderGraph 编译主要分析这些声明，实际 render function 在编译后
  通过受控 command context 执行。Asharia Engine 后续可以借鉴这个边界：脚本/工具可生成 graph 和受控命令，
  但编译优化必须基于显式 resource access，而不是解析任意脚本闭包。
  资料：https://docs.unity.cn/6000.0/Documentation/Manual/urp/render-graph-write-render-pass.html
- Unity Core RP 的 RenderGraph 执行模型是每帧 setup、compile、execute；资源通过 graph handle
  操作，pass 必须显式声明读写，graph 计算 resource lifetime，并可剔除输出未被使用的 pass。
  资料：https://docs.unity.cn/cn/Packages-cn/com.unity.render-pipelines.core%4014.1/manual/render-graph-fundamentals.html
- Unity `RenderGraphBuilder` 的 API 形态显示了需要覆盖的资源声明：`ReadTexture`、`WriteTexture`、
  `UseColorBuffer`、`UseDepthBuffer`、`Read/WriteComputeBuffer`、`UseRendererList` 和 transient
  texture/buffer；Asharia Engine 的 named slots 应覆盖同一类语义，而不是只提供位置参数。
  资料：https://docs.unity.cn/Packages/com.unity.render-pipelines.core%4011.0/api/UnityEngine.Experimental.Rendering.RenderGraphModule.RenderGraphBuilder.html
- Unity Render Graph Viewer 中的 read-write 展示是 pass 对资源的访问摘要；普通 render pass 对同一 texture 同时读写仍需要临时纹理、兼容/unsafe 路径或更明确的访问模型。
  资料：https://docs.unity.cn/6000.0/Documentation/Manual/urp/render-graph-viewer-reference.html
  资料：https://docs.unity.cn/Manual/urp/render-graph-read-write-texture.html
- Unity 的 `AddUnsafePass` 允许兼容式命令，但会降低优化能力，例如无法安全合并后续写同一 buffer 的 pass。
  Asharia Engine 后续如提供 unsafe/native pass，应只作为迁移和调试逃生口。
  资料：https://docs.unity.cn/6000.0/Documentation/Manual/urp/render-graph-unsafe-pass.html
- Vulkan pipeline cache 可复用 pipeline construction 结果，既可在相关 pipeline 间复用，也可跨应用运行复用；
  因此 pipeline 创建应在 backend/cache 层，不能落入每帧 RenderGraph compile。
  资料：https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineCache.html
- O3DE Atom RPI 把 Scene、Render Pipeline 和 View 分开建模，editor level view、material preview 和 game
  view 可以作为不同渲染上下文消费同一套渲染基础设施。Asharia Engine 的 editor viewport 应先消费通用
  RenderView / RenderTarget，而不是创建 editor 专属 renderer 路径。
  资料：https://www.docs.o3de.org/docs/atom-guide/dev-guide/rpi/rpi-system/
- Unity AssetDatabase 强调 source asset、import settings、artifact/cache、dependency hash 和 GUID 等边界；
  Asharia Engine 进入 Asset Browser / Material Editor 前，应先有最小 `asset-core`，不能让 runtime 直接依赖源文件路径。
  资料：https://docs.unity.cn/Manual/AssetDatabase.html
- Vulkan compute shader 路径需要 queue 支持、compute pipeline、`VK_PIPELINE_BIND_POINT_COMPUTE` 和
  `vkCmdDispatch`；当前已在真实 compute smoke 中同步验证设备 capability、pipeline、storage buffer
  descriptor 和 dispatch readback。
  资料：https://docs.vulkan.org/guide/latest/compute_shaders.html
- Vulkan ray tracing 最小路径已经包含 acceleration structure、ray tracing pipeline、shader binding table
  和 raygen/miss/hit shader 等完整对象族；当前阶段只记录为高级能力池，不应在 image-only RenderGraph
  和固定 material/resource 模型上硬接。
  资料：https://docs.vulkan.org/samples/latest/samples/extensions/ray_tracing_basic/README.html

## 优秀案例借鉴

| 案例 | 可借鉴点 | 对 Asharia Engine 的落地方式 | 暂不照搬 |
| --- | --- | --- | --- |
| Frostbite FrameGraph | pass setup 和 execution 分离；setup 阶段声明资源读写，execution 阶段只消费解析后的资源 | 继续要求 RenderGraph pass 在声明期暴露 resource access；后续 reflection/descriptor 信息也先进入声明或 metadata 层 | 不直接复制大型 world renderer/feature 系统，避免超出当前 package 边界 |
| Unreal RDG | 整帧延迟编译、dependency-sorted execute、资源 alias、barrier/memory 管理和开发期 validation | 先从 validation 和调试表开始：缺失 binding、非法 state、未声明资源访问都应在 compile/record 阶段报错 | 暂缓 async compute、alias memory 和 RDG Insights 级别工具 |
| Unity Render Graph | pass data、resource usage 和 render function 分离；record 阶段可用普通语言控制流构建图，compile 阶段依赖显式资源声明；兼容期允许 unsafe pass 但优化能力下降 | 当前先在 C++ builder 中形成 `PassParams`、named resource slots 和受控 command context；未来脚本只是这些 API 的前端 | 不把 Unity 的完整 SRP 层级、camera stack、RendererFeature 生态提前搬入 MVP |
| Granite | Vulkan 侧实践强调 render graph、deferred destruction、自动 descriptor/pipeline、linear upload allocator | 后续 mesh 阶段优先做 staging/linear upload 和 deferred destruction 规则，再扩大 descriptor 自动化 | Granite 后端偏 Vulkan-first；Asharia Engine 的通用 RenderGraph 层仍保持后端无关 |
| vuk | 资源通过 access-annotated pass 参数进入图；直接捕获外部资源会绕过自动同步；transient/persistent 资源区分清楚 | `--smoke-transient` 设计应区分 declare transient image 和 import/acquire persistent resource；pass callback 不直接偷用未绑定 VkImage | 暂缓 futures、多 queue 自动调度和跨 graph composition |
| Blender Vulkan render graph | 后端可把 draw/compute/transfer 命令收集成 graph，再重排并生成同步 barrier | depth/transient 阶段可把 transfer clear、dynamic rendering begin/end、draw 拆成更清楚的后端节点或调试事件 | 不把 Blender GPU module 的多线程 context 模型提前搬进当前单窗口 sample |
| Diligent Render State Packager | shader、pipeline、resource signature 可离线打包，构建期发现 shader/pipeline 问题，运行期减少编译依赖 | reflection JSON 之后，逐步把 pipeline layout/resource signature 固化为可审查的构建产物 | 暂不做跨 API archive 和完整离线 PSO packager |

## 推荐推进顺序

| 阶段 | 目标 | 主要改动 | 验收标准 |
| --- | --- | --- | --- |
| 1 | Slang reflection 基线 | 已在 `packages/shader-slang` 增加 reflection 工具，输出 `*.reflection.json` | triangle shader 生成 entry、stage、vertex inputs、descriptor set/binding、push constant 信息；现有 smoke 不退化 |
| 2 | Descriptor/Layout 契约 | 已开始用 reflection JSON 校验 triangle shader 契约，并打通 pipeline layout/resource signature 接口 | layout mismatch 能在构建或启动时报清楚错误；triangle smoke 继续通过 |
| 3 | RenderGraph 声明模型 v2 | named write slots、params type id、typed POD params payload 和最小 pass schema 已接入；schema 现在校验 slot、params type 和 allowed command kind；下一步继续补更细的 compile/execute 错误；`pass.type` 先作为执行模型 / typed pass key | `--smoke-rendergraph` 已验证 type、slot、params type、allowed command kind 和 schema 能进入 compiled pass 和 executor context，并覆盖每个 renderer-basic builtin pass 的 missing slot、invalid slot 和 wrong params type 负向编译；旧 callback 路径不退化 |
| 4 | RenderGraph access/state 扩展 | `ShaderRead(fragment/compute)`、`DepthAttachmentRead/Write`、`DepthSampledRead(fragment/compute)` 已接入，并同步 Vulkan layout/stage/access 翻译 | `--smoke-rendergraph` 已验证 shader-read、depth-write、depth-sampled-read transition、shader stage/domain、depth aspect barrier 和 Vulkan adapter 字段；尚不引入实际 shader sampling |
| 5 | RenderGraph transient image | `createTransientImage()`、transient lifetime plan、debug table 和 `--smoke-transient` 已接入 | `--smoke-transient` 已验证非 backbuffer image transition、first/last pass、final shader stage 和 Vulkan adapter mapping |
| 6 | PrepareBackend transient allocation | 已增加 Vulkan image/image view RAII、VMA-backed transient image 创建、usage/aspect 推导和 binding 表接入 | `--smoke-transient` 已升级为真实 transient VkImage 录制路径，validation 无 error/warning |
| 7 | Depth attachment MVP | 已增加 dynamic rendering depth attachment、`D32Sfloat` transient depth image、depth aspect binding 和 depth-enabled pipeline | `--smoke-depth-triangle` 已接入，validation 无 error/warning |
| 8 | 受控 command context skeleton | 已增加 `RenderGraphCommandList`、`RenderGraphCommand`、compiled pass command summary、executor context command span 和 debug table 输出；`--smoke-rendergraph` 已覆盖 clear、set shader、set texture、float/vec4 参数和 fullscreen draw summary | command summary 可审查；不暴露 Vulkan API；不接入脚本 VM；resource access 仍必须在 builder 上显式声明 |
| 9 | Descriptor binding / fullscreen pass | 已增加最小 descriptor pool、descriptor set allocation、uniform-buffer write、sampled-image write、sampler write、descriptor bind 和 fullscreen dynamic-rendering draw；fullscreen 路径使用共享 builtin schema、allowed command kind、typed params payload、command-derived pipeline key 和 `source` slot binding lookup；mesh / draw list 路线已接上同一套声明模型 | `--smoke-descriptor-layout` 已验证 descriptor set 分配和 buffer/image/sampler write；`--smoke-fullscreen-texture` 已验证 shader 真实采样 transient source image |
| 10 | Mesh asset / draw list 路线 | 已从固定顶点数据扩展到最小 indexed quad mesh、host-upload index buffer、indexed draw、独立 3D cube smoke，以及最小 draw list；clear、triangle、depth triangle、mesh3D、draw list 和 fullscreen 路径已通过 `renderer_basic` 共享 builtin schema compile，depth/draw-list 回调通过 `depth` slot 查询 binding；当前使用固定 MVP push constants，不引入全局相机系统；后续再补 resource upload、material/pipeline key 和 asset database | `--smoke-mesh` 已渲染 indexed quad；`--smoke-mesh-3d` 已验证 3D vertex layout、depth 和 MVP push constants；`--smoke-draw-list` 已验证 typed pass + 多 item indexed cube draw |
| 11 | RenderGraph compiler v2 起步 | 已增加 pass declaration index、read/write dependency table、稳定拓扑排序、最小 producer-read 依赖推导、`allowCulling` / `hasSideEffects` 标记和 culled pass 列表；compiled graph 按依赖顺序执行，direct callback 通过 declaration index 找回原 pass；无 producer transient read 和缺失 schema 会在编译期失败 | `--smoke-rendergraph` 已把 transient reader 声明在 writer 前，并验证 compiled order 会把 writer 排到 reader 前；debug table 输出 dependencies 和 culled passes；负向 smoke 验证错误不会进入 pass callback；culling smoke 验证 unused transient writer 不进入 execute，side-effect pass 会保留 |
| 12 | 性能诊断底座 | `packages/profiling`、CPU scope、benchmark CLI、RenderGraph compile counters、Vulkan debug labels 和 delayed timestamp query readback 已接入 | `--bench-rendergraph` 可在 Release preset 下输出 warmup/frames/p50/p95/max 和 graph counters；frame/renderer smoke 验证 GPU label/timestamp counters；完整 editor performance panel 只保留技术约束，不进入当前里程碑 |
| 13 | 通用 RenderTarget / TextureView | 已在 `rhi-vulkan` 增加 `VulkanRenderTarget`、`VulkanSampledTextureView` 和 RT create/reuse/deferred deletion counters；`BasicFullscreenTextureRenderer` 的 offscreen viewport 已改为消费该通用 wrapper。后续再把 renderer-facing 命名从 offscreen viewport 收敛到 RenderView target | offscreen viewport smoke 验证 RT 独立尺寸、多帧复用、resize deferred deletion、sampled target 输出；后续 depth/MSAA/MRT 不需要推翻 API |
| 14 | RenderView target recording | 已在 fullscreen renderer 引入 `BasicRenderViewDesc` / `BasicRenderViewTarget` 和 `recordViewFrame()`；旧 `recordFrame()` 保留为 swapchain target 便捷包装，offscreen viewport 先复用同一路径写入 sampled target 再 composite 回 backbuffer；`BasicRenderViewDiagnostics` 已把编译后的 RenderGraph snapshot 挂到 successful view recording | 同一 renderer 路径可分别写 swapchain 和 offscreen target；graph pass/schema 复用，不复制 editor 专用渲染路径；diagnostics snapshot 不暴露 Vulkan handle |
| 15 | ImGui sampled texture contract | 不新增通用 `packages/ui` / `UiTextureHandle`；记录 ImGui Vulkan backend 的 texture registration 边界：renderer 输出 sampled target 的 image view、sampled layout、format 和 extent，editor ImGui integration 负责 `ImGui_ImplVulkan_AddTexture()` / `RemoveTexture()` 和 resize 后的 descriptor 更新 | 文档和审查确认 renderer、RenderGraph、RHI 均不依赖 ImGui；sample-viewer 继续只验证 offscreen sampled target 输出；阶段 16/17 再接真实 editor ImGui host |
| 16 | Editor app skeleton / ImGui shell | 已在 `apps/editor` 保持 host/integration 边界，把 app loop、ImGui runtime、dock shell、workspace layout preset、DockBuilder adapter、tool registry、panel registry、action registry、typed event queue 和 diagnostics history 拆成可维护模块；`packages/editor-core` 等 selection、transaction 逻辑等到真实 editor 状态模型出现再新增。详细切分见 `docs/planning/editor-development-plan.md` 的 16.x | editor app 能启动 ImGui shell；sample-viewer 不链接 editor 或 ImGui integration；runtime packages 不依赖 editor；`--smoke-editor-shell` 覆盖 shell、menu、workspace layout preset、tool contribution、panel registry、action registry 和 event queue |
| 17 | Editor viewport host | 17.1 已在 `apps/editor/src/editor_viewport.*` 增加 backend-neutral viewport request/result model；17.2 已把 ImGui sampled texture descriptor 注册/注销拆到 `apps/editor/src/imgui_texture_registry.*`，按 submitted/completed frame epoch 延迟 `RemoveTexture()`；17.3 已把 request 收集、RenderView 录制、viewport render target 生命周期和 registry 发布拆到 `apps/editor/src/editor_viewport_coordinator.*`；17.4/17.5 已新增 `--smoke-editor-viewport-resize`，真实缩小 editor window 后验证 resized texture 呈现、旧 descriptor retired 和旧 render target deferred destroy；17.6 已新增 `editor_input_router`，记录 ImGui capture flags、Scene View hover/focus 和 derived viewport/shortcut input flags；17.7 已新增 `editor_shortcut_router`，把 action shortcut metadata 转成 ImGui key chord，并通过 input snapshot gate 调用同一个 action registry。Scene View panel 只提交 `EditorViewportRequest`、上报 input state 并绘制上一帧完成的 texture。详细切分见 `docs/planning/editor-development-plan.md` 的 17.x | editor viewport 能显示通用 RT 输出；descriptor 注册/注销由 editor integration 管理并延迟释放；旧 viewport RT 通过 frame deferred destroy 释放；`--smoke-editor-shell` 验证 input snapshot 和 shortcut routing，`--smoke-editor-viewport` / `--smoke-editor-viewport-resize` 验证至少一次 viewport texture 呈现和 resize 后继续呈现 |
| 18 | RenderGraph buffer / storage / MRT | Stage 18.1 已落地后端无关 buffer handle、buffer desc、buffer access、import/transient buffer、buffer read/write slot、dependency sort、lifetime/final transition 计划和 debug table；Stage 18.2 已补 buffer usage / transition / `VkBufferMemoryBarrier2` adapter 映射；Stage 18.3 已补 `builtin.raster-mrt`、两个 named color slots 和 dynamic rendering multi-color attachment clear；Stage 18.4 已补 `StorageReadWrite(compute)` buffer access、schema validation、debug table 和 Vulkan stage/access 映射；后续子阶段再补 dynamic rendering pipeline desc 多 color format | `--smoke-rendergraph` 覆盖 buffer dependency、imported final state、missing producer、missing shader stage、zero-size buffer、storage read/write、dispatch command summary、buffer Vulkan adapter 字段和 MRT builtin schema 负向路径；`--smoke-mrt` 验证两张 transient color attachments 的真实 Vulkan dynamic rendering clear 和 pool reuse |
| 19 | Compute dispatch baseline | Stage 19.1 已补 `Dispatch` command summary 与 `builtin.compute-dispatch` schema；Stage 19.2 已记录 graphics queue compute capability，并新增 compute pipeline wrapper、storage descriptor、RenderGraph buffer transition 录制、`vkCmdDispatch` 和 readback smoke | `--smoke-rendergraph` 覆盖 compile-time compute dispatch 语义；`--smoke-compute-dispatch` 覆盖真实 compute shader、storage buffer descriptor、dispatch 和 GPU 写入 readback |
| 20 | Scene object / selection baseline | 20.1 已在 `packages/scene-core` 落地 runtime `EntityId`（index + generation）、`World` create/destroy/isAlive、local `TransformComponent` 和 CPU smoke；后续 20.2/20.3 再建立 `editor-core` selection model；renderer 只消费 frame snapshot 或 draw packet，不捕获 editor object 指针 | `asharia-scene-core-smoke-tests` 覆盖 entity lifetime、旧 id 拒绝和 transform 移动不改变 id；selection 后续可在 editor-core 中独立测试；Scene View / Game View 对象数据边界清楚 |
| 21 | Gizmo / grid / debug draw | 21.1 已在 editor viewport request/result、coordinator 和 ImGui texture metadata 中加入 Scene/debug viewport flags，并在 coordinator 层清空 Game/Preview 的 Scene-only authoring flags；Game View 可保留显式 debug overlay/debug gizmo intent。21.2 已完成 editor overlay metadata 闭环：Scene View flagged render 会通过 ImGui texture result 被 panel 消费，并且 recorded RenderView diagnostics 可供 RG View / Frame Debug 消费。21.3 已新增 `EditorFrameDebugger` capture/pause/resume 状态机，WaitingGpuFence/PausedFrameDebug 会暂停新的 RenderView recording，并通过 `EditorInspectedWorldScheduler` 的 counter-based seam gate 被检查 world 的 frame advance、game update 和 script update safe points。21.4 已新增只读 `RenderGraphPanel` 初版。21.5 已新增 editor-owned refresh policy，Scene View 默认 on-demand 复用上一张 texture，resize/overlay/debug event/AlwaysRefresh 等 repaint reason 才触发新的 RenderView recording。21.6 已把 Live RG View 和 FrameDebuggerPanel 内 RenderGraph view 的所有权拆清：Live RG View 属于 viewport/render diagnostics，读取最近一次 RenderView compile snapshot；FrameDebuggerPanel 在同一面板内提供 Frame 和 RenderGraph 两个切换视图，Frame 视图按左 pass/execution event、右详情/预览组织，RenderGraph 视图读取 frozen capture snapshot；两个 RenderGraph 入口复用同一个 read-only snapshot renderer。21.7/21.8 已补 Frame Debug image preview copy 和 pass-level replay contract，并把 pass/event 预览推进为选中 RenderView pass 之后的 replay copy；graph-local resource override 仍可按最终资源图预览。21.9 已补 renderer-owned RenderView view/camera/frame/overlay prerequisites；Scene View request 现在携带 editor-owned navigation/camera context（所有权在 editor viewport，不是 renderer 矩阵旁路），并由 coordinator bridge 到 `BasicRenderViewCamera`；另已把工具注册表中的 Scene View overlay intent 暴露为视口内 grid/gizmo/selection-outline strip。21.10 已把 interim grid data bridge 收敛进 renderer-owned world-grid intent：默认 Scene View grid 进入 `builtin.render-view-world-grid`，`EditorViewportOverlayProvider` v0 保留 debug-line packet route，`EditorViewportCoordinator` 只负责 bridge 成 `BasicDebugWorldLine` 并交给 `BasicRenderViewOverlayDesc`。21.11 已补 Scene View panel-owned camera state 和 viewport unproject v0：resize 后重算 camera projection，`unprojectEditorViewportPoint()` 用 inverse view-projection 把 viewport-local pixel 映射到 near/far world ray。21.12 已补 `builtin.render-view-overlay` 输入 pass：存在 debug world-line 数据时，RenderView 会把 camera position / near plane、frame params 和 debug world-line count 作为 typed params 与 command summary 进入 RenderGraph；没有 debug line 时 overlay intent 只保留在 diagnostics，避免空 pass。21.13 已补 Scene View orbit/pan/dolly 输入并通过 `CameraInputChanged` repaint reason 触发新的 RenderView recording。21.14 已补 renderer-owned debug-line overlay draw：`renderer_basic_vulkan` 在 overlay pass 内把 `BasicDebugWorldLine` 投影为 line-list vertex buffer，并用 debug-line shader/pipeline 绘制可见 world/debug lines。21.15 已补 camera-aware grid 与 `--smoke-render-view-grid-readback` 像素/readback camera smoke。21.16 已补 source overlay id diagnostics，并把 world-grid LOD 改为 RenderView policy 只按 camera 到 grid plane 的垂直距离计算整帧统一的 `GridLodSettings`，低高度锁定 base spacing，拉高后在 1/2/5/10 spacing 间平滑切换、默认无距离淡出，readback smoke 覆盖近距离 base LOD command、低高度远水平距离仍保持 base LOD 和高视角可见性。21.17 已补 captured world-grid desc diagnostics，Frame Debug replay 使用 capture 中的 grid 参数而不是硬编码默认值。21.18 已补 `EditorSettings::sceneGrid` 数据合同，Scene View panel 读取 settings 并通过 `EditorViewportRequest::worldGrid` 传给 coordinator，最终进入 `BasicRenderViewOverlayDesc::worldGrid`。21.19 已让 Scene grid overlay contribution 声明 built-in 默认 world-grid settings，settings bootstrap 使用同一默认值处理新 settings 和旧 settings 缺省迁移；21.20 已把 grid color 纳入 `sceneGrid` settings、RenderView `GridColor` command、world-grid shader 和 Editor Settings 最小颜色控件；21.21 已把 Editor Settings 扩为左侧目录/右侧内容结构，并补 Scene Grid 的 plane、minor/major spacing、fade、opacity 和 color 控件；后续先定义脚本/插件系统扩展边界，再接真实 external manifest/reload path。 | `--smoke-editor-viewport` 验证 Scene View flag defaults、Scene flags 保留、Game 清空 Scene-only authoring flags 但保留 explicit debug flags、Preview effective flags 为空、overlay provider metadata、world-grid/source overlay diagnostics、自定义 Scene View grid request 进入 RenderView diagnostics、Game View 不接收 Scene-only packet、camera diagnostics、center unproject ray、near-plane origin、corner orientation、invalid matrix rejection、resize aspect handling、至少一帧 flagged Scene View render、至少一次 flagged texture acquisition、RenderView diagnostics counts、renderer execution event counts、debug-world-line count、默认 Scene View 不产生空 overlay pass、idle Scene View 复用上一张 texture，以及 Live RG View 无需 capture 也能消费 compile snapshot；`--smoke-fullscreen-texture` 验证存在 debug world line 时保留 graph-visible overlay pass 和 `DrawDebugWorldLines` execution event；`--smoke-render-view-grid-readback` 验证四组 camera 的 Scene View grid diagnostics、world-grid pass、`DrawWorldGrid` event、近/斜视角 base LOD command、低高度远水平距离仍保持 base LOD、高视角离开 base LOD、像素 spread、camera-difference 与高视角可见性；`--smoke-editor-shell` 验证 Scene View grid settings 可保存、加载和恢复，并验证 Scene grid overlay contribution 默认值与 settings bootstrap 默认值一致；`--smoke-editor-viewport-resize` 验证 resize repaint；`--smoke-editor-frame-debugger` 验证 capture、fence wait、暂停 RenderView recording、inspected-world safe-point gate、FrameDebuggerPanel execution-event selection、FrameDebuggerPanel RenderGraph tab frozen snapshot consumption、world-grid desc/source overlay id capture、resume、debug repaint、replay 同帧多 RenderView 录制、选中 renderer execution event 不恢复 normal RenderView recording，并验证 preview copy 发生在选中 pass 之后；后续 Scene View graph 可包含 Scene-only provider-driven authoring pass，Game View graph 只包含显式 opt-in 的 debug pass |
> 2026-06-01 update: 21.15 已补 renderer-owned fullscreen world-grid overlay pass；Scene View grid intent 通过 `BasicRenderViewOverlayDesc::worldGrid` 进入 `builtin.render-view-world-grid`，shader 用 inverse view-projection / optional fade / LOD / color push constants 绘制 XZ world grid。2026-06-02 update: world-grid LOD 已收敛到 RenderView policy：只按 camera 到 grid plane 垂直距离生成整帧统一的 `GridLodSettings`，低高度锁定 base spacing，低高度远水平距离仍保持 base LOD，拉高后在 1/2/5/10 spacing 间平滑切换，默认 `fadeStart == fadeEnd == 0` 不做距离淡出；`--smoke-render-view-grid-readback` 已验证近距离/低高度远水平距离 base LOD command、第三组高视角 camera 像素 spread 与 camera-difference；同日已补 world-grid desc/source overlay id diagnostics，Frame Debug replay 使用 captured grid 参数。Scene View grid spacing/fade/opacity/color 已进入 editor user settings 到 RenderView 的数据路径，Scene grid overlay contribution 已声明同一份 built-in 默认值，Editor Settings 已有左侧目录/右侧内容的 grid settings v0；后续 grid 扩展先收敛脚本/插件系统边界，再接真实 external manifest/reload path。

| 22 | Asset-core + asset-pipeline + resource upload baseline | `asset-core` 拥有 GUID、source metadata、import settings、product key/cache key 和 dependency 数据模型；`.ameta` deterministic metadata IO 已落在可选 `asharia::asset_core_io` target，`asset_core` identity/catalog 仍只依赖 core；`project-core` 拥有最小 `asharia.project.json` descriptor，只记录 project identity、asset source roots、asset cache root policy 和 discovery ignore policy，不记录 target profiles / asset profiles / package settings；canonical `sourcePath` 合同已收敛为 project-relative generic path；`asset-pipeline` 已落地 deterministic source tree scan baseline、显式 source/.ameta metadata discovery baseline、显式 source file snapshot/hash baseline、product manifest IO baseline、import planning baseline、scan-to-planning bridge baseline 和 deterministic product execution baseline；`tools/asset-processor` 已有 read-only dry-run CLI 报告、`--project` asset source discovery 输入和受控 `execute` product output；renderer/RHI 侧已补第一条 graph-visible buffer upload copy baseline，后续再推进真实 importer、dependency invalidation、linear upload allocator、texture upload 和真实 mesh resource | `asharia-asset-core-smoke-tests` 已覆盖 `.ameta` round-trip / malformed input / settings hash mismatch / 非规范 sourcePath；`asharia-project-core-smoke-tests` 覆盖 project descriptor round-trip / malformed input / duplicate/missing fields / invalid project id / invalid source root / duplicate root negative paths；`asharia-asset-pipeline-smoke-tests` 覆盖 source tree scan determinism/missing/orphan/invalid diagnostics、scan-to-planning bridge request/cache-hit/staged diagnostic paths、discovery round-trip、source snapshot determinism/content change、product manifest IO round-trip/malformed/duplicate/mismatch negative paths、import planning hit/miss/change/diagnostic paths、deterministic product execution、source-bytes/hash mismatch diagnostic、missing/malformed metadata、source path mismatch、duplicate GUID/path、entry/metadata 非规范 sourcePath、缺失/非普通 source file 和 duplicate snapshot source path；`asharia-asset-processor --smoke-dry-run` 覆盖 dry-run request/cache-hit 报告、scan diagnostic、project descriptor input 和 product manifest read diagnostic；`asharia-asset-processor --smoke-product-execution` 覆盖 product blob/manifest 写入、unchanged-input cache hit rerun 和 source bytes change 后的新 product 输出；`--smoke-buffer-upload` 覆盖显式 upload payload 经 RenderGraph `CopyBuffer` 到 device-local buffer 再读回；后续补 `--smoke-mesh-resource`、`--smoke-texture-upload`；runtime 不直接依赖 source asset 路径；editor 文件变化不直接落在 UI、`asset-core` 或 `project-core` |
| 23 | Material / pipeline key | `packages/material-core` 已启动 CPU-only material resource signature、descriptor contract、shader/signature compatibility validation 和 pipeline key hash；renderer feature 层后续消费 pipeline key、pipeline layout cache 和 descriptor mismatch 诊断 | `asharia-material-core-smoke-tests` 覆盖 deterministic signature hash、duplicate/missing/invalid binding negative paths、material/shader signature mismatch 和 pipeline key invalid render-state/hash paths；fullscreen/draw-list 后续不再依赖硬编码 descriptor binding 假设 |
| 24 | Asset browser / material editor | Asset Browser shell 已使用 editor-owned Lucide icon ids 和 `EditorAssetIconRegistry` custom resolver 合同；`editor_asset_catalog` 已把 project/source/metadata/product manifest snapshot 路由进 `EditorAssetCatalogStore`，panel 只消费 `AssetCatalogView` 和 diagnostics，并已有本地只读选择、状态摘要、selected asset details、sourcePath 派生 folder scope/breadcrumbs、assetTypeName 派生 type filter、productState 派生 state filter 和本地 sortable catalog rows，不直接扫描、导入、写 cache 或加载 runtime/GPU 资源。后续继续把 material editor 接到 asset/material public API | `--smoke-editor-asset-browser` 覆盖 snapshot-backed catalog startup；startup smoke 覆盖 product manifest、missing-product diagnostic、project diagnostic 和 fixture/snapshot store selection；editor 后续能创建/编辑一个 material；runtime 同样能独立报告 material/descriptor 错误 |
| 25 | Lighting baseline | renderer feature 层优先做 MRT/G-buffer deferred MVP：G-buffer、depth、lighting fullscreen/compute pass、HDR scene color；scene 只提供 light snapshot | `--smoke-gbuffer`、`--smoke-lighting`；至少一个动态 light 输出 HDR scene color |
| 26 | Scene/world persistence | `scene-core` + serialization 拥有最小 scene file、entity hierarchy、mesh renderer、camera/light component、save/load；editor 修改走 transaction | editor 能保存/加载最小 scene；viewport 显示多个 mesh object |
| 27 | Postprocess / temporal | renderer feature 层负责 HDR tone mapping、bloom MVP、history texture/ping-pong RT、per-view frame params、resize/history invalidation | `--smoke-postprocess`、`--smoke-history-resource` |
| 28 | Play Session / diagnostics | app/editor/scene 层拥有 Edit Mode / Play Mode 状态机；Game View 使用 runtime world copy 或 snapshot；render/profiling 只按 view 输出 graph pass/counter/timestamp | 进入/退出 Play 不污染编辑场景；Game View 和 Scene View 可同帧共存 |

## 防过度设计执行规则

优秀案例用于校准边界，不作为一次性照搬目标。后续每个阶段按下面规则裁剪：

- 只有能被当前 smoke、benchmark 或 validation 证明的问题，才进入实现。
- 每个阶段必须能独立提交、独立回退、独立验收。
- 先做最小数据模型，再做后端实现，最后才做 UI 或工具体验。
- cache/pool/deferred lifetime 必须带 counter，否则不进入主线。
- 完整 editor 产品功能、完整 asset database、script VM、bindless、async compute 和 transient memory alias 必须保持在暂缓项，直到 RT / RenderView / ImGui texture registration 这些前置小闭环已经稳定。
- 文档可以记录未来技术细节，但代码不为尚未纳入计划的产品功能创建长期维护路径。

## RenderGraph 脚本前置原则

当前不接入脚本系统，但从现在开始避免写出未来脚本难以映射的 C++ API：

- 脚本/工具未来应运行在 build/record 阶段，用普通控制流创建资源、pass、参数和受控命令；脚本函数本身不保存进 compiled graph。
- RenderGraph compile 只依赖显式声明的 resource access、pass type、params、schema 和受控 command summary；任意脚本闭包或 native callback 都视为不可分析黑盒。
- 后端执行阶段不调用脚本语言 VM；它消费 compiled graph、barrier plan、descriptor plan 和受控 command context 产物。
- 如果未来提供 unsafe/native pass，必须显式标记并降低优化假设；unsafe pass 不参与 aggressive alias、merge、reorder 等强优化，不能作为普通 pass 的默认路径。
- `pass.type` 不等同于 RenderQueue 或 shader tag；它表示执行模型或 typed pass key。RenderQueue 和 shader pass tag 等到 mesh/material 阶段再引入。
- `pass.type` 命名优先使用执行模型，例如 `builtin.transfer-clear`、`builtin.raster-fullscreen`、
  `builtin.raster-draw-list`、`builtin.compute-dispatch`；业务语义放在 `pass.name`、params 或后续 feature
  层，避免退化成大量不可优化的业务字符串 executor。
- command context skeleton 第一版仍主要作为 debug IR/summary；schema 已能限制 pass 允许的 command kind，
  `--smoke-fullscreen-texture` 已验证 `setTexture` / fullscreen draw 的最小 Vulkan 路径，并开始从 command
  summary 派生当前 fullscreen pipeline key；clear color 和 fullscreen tint 已通过 typed POD params payload
  进入 pass context。

近期 API 形态应先在 C++ 中验证，例如：

```cpp
graph.addPass("ClearColor", "builtin.transfer-clear")
    .writeTransfer("target", backbuffer)
    .setParams(ClearTransferParams{.color = clearColor});
```

后续 fullscreen/后处理方向可以扩展为：

```cpp
graph.addRasterPass("BloomPrefilter")
    .readTexture("source", sceneColor)
    .writeColor("target", bloomHalf)
    .record([](RenderGraphCommandList& cmd) {
        cmd.setShader("Hidden/Bloom", "Prefilter");
        cmd.setTexture("SourceTex", "source");
        cmd.setFloat("Threshold", 1.2F);
        cmd.drawFullscreenTriangle();
    });
```

第一版 compiler 不需要理解每条命令的全部语义，只要求资源访问已在 pass builder 上显式声明；
命令列表用于后续 pipeline/descriptor/debug 规划，并避免脚本直接触碰 Vulkan API。当前已有最小
fullscreen texture 真实执行 smoke；通用执行仍应等 pipeline key、typed params、resource state 和
Vulkan adapter 边界收紧后再扩大。

## RenderGraph 编译性能原则

RenderGraph 的 record/compile 路径需要按“可每帧运行”的标准设计。动态后处理、技能特效、
调试视图、相机差异和质量开关都可能让当前帧 graph topology 变化；图的价值正是让这些变化在
执行前变成可分析的 pass/resource 计划。

每帧流程拆成四段：

1. **RecordGraph**：根据 camera、quality、debug、演出/技能状态和 active predicate，创建当前帧
   resource handle、pass、named slots、typed params 和 command summary。这里可以使用普通 C++ 控制流；
   未来脚本也只应运行在这一段。
2. **CompileGraph**：校验 schema 和 resource access，剔除无用 pass，计算依赖、lifetime、resource
   state、barrier/layout plan、transient allocation plan 和 debug/profiling 表。该阶段只产生计划，不创建
   长期 GPU 对象。
3. **PrepareBackend**：根据 compiled graph 从 cache/pool 取得或创建真实 backend 对象，例如 transient
   image/buffer、descriptor set、pipeline layout、pipeline 和 per-frame parameter buffer。
4. **RecordCommands / Execute**：按 compiled pass 顺序录制 Vulkan command buffer，提交并 present。这里不再
   改 graph topology，也不调用脚本 VM。

每帧 compile 可以做的轻量工作：

- 根据当前设置、演出状态和 feature active predicate 生成 pass/resource 声明。
- 校验 pass type、schema、named resource slots、typed params 和显式 resource access。
- 计算 pass 依赖、resource lifetime、final transition 和 barrier/layout plan。
- 生成 transient resource allocation plan、command summary、debug table 和 profiling label。

每帧 compile 不应该做的重型工作：

- shader 编译、shader reflection 解析或磁盘 IO。
- descriptor set layout、pipeline layout、graphics/compute pipeline 的重复创建。
- 长期 GPU resource 创建、VMA allocation churn 或大量 heap allocation。
- 脚本源码/字节码编译、任意脚本 VM 执行期回调或 native black-box 分支解析。

因此后续需要逐步形成这些缓存/池：

- `ShaderCache`：key 至少包含 shader asset/path、entry point、stage/profile、target、compiler/toolchain
  version、defines 和 source/reflection 版本。value 包含 SPIR-V、shader module、reflection model。
- `PipelineLayoutCache`：key 来自合并后的 descriptor set/binding、descriptor type/count、stage visibility
  和 push constant ranges。value 包含 descriptor set layouts 和 pipeline layout。
- `PipelineCache`：key 至少包含 shader pass ids、pipeline layout signature、render target formats、sample
  count、depth/stencil state、blend state、primitive topology、vertex input layout 和 dynamic state flags。
  Vulkan 的 `VkPipelineCache` 可作为 backend 的复用机制之一。
- `DescriptorAllocator`：按 frame 或 per-flight frame arena 分配 descriptor set，key 为 descriptor set layout；
  在 GPU fence 确认后回收/重置，避免每 pass 创建 descriptor pool/layout。
- `TransientResourcePool`：按 compiled lifetime plan 分配/复用 image/buffer。image key 至少包含 format、
  extent、mip/layer、sample count、usage flags 和 debug name class；buffer key 至少包含 size、usage 和
  memory domain。Vulkan 侧可用 VMA `VMA_MEMORY_USAGE_AUTO` 选择 memory type。
- `GraphTemplateCache`：暂缓；只有当 topology 稳定且 compile 成本成为瓶颈时，再按 active feature set、
  render scale、formats、quality level、injection point 和 pass topology 复用拓扑排序、schema validation
  和部分 lifetime plan。

`PassSchema` 第一版建议字段：

```cpp
struct RenderGraphPassSchema {
    std::string_view type;
    RenderGraphQueueDomain queueDomain;
    std::span<const ResourceSlotSchema> requiredReads;
    std::span<const ResourceSlotSchema> requiredWrites;
    std::span<const ResourceSlotSchema> optionalReads;
    std::span<const ResourceSlotSchema> optionalWrites;
    PassParamsTypeId paramsType;
    std::span<const RenderGraphCommandKind> allowedCommands;
    bool allowCulling;
    bool hasSideEffects;
};
```

slot schema 至少需要描述：

- slot 名称，例如 `source`、`target`、`depth`、`objects`、`visibleList`。
- resource kind，例如 texture/image、buffer、renderer/draw list。
- abstract access，例如 `ShaderRead(fragment/compute/etc.)`、`ColorAttachmentWrite`、
  `DepthAttachmentRead`、`DepthAttachmentWrite`、`DepthSampledRead`、`TransferDst`、
  `StorageReadWrite`。depth 作为 attachment 读写与 depth texture 采样必须分开建模，避免混用
  layout/stage/access。
- 同图 read/write 必须用明确 access 表达：depth/stencil attachment read-write、color blend/load、
  storage image/buffer read-write、framebuffer fetch/input attachment、grab/copy-to-temp 或 unsafe/native
  pass。普通 `readTexture + writeColor` 不能作为通用 read-write 表达。
- 是否允许 transient、imported、persistent resource。
- 是否允许多个绑定，例如 MRT color slots。

command summary 第一版建议只保存数据，不保存脚本函数或裸指针：

- `SetShader(shaderAsset, shaderPass)`：后续生成 pipeline key。
- `SetTexture(bindingName, slotName)`：`slotName` 必须引用 pass builder 已声明的 read/write slot。
- `SetFloat/SetInt/SetVec4(bindingName, valueOrParamHandle)`：值可以是当前帧 literal，也可以是
  `FrameParamHandle`、`MaterialParamHandle` 等参数句柄。
- `DrawFullscreenTriangle()`：只允许在 `builtin.raster-fullscreen` 一类 schema 中出现。
- `ClearColor(slotName, color)`：只允许在 transfer/raster clear 类型中出现。

动态参数通信规则：

- 初期允许每帧 RecordGraph，把脚本/设置系统当前值复制为 command summary literal。
- 后续为频繁变化的参数引入 `FrameParamTable`、`MaterialParamTable` 或 `PostParamBlock`；compiled graph
  保存参数句柄，PrepareBackend/RecordCommands 阶段把当前值打包到 push constants、uniform buffer 或
  descriptor。
- 后端执行阶段不得回调脚本获取参数；参数必须在 RecordGraph 或 PrepareBackend 前进入参数表。

动态 feature 的建议策略：

- 轻量、常驻且需要连续演出混合的效果可以固定在 graph 中，用参数控制强度，例如 vignette、
  color grading、exposure、debug overlay。
- 昂贵或需要额外 RT/buffer 的效果应由 active predicate 控制是否 record，例如 bloom、DOF、
  SSAO、SSR、motion blur、技能 mask composite。
- 技能/演出类临时效果在淡入、激活、淡出期间 record pass；淡出结束且权重低于阈值若干帧后移除。
- 为避免首次触发 hitch，技能或后处理 feature 可以预热 shader、reflection、pipeline layout 和
  pipeline cache，但不需要长期执行对应 pass 或常驻 transient RT。

示例：黑白闪技能可以在激活期间临时加入 `SkillFlashMask` draw-list pass 和
`SkillBlackWhiteFlash` fullscreen composite pass；mask RT 是 transient image，`weight`、`phase`
和受影响对象列表作为每帧参数/输入更新。技能未激活时，这些 pass 和 mask RT 不进入 graph。

这类 feature 可以拆成：

- `SkillFlashState`：由 gameplay/timeline 更新，包含 `active`、`weight`、`phase`、`affectedObjects`、
  `cooldownFrames`。
- `SkillFlashMask`：`type = builtin.raster-draw-list`，写 `mask` transient RT，draw list 由
  `affectedObjects` 生成，shader tag 后续可为 `MaskOnly`。
- `SkillBlackWhiteFlash`：`type = builtin.raster-fullscreen`，读 `sceneColor` 和 `mask`，写 post target，
  参数为 `weight`、`phase`。
- `active predicate`：`active || weight > epsilon || cooldownFrames > 0`；predicate 为 false 时不 record
  这两个 pass，mask RT 也不会进入 transient allocation plan。

## 多视图 / 多相机原则

Unity SRP 在 Editor 中会为可见 Game View、Scene View、preview camera 等 view/camera 调用渲染；URP
也会处理 game camera、Scene view camera、reflection probe 和 inspector preview 等不同 camera。
Asharia Engine 的 editor viewport 从通用 RenderView 稳定后进入主线；RenderGraph 和 profiling 的技术设计不应假设一帧只有一个 RenderGraph。

建议模型：

```cpp
enum class RenderViewKind {
    Game,
    Scene,
    Preview,
    ReflectionProbe,
};

struct RenderView {
    RenderViewKind kind;
    CameraData camera;
    RenderTarget target;
    RenderViewFlags flags;
};
```

每帧流程：

```cpp
for (const RenderView& view : frameViews) {
    RenderGraph graph;
    renderer.recordViewGraph(graph, view);
    RenderGraphCompileResult compiled = graph.compile();
    backend.prepareAndRecord(compiled, view);
}
```

未来 Game View 和 Scene/debug/Preview view 共享 renderer package、RenderGraph、Vulkan backend、shader/pipeline/descriptor
cache 和 resource pools，但 graph topology 可以不同：

- Game View 通常使用游戏 camera、game volume/postprocess、UI/camera stack，输出到 swapchain 或 game
  viewport texture。
- Scene/debug view 使用独立 camera，可能关闭或替换部分 game postprocess，并额外加入 grid、gizmos、
  selection outline、wire overlay、debug overlay 等未来 editor/debug pass。
- Preview View / asset inspector 只作为后续技术约束记录；它可以使用更小的 target 和专用 lighting/背景 pass，但仍复用 backend caches。

工程约束：

- RenderGraph resource handle 只在单个 graph/view 内有效；跨 view 共享的 GPU resource 必须由 resource
  manager 拥有，再作为 imported resource 进入各自 graph。
- transient resources 默认只在单个 graph/view 内分配和复用；跨 view alias 以后再考虑。
- pipeline、shader、descriptor layout 等 cache 应跨 view 共享；per-view dynamic params、camera buffer、
  descriptor sets 和 transient resources 按 view/frame 隔离。
- Scene/debug view 的专用 pass 必须有独立 feature flag，不能污染 Game View graph。
- 编辑器性能面板、EditorHost profiler 和 asset/inspector 性能分析晚于 editor viewport、scene/object 和 asset/material 小闭环；相关约束记录在 `docs/systems/performance-profiling.md`。
- 未来如支持 camera stacking，camera stack 是单个 view 内的多 camera composition；它不同于
  Scene/Game/Preview 这些 view target。

## 阶段 1 状态

Slang reflection 基线已接入，原因是它对后续 descriptor、pipeline layout、material、mesh
输入都提供契约，而且不会扩大 RenderGraph/Vulkan resource 生命周期风险。

当前范围：

- 新增 `packages/shader-slang/tools/slang_reflect.cpp`，使用 Slang API 读取 shader。
- 输入继续沿用 `*.shader.json`：source、entry、stage、profile、target。
- 输出 `*.reflection.json`：entry、stage、vertex inputs、descriptor bindings、push constants、
  compiler version、source path。
- `asharia_add_slang_shader()` 增加可选 `REFLECTION_OUTPUT`，和当前 `METADATA_OUTPUT` 并行。
- 暂不自动生成 C++ 代码，先作为构建产物和校验输入。

验收：

- `basic_triangle.vert.reflection.json` 和 `basic_triangle.frag.reflection.json` 可重复生成。
- `--smoke-triangle` 继续通过。
- 文档更新 `docs/architecture/flow.md`、`docs/workflow/technical-stack.md` 和本文件。

## 阶段 2 状态

Descriptor/Layout 契约已开始消费 reflection JSON。当前实现会在
`BasicTriangleRenderer::create()` 期间校验 triangle shader 契约，并把合并后的
`ShaderResourceSignature` 映射为固定 Vulkan descriptor set layout / push constant ranges，
再创建 pipeline layout。当前 triangle signature 仍为空，因此尚未引入 descriptor set 绑定行为。

当前范围：

- `packages/shader-slang` 提供 `readShaderReflection()`，读取 entry、stage、vertex inputs、
  descriptor binding 数量和 push constant 数量。
- `renderer-basic-vulkan` 校验 triangle vertex shader 的 `POSITION0` 和 `COLOR0` 输入。
- `renderer-basic-vulkan` 校验 triangle vertex/fragment shader 当前没有 descriptor binding 和
  push constant。
- `shader-slang` 可合并 shader reflection 得到 resource signature 明细；descriptor binding
  按 `(set,binding)` 合并，push constant 按 `(offset,size)` 合并，并保留 stage visibility。
- `rhi-vulkan` 提供 `VulkanDescriptorSetLayout` RAII wrapper。
- `VulkanPipelineLayoutDesc` 可接收 descriptor set layouts 和 push constant ranges，triangle
  renderer 通过 reflection-derived signature 创建 pipeline layout。
- `--smoke-descriptor-layout` 已验证非空 descriptor signature：`descriptor_layout.slang`
  反射出 set 0 / binding 0 / `constantBuffer`，并能创建固定 descriptor set layout
  和 pipeline layout。
- 缺失 reflection JSON 或字段不匹配会返回 `ErrorDomain::Shader`，并带具体字段上下文。

后续范围：

- descriptor pool / descriptor set allocation / uniform-buffer / sampled-image / sampler write 已有
  最小 RHI wrapper；descriptor bind、fullscreen texture、draw list smoke 和 dependency-sorted
  RenderGraph smoke 已接入。进入 material/resource binding 前，下一步先补 pipeline/layout/descriptor
  cache 与 deferred destruction。
- RenderGraph pass 已拥有 named resource slots 和 typed params；后续 fullscreen/postprocess/draw-list
  继续沿用这套声明模型，避免依赖位置参数或 ad hoc callback capture。
- `ShaderRead`、depth attachment read/write 和 sampled depth state 已扩展到抽象 state 与 Vulkan
  access/layout 翻译；后续新增采样或 depth 路径时继续校验 image usage flags 与目标 layout/access 匹配。
- Unity/RDG 风格的 read-write 展示不能直接映射为模糊同图 read/write；Asharia Engine 需要先定义具体 combined-access state，再为它补 schema、usage、layout/access、feature query 和 smoke。
- 在 material/resource binding 路线稳定前，继续暂缓 bindless 和自动 C++ codegen。

## 暂缓事项

- 暂缓 bindless/descriptor indexing，等固定 descriptor 契约稳定后再进入。
- 暂缓完整 editor 产品化、完整 asset database 和 package registry；editor skeleton、UI shell 和 viewport 按本文阶段进入。
- 暂缓脚本 VM 接入；当前只做脚本可映射的 C++ builder、typed params 和受控 command context。
- 暂缓完整 Unity SRP 风格的 RenderPipelineAsset、RendererFeature、RendererList 和 ShaderLab metadata。
- 暂缓 glTF/mesh importer，先做最小 mesh buffer 和 index buffer。
- 暂缓多 queue/async transfer，当前仍保持 single graphics queue，降低同步复杂度。
- 暂缓 editor performance panel、graph visualizer、GPU timestamp 全套 UI 和自动 capture 工作流；当前只做性能数据底座。
