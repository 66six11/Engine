# 架构问题记录与解决方案

记录日期: 2026-05-25
更新日期: 2026-05-27

本文基于 `docs/planning/next-development-plan.md` 的 A-F 门禁和全量代码审查，按层级归类所有已知架构缺陷，并针对每个问题提供根因分析、行业参考和具体可执行方案。已修复项标注 `Fixed` 并附修复摘要; 未修复项标注 `Open` 并按确定性推进顺序排列。

## A-F 门禁当前状态

| 门禁 | 问题 | 状态 | 完成日期 |
|------|------|------|---------|
| **A** | Format / capability contract | ✓ Fixed | 2026-05-23 |
| **B** | RenderView camera → GPU contract | ◐ Partially Fixed (B.2+B.3 renderer slice) | 2026-05-25 |
| **C** | Multi-view request model | ✓ Fixed | 2026-05-23 |
| **D** | Graph-visible GPU work | ✓ Fixed | 2026-05-25 |
| **E** | Editor state and command model | ◐ Partially Fixed (Step 1+2a+2b-a+2b-b+2b-c+3) | 2026-05-27 |
| **F** | RenderGraph API / implementation split | ◐ Partially Fixed (Phase 1+2+3+4-A+4-B+4-C) | 2026-05-27 |

---

## 1. 渲染层 (RenderGraph / RHI / Renderer)

---

### 1.1 [Fixed] Format / Capability Contract 不闭合

**优先级**: P2-A | **状态**: Fixed (2026-05-23)

**根因**:
`vulkan_frame_loop.cpp` 的 `chooseSurfaceFormat()` 优先选择 `VK_FORMAT_B8G8R8A8_SRGB`，否则 fallback 到 `formats.front()`。但 `basicRenderGraphImageFormat()` 原来只映射 SRGB 这一种 format，其他格式变成 `RenderGraphImageFormat::Undefined`。swapchain 能创建，但 RenderGraph 侧拿到 Undefined 格式，错误延迟到后续录制路径才暴露。

**行业参考**:
- Unity: 所有 `GraphicsFormat` 在管线入口有完整的 `SystemInfo.IsFormatSupported` 检查
- Vulkan Guide: `VkFormat` 是资源创建、view、attachment 和 descriptor 的基础合同，必须 fail-early

**修复方案** (已完成):
1. `basicRenderGraphImageFormat()` 返回 `Result<RenderGraphImageFormat>`，unsupported format 时返回错误
2. 所有 renderer 入口 (triangle/mesh3D/draw-list/fullscreen/MRT/compute) 传播 unsupported format 错误
3. `--smoke-renderer-format-contract` 验证 unsupported `VK_FORMAT_R8G8B8A8_UNORM` 在 graph import 前失败

**后续要求**:
- 新增 swapchain/offscreen/material format 时同步扩展 helper、Vulkan adapter 映射和 negative smoke
- 不能重新引入 `Undefined` fallback

---

### 1.2 [Partially Fixed] RenderView Camera 数据已进入 GPU Overlay Path

**优先级**: P2-B | **状态**: Partially Fixed (2026-05-26)

**根因**:
`EditorViewportCoordinator` 已经把 camera 数据桥接到 `BasicRenderViewOverlayDesc`，overlay enabled 时会插入 `builtin.render-view-overlay` pass。但 `BasicMesh3DRenderer` 等 sample renderer 在 renderer 内部用 `basicMesh3DProjectionMatrix(extent)` 和硬编码的 `basicMesh3DModelMatrix()` 计算 MVP，不从 `BasicRenderViewCamera` 读取。

**已修复 (B.2+B.3 renderer slice)**:
1. `BasicMesh3DRendererDesc` 新增 `camera` + `useRenderViewCamera` 字段
2. `BasicMesh3DRenderer` 存储 camera，`recordFrame` 在 camera 模式用 `camera.viewProjection * modelMatrix` 替代硬编码投影
3. 新增 `basicMesh3DPushConstants(camera, model)` 重载
4. `recordMesh3DDraw` 接受 optional camera 指针
5. `builtin.render-view-overlay` pass 在存在 `BasicDebugWorldLine` 时创建 renderer-owned debug-line pipeline，以 `VK_PRIMITIVE_TOPOLOGY_LINE_LIST` 绘制 Scene View grid/world lines。
6. `BasicFullscreenTextureRenderer` 为 debug-line vertex upload 使用 per-frame buffer ring，避免同一帧多 RenderView / Frame Debug replay 复用正在录制的上传缓冲。
7. `--smoke-editor-viewport` 验证 Scene View overlay pass 产生 `DrawDebugWorldLines` execution event，且 vertex count 等于 debug-world-line count × 2。

**待完成**:
- Camera-aware grid range/fade policy；当前 grid provider 仍输出原点附近固定 XZ line packet。
- Pixel/readback 级 GPU smoke 验证不同 camera position 产生不同渲染输出；当前 smoke 覆盖 graph pass、draw event、vertex count 和 Vulkan validation。

**行业参考**:
- Unity: `UNITY_MATRIX_V`、`UNITY_MATRIX_P`、`_WorldSpaceCameraPos` 等 built-in shader variables，通过 `PerCamera` 或 `PerView` constant buffer 绑定。Unreal: `FSceneView::ViewUniformBuffer` 包含 ViewProjectionMatrix、ViewOrigin、PreExposure 等全集。Godot: 空间 shader built-ins 包括 `VIEW_MATRIX`、`PROJECTION_MATRIX`、`INV_VIEW_MATRIX`、`CAMERA_POSITION_WORLD` 等。所有三个引擎都把 view/camera 数据作为 shader 输入合同，不是 diagnostics-only。

---

### 1.3 [Fixed] Compute Dispatch 有 Graph 外 GPU Work

**优先级**: P3-D | **状态**: Fixed (2026-05-25)

**根因**:
`compute_dispatch_renderer.inl:176` 在构建 RenderGraph 前调用 `vkCmdFillBuffer()` 清 storage buffer，然后以 `TransferWrite` 导入同一 buffer。这削弱了 RenderGraph 对真实 GPU work 的可观测性。

**行业参考**:
- Unreal RDG: 所有 GPU work 通过 `AddPass` 进入 graph; 外部 upload/copy 通过 `RDG_EVENT_SCOPE` 标记
- Unity: RenderGraph API 强制所有 resource access 必须在 pass 声明中

**修复方案** (已完成):
1. 新增 `builtin.transfer-fill-buffer` pass type + `BasicTransferFillBufferParams`
2. 新增 `registerBasicTransferFillBufferSchema()` schema 注册
3. Storage buffer 初始状态改为 `Undefined`，FillStorageBuffer pass 写入 `BufferTransferWrite`
4. 移除 graph 外 `vkCmdFillBuffer`，将 fill 操作封装为 RenderGraph pass
5. `--smoke-compute-dispatch` / `--smoke-rendergraph` 验证通过

**Graph 执行顺序**:
```
FillStorageBuffer (BufferTransferWrite) → ClearBackbuffer → ComputeDispatch → ComputeReadbackCopy
```

---

### 1.4 [Partially Fixed] RenderGraph Header 承载过多实现细节

**优先级**: P3-F | **状态**: Partially Fixed (2026-05-27)

**根因**:
`render_graph.hpp` 原约 4000 行，且原本作为 INTERFACE target 承载 public builder API、compile logic、diagnostics formatting、validation 和内部 helpers。

**行业参考**:
- Unreal RDG: `RenderGraph.h` (public API interface) + `RenderGraphBuilder.cpp` (implementation) 分离
- Unity RenderGraph: `RenderGraph.cs` + `RenderGraphBuilder.cs` + `RenderGraphPass.cs` 分离
- Filament FrameGraph: `FrameGraph.h` + `FrameGraphPassResources.h` + `FrameGraphHandle.h` 分离

**已修复 (Phase 1+2+3+4-A+4-B+4-C)**:
1. ADR-001 记录拆分策略 (`docs/rendergraph/adr-001-header-split.md`)
2. Phase 1: 提取纯数据类型到 `render_graph_types.hpp` (~200 行) — handles, enums, descs, schema
3. Phase 2: `vulkan_render_graph.hpp` (adapter) 改为只依赖 `render_graph_types.hpp`，不再依赖完整 `render_graph.hpp`
4. Phase 3: 提取 compile 类型到 `render_graph_compile.hpp`，diagnostics 类型到 `render_graph_diagnostics.hpp`
5. Phase 4-A: `asharia-rendergraph` 改为 STATIC target，新增 `packages/rendergraph/src/render_graph.cpp`
6. Phase 4-A: `RenderGraphCommandList` 方法实现移出 public header，现有 public API 不变
7. Phase 4-B: `RenderGraphSchemaRegistry` / `RenderGraphExecutorRegistry` 实现移入 `src/render_graph.cpp`
8. Phase 4-B: `PassBuilder` 非模板 builder 方法、RenderGraph resource/pass facade 和 public compile/execute overload 移入 `.cpp`
9. Phase 4-C: `diagnosticsSnapshot()` 和 `formatDebugTables()` 实现移入 `.cpp`
10. Phase 4-D: private `compile(schemaRegistry*)` 和 `execute(compiled, executorRegistry*)` 实现移入 `.cpp`
11. Phase 4-E: dependency/culling/producer helper 实现移入 `src/render_graph_dependencies.cpp`
12. Phase 4-F: handle/slot validation helper 实现移入 `src/render_graph_validation.cpp`
13. Phase 4-G: schema/access validation helper 实现移入 `src/render_graph_validation.cpp`
14. Phase 4-H: transient lifetime/resource transition helper 实现移入 `src/render_graph_lifetime.cpp`
15. Phase 4-I: debug label/table formatting helper 实现移入 `src/render_graph_debug.cpp`

**当前结构**:
```
render_graph_types.hpp      — 纯数据契约，无内部依赖
render_graph_compile.hpp    — 编译产物，只依赖 types
render_graph_diagnostics.hpp — diagnostics snapshot，只依赖 compile/types
render_graph.hpp            — command/builder/registry/facade 声明、模板 builder 入口、private helper 声明
src/render_graph.cpp        — command list、registry、builder facade、resource/pass facade、compile/execute、diagnostics formatting 实现
src/render_graph_debug.cpp — debug label/table formatting helper 实现
src/render_graph_dependencies.cpp — dependency/culling/producer helper 实现
src/render_graph_lifetime.cpp — transient lifetime/resource transition helper 实现
src/render_graph_validation.cpp — handle/slot/schema/access validation helper 实现
```

**待完成 (Phase 4+)**:
- 继续评估是否拆出 `RenderGraphCommandList` / `PassBuilder` 独立 header
- `RenderGraphCommandList` / `PassBuilder` 独立 header（当前只有声明和模板入口，仍可接受）

---

### 1.5 [Open] Frame Wait Stage 是单值模型

**优先级**: P3 | **状态**: Open (低优先级)

**根因**:
`VulkanFrameRecordResult` 只有一个 `waitStageMask`。当前 sample renderer 能显式返回 transfer/color/compute stage，但多 RenderView、多 attachment、多 queue 场景会出现覆盖问题。

**行业参考**:
- Vulkan spec: 同帧多个资源需要不同 wait stage 时，应使用 `VkSemaphoreSubmitInfo` 数组，每个元素关联不同 stage
- Unity RenderGraph / Unreal RDG: 从 compiled graph 推导 backbuffer/imported target 的 first-use stage，而不是依赖上层猜测

**解决方案**:
1. 当前维持单值模型作为可行 MVP。
2. 在 RenderGraph 能确定 backbuffer 首次使用 pass 后，从 compiled graph 推导 acquire wait stage:
   ```
   for each imported resource @ first-use pass:
       stage = adapter.toVulkanStage(firstAccess)
   ```
3. 在 frame callback contract 中用 `perTargetWaitStages` 替代 `waitStageMask`。

**验收**:
- 当前 smoke 不退化
- 后续多 RenderView 场景不出现 acquire semaphore stage 不匹配 validation error

---

## 2. 编辑器层 (Editor Shell / Panel / Viewport)

---

### 2.1 [Partially Fixed] Editor Context 与 App Glue 有 God Object 风险

**优先级**: P3-E | **状态**: Partially Fixed (2026-05-27)

**根因**:
- `EditorContext` 暴露 panel registry、event queue、diagnostics、frame debugger、i18n、settings、workspace 和 tools
- `EditorFrameContext` 暴露每帧 UI、diagnostics、settings、input、RenderGraph snapshot 和 viewport host
- `editor_app.cpp` 原约 2373 行，承担 bootstrap、panel/action/tool 注册、smoke 统计、frame loop、ImGui/Vulkan glue 等

**已修复 (Step 1+3)**:
1. 拆出 `editor_app_registration.cpp` — `registerEditorPanels/Actions/Tools` 独立编译单元
2. `editor_app.cpp` 减至 ~2085 行
3. 新增 `EditorCommand` / `EditorTransaction` / `EditorCommandHistory` 体系，为未来 editor mutation 提供 undo/redo 基础

**已修复 (Step 2a)**:
1. `EditorFrameContext` 不再暴露扁平服务集合，改为 `ui` / `diagnostics` / `settings` / `tools` /
   `input` / `renderGraph` / `viewport` capability groups
2. `EditorActionRegistry` dispatch 改为 `EditorActionInvokeContext`，统一负责 `ActionInvoked`
   event；callback 只接收 `EditorActionContext`，action handler 只能访问 panel registry、frame
   debugger 和 workspace layout 能力
3. Scene View overlay helper 已以 `EditorFrameUiContext` + `EditorFrameToolContext` 作为窄 helper
   边界，作为后续逐 panel context 收敛样例

**已修复 (Step 2b-a)**:
1. 拆出 `apps/editor/src/imgui_frame_renderer.hpp/.cpp`
2. `imgui_frame_renderer` 只负责 swapchain image color/present barrier、dynamic rendering attachment
   setup 和 ImGui draw data Vulkan recording
3. `editor_app.cpp` 的 frame callback 继续拥有 RenderView / Frame Debug orchestration，但不再内联
   ImGui Vulkan draw data recording 细节

**已修复 (Step 2b-b)**:
1. 拆出 `apps/editor/src/editor_smoke.hpp/.cpp`
2. `editor_smoke` 接管 smoke mode 判定、frame count、viewport resize smoke 状态推进、
   synthetic multi-view 请求和 Frame Debugger capture/preview/resume smoke 驱动
3. `editor_app.cpp` 保留 smoke validation 和主循环编排，后续可继续拆出 validation/loop host
   而不再扩张顶层 app glue

**已修复 (Step 2b-c)**:
1. 拆出 `apps/editor/src/editor_smoke_validation.hpp/.cpp`
2. `editor_smoke_validation` 接管 editor startup、registration、command、viewport、resize、
   Frame Debugger、input、shortcut 和 layout persistence smoke 断言
3. `editor_app.cpp` 继续保留 bootstrap、frame loop 和 shutdown 编排，不再承载大块 smoke validation

**待完成 (Step 2b)**:
- 继续把 Panel `draw()` / `prepareWindow()` 从顶层 `EditorFrameContext` 推向逐面板需要的窄 context
- 继续拆分 `editor_app.cpp` 中的 frame loop 和剩余 Vulkan host glue

---

### 2.2 [Fixed] Editor Viewport Request 是单实例覆盖模型

**优先级**: P2-C | **状态**: Fixed (2026-05-23)

**根因**:
原 `EditorViewportCoordinator` 只有一个 `requestedViewport_`，同帧第二个 panel 的 viewport 请求会覆盖前者。ImGui texture result 也只保存最近一个 render target。

**行业参考**:
- Unity: `Camera.targetTexture` 为每个 Camera 独立; multi-window 共用渲染基础设施
- O3DE RPI: Scene、Render Pipeline 和 View 分离建模，每个 View 独立拥有 render target
- Godot: `Viewport` 节点可多个同帧共存，每个拥有独立 render target 和 camera

**修复方案** (已完成):
1. `EditorViewportCoordinator` 按 `panelId + EditorViewportKind` 保存 keyed slot
2. 每个 slot 拥有独立请求、presented/pending texture、diagnostics snapshot
3. ImGuiTextureRegistry 的 descriptor owner key 与 panel id 分离
4. `--smoke-editor-viewport` 合成 Scene + Game + Preview 同时请求验证

**后续扩展**: 真实 Game View panel、asset preview UI 和 per-view profiler 仍必须在 keyed slot contract 上扩展

---

### 2.3 [Fixed] Editor 缺少 Command/Transaction 模型

**优先级**: P3-E | **状态**: Fixed (2026-05-25)

**根因**:
当前 editor panel 内没有持久化 mutation 的统一通道。如果要加入 asset browser、material editor、inspector mutation 或 undo/redo，panel 会绕过 controller 直接修改 state。

**行业参考**:
- Unity Undo: `Undo.RecordObject` + `Undo.DestroyObjectImmediate` + `Undo.SetTransformParent`, 所有 mutation 必须走 Undo
- Unreal Transaction: `FScopedTransaction` + `UObject::Modify`, undo/redo 通过 `GEditor->Trans->Undo()`
- Godot UndoRedo: `EditorPlugin.undo_redo.add_do_method/add_undo_method`

**修复方案** (已完成):
1. `editor_command.hpp` — 定义:
   ```cpp
   class EditorCommand {                 // execute() / undo() 纯虚接口
   class EditorTransaction {             // 命令组，原子执行/回滚，反向遍历 undo
   class EditorCommandHistory {          // deque 双栈，深度上限 256
   ```
2. `editor_command.cpp` — 完整实现，push → undo → redo 流程
3. `editor_command.hpp` 只依赖 `asharia/core/result.hpp`，不依赖 Vulkan/editor/renderer
4. `--smoke-editor-shell` 通过 smoke 验证:
   - push 后 `undoDepth=1, redoDepth=0`
   - undo 后值恢复 + 深度正确
   - redo 后值重新执行 + 深度正确
   - double undo 被正确拒绝
5. 现有 `EditorAction` 无需迁移；新 editor mutation 通过 command + transaction 提交

---

### 2.4 [Open] Editor 缺少真实 Game View Panel

**优先级**: P3 | **状态**: Open

**根因**:
当前 Scene View panel 是唯一 viewport 消费者。`EditorViewportCoordinator` 的 keyed slot 已支持同帧 Scene + Game + Preview，但还没有独立的 Game View panel UI。

**解决方案**:
1. 新增 `apps/editor/src/panels/game_view_panel.cpp`:
   - 复用 `EditorViewportPanelHost` request/result API
   - 使用 `EditorViewportKind::Game`
   - 不暴露 grid、gizmo、selection outline toggle
   - Scene-only authoring flags 由 coordinator 自动清除

2. 注册到 `registerEditorPanels()`

**验收**:
- Game View 显示独立 RenderView 输出
- Game View 的 RenderView diagnostics 与 Scene View 分属不同 keyed slot
- `--smoke-editor-viewport` 覆盖 Game View keyed diagnostics

---

## 3. 反射/序列化层 (Schema / Archive / Binding / Persistence)

---

### 3.1 [Open] Schema-first 迁移未完成

**优先级**: P4 | **状态**: Open (部分落地)

**根因**:
新 schema-first 体系 (schema + archive + cpp-binding + persistence) 已落地约 60%，但 `Enum`、`Array`、`AssetReference`、`EntityReference` 仍无持久化语义。旧 `reflection`/`serialization` 仍保留为过渡层。

**行业参考**:
- Unity `ISerializationCallbackReceiver`: OnBeforeSerialize / OnAfterDeserialize 用于迁移
- Unreal `FArchive`: 版本化加载 + `UObject::Serialize` + core redirects
- Serde: data-format-agnostic serialization model

**解决方案**:
1. 补 Enum 持久化: schema 注册时加 `SchemaEnumDesc`，绑定 int→string 映射
2. 补 Array 持久化: `ArchiveValue::Array` 读/写
3. 补 AssetReference 序列化: 用 `stable_guid` 代替指针，反序列化时验证
4. 接入 schema version 和 field alias migration 到文档格式

**验收**:
- package-local persistence test 覆盖 Enum/Array/AssetRef round-trip
- 旧 smoke 不退化

---

### 3.2 [Open] 仍需收口旧 reflection/serialization 层

**优先级**: P4 | **状态**: Open

**根因**:
`packages/reflection` (TypeRegistry) 和 `packages/serialization` (Serializer) 仍保留为过渡兼容层，但不应继续承载新 editor/asset/script 语义。

**解决方案**:
1. 标记旧包为 deprecated facade:
   ```cpp
   // include/asharia/reflection/
   namespace asharia::reflection {
       [[deprecated("Use asharia::schema + asharia::cpp_binding instead")]]
       class TypeRegistry { /* delegate to SchemaRegistry */ };
   }
   ```

2. 迁移完所有现有消费者后删除旧包。

---

## 4. 资产层 (Asset Core / Future Pipeline)

---

### 4.1 [Open] Asset Pipeline 工具链未启动

**优先级**: P4 | **状态**: Open (数据模型已设计)

**根因**:
`asset-core` 已完成 GUID、metadata、product/cache key 和 catalog 的 CPU 数据模型，但 source scan、metadata IO、import 调度、product manifest 和 dependency invalidation 的 pipeline 工具链未实现。

**行业参考**:
- O3DE Asset Processor: 独立进程监控 source asset，生成 product asset，维护 source→product 映射和热重载通知
- Unity AssetDatabase v2: source asset、artifacts (cached)、dependency hash 三层分离
- Godot import process: source + import metadata + hidden `.godot/` folder cache

**解决方案**:
1. 先补最小 `packages/asset-core` 文件 I/O: `AssetCatalog::loadManifest` / `saveManifest`
2. 后续按 `docs/systems/asset-architecture.md` 分阶段实现
3. 每个阶段必须有独立的 import/verification smoke

---

## 5. 场景层 (Scene / World)

---

### 5.1 [Open] Scene Persistence 未接入

**优先级**: P5 | **状态**: Open

**根因**:
`scene-core` 已落地 EntityId + World + Transform，但 save/load 未接入 persistence 层。

**解决方案**:
1. 注册 `TransformComponent` 到 cpp-binding，写 to/from schema field 的 bridge
2. 定义 scene file schema: `EntityId` 映射到 stable GUID，`TransformComponent` 映射到 schema fields
3. persistence load/save 接入 World

**验收**:
- 最小 scene file round-trip: create entity → save → load into new World → verify transform

---

## 6. 构建与基础设施层

---

### 6.1 [Open] renderer_basic_vulkan 是单 Translation Unit (`.inl` 分片)

**优先级**: P4 | **状态**: Open

**根因**:
`basic_renderers.cpp` 包含 14 个 `.inl` 文件，实质上是一个 ~3000+ 行的 translation unit。编译慢，且 helper 无法被别的 TU 引用。

**行业参考**:
- Unreal: render pass 实现各自独立的 `.cpp`，通过 headers 共享公共工具
- Godot: 每个 renderer 子系统独立 translation unit

**解决方案**:
1. 先用 cmake `OBJECT` library 保留 `graph_recording.inl`、`fullscreen_texture_renderer.inl` 等共享 helper 独立编译
2. 拆分 sample renderers 为独立 `.cpp`:
   ```
   src/basic_renderers/triangle_renderer.cpp
   src/basic_renderers/mesh3d_renderer.cpp
   src/basic_renderers/draw_list_renderer.cpp
   src/basic_renderers/compute_dispatch_renderer.cpp
   src/basic_renderers/mrt_renderer.cpp
   src/basic_renderers/fullscreen_texture_renderer.cpp
   ```

**验收**:
- 所有现有 smoke 通过
- cmake compile 时间可观测缩短

---

### 6.2 [Fixed] Scene View Camera Orbit/Pan/Dolly 交互已接入

**优先级**: P4 | **状态**: Fixed (2026-05-25)

**根因**:
Scene View panel 的 camera/unproject code (`editor_viewport_camera.hpp`) 已经支持 resize 后的投影重算和 viewport pixel → world ray unproject；原缺口是 viewport 内没有 interactive orbit/pan/dolly 输入处理，导致相机状态虽能进入 RenderView request，但用户无法通过 Scene View 交互驱动它。

**行业参考**:
- Unreal `FEditorViewportClient`: 拥有 `ViewTransform`、`CameraSpeed`、`EnableRealTimeMouse`, 用 `InputAxis` 驱动
- Unity `SceneView`: `SceneView.lastActiveSceneView.pivot`、`SceneView.size`
- Godot `EditorNode3DGizmoPlugin`: 继承 EditorPlugin，使用 `SubViewport` 的 camera

**修复方案** (已完成):
1. 在 `EditorViewportCamera` helper 中补:
   ```cpp
   void orbitEditorViewportCamera(EditorViewportCamera& camera, float deltaYaw, float deltaPitch);
   void panEditorViewportCamera(EditorViewportCamera& camera, float deltaX, float deltaY, EditorExtent2D extent);
   void dollyEditorViewportCamera(EditorViewportCamera& camera, float delta);
   ```

2. `SceneViewPanel::handleCameraNavigation()` 消费 viewport hover、right-drag、middle-drag 和 mouse wheel，分别驱动 orbit / pan / dolly。
3. 相机变化后重算 projection，并通过 `CameraInputChanged` repaint reason 触发新的 RenderView recording。

**验收**:
- Scene View right-drag / middle-drag / wheel 可 orbit / pan / dolly。
- 相机变化后 viewport 输出通过 `CameraInputChanged` 进入 on-demand repaint。
- `--smoke-editor-viewport` 继续验证 camera diagnostics、unproject、resize aspect 和 Scene View repaint path。

**后续要求**:
- Camera-aware grid range/fade policy 仍归属于 RenderView/grid 后续切片，不在本条重复推进。

---

## 7. 行业对比快速诊断

### 7.1 你的架构 vs 成熟引擎的结构性差异

| 维度 | Asharia Engine 当前 | Unreal 对标 | Unity 对标 | Godot 对标 |
|------|-------------------|------------|-----------|-----------|
| Engine core | `engine/core` 含 log/result/path —— 完整 | `Core` (FString, TArray, TMap) | `UnityEngine.CoreModule` | `core/` (Variant, String, OS) |
| RHI abstraction | `rhi_vulkan` —— 完整 | `RHI` (FRHICommandList) | `GfxDevice` (internal) | `RenderingDevice` |
| RenderGraph | `rendergraph` —— 完整 | `RDG` (FRDGBuilder) | `RenderGraph` (com.unity.render-pipelines.core) | 无(Blender 有) |
| Renderer | `renderer_basic` + `vulkan` —— 在拓展 | `Renderer` (FDeferredShadingSceneRenderer) | `URP` / `HDRP` | `renderer_rd/` |
| Entity model | `scene-core` (EntityId+World) —— 早期 | `UObject` + `AActor` | `GameObject` / `ECS Entity` | `Node` / `Object` |
| Asset pipeline | `asset-core` (GUID+metadata) —— 待实现 | `AssetManager` + `StreamableManager` | `AssetDatabase` + `AssetBundle` | `ResourceImporter` + `.import` |
| Editor host | `apps/editor` (ImGui shell) —— 早期 | `Slate` + `LevelEditor` | `EditorApplication` + `IMGUI` | `EditorInterface` + Godot Editor |
| Schema/persist | `schema` + `archive` + `binding` + `persistence` —— 60% | `UProperty` + `FArchive` | `SerializedObject` + `YAML` | `ClassDB` + `ConfigFile` |
| Shader pipeline | `shader-slang` (Slang → SPIR-V) —— 完整 | `ShaderCompiler` (HLSL → DXIL/SPIR-V) | `ShaderLab` + HLSL | `shader_language` + GLSL |
| Multi-thread | 文档已设计，未实现 | GameThread / RenderThread / RHIThread | JobSystem + Burst | Server API + WorkerThreadPool |

**解读**:
- 渲染链 (RHI + RenderGraph + Renderer) 成熟度: **80%**
- 编辑器 host 成熟度: **50%** (shell 完备，缺 transaction/command/browser)
- 资产管线成熟度: **25%** (metadata model 有，pipeline tooling 无)
- 场景系统成熟度: **40%** (核心 EntityId 有，persistence 和完整 component 无)
- 反射序列化成熟度: **60%** (schema-first 骨架好，Enum/Array/AssetRef 缺)
- 多线程: **10%** (设计文档完整，实现仅单线程)

### 7.2 你在哪些地方超越了大部分自研引擎

1. **RenderGraph 声明式架构**: 多数自研项目直接操作 Vulkan/DX12 命令，你的 pass/resource/state 声明 + compile + execute 四段式设计能进入 Frostbite/Unreal/Unity 这一梯队
2. **Schema-first persistence**: 四层分离 (schema/cpp-binding/archive/persistence) 是正确设计，和 Unreal FProperty + FArchive + CoreRedirects 思路一致，比简单 JSON dump 成熟得多
3. **Editor 作为独立消费者**: 你的 editor 不拥有 renderer，这和 Unreal Editor 靠 Slate + FViewport 但不拥有 RHI 的原则相同
4. **完整文档 + Smoke 驱动**: 你的 AGENTS.md、多层架构文档、每阶段 smoke gate 和工程化 build 脚本在自研项目中罕见

### 7.3 当前最需要关注的 5 个改变

1. **Editor context 仍需 capability-scoped 收敛**: command/transaction 已有基础，但 `EditorContext` / `EditorFrameContext` 仍是宽服务集合，新增 asset browser、material editor 或 inspector mutation 前需要继续收窄。
2. **Grid 仍需 camera-aware policy 和 pixel smoke**: renderer-owned debug-line GPU pass 已落地，但 grid provider 仍是固定原点 packet，尚未按 camera range/fade 生成稳定可读网格，也未做像素/readback 级 camera-difference 验证。
3. **`render_graph.hpp` 仍需继续拆分实现边界**: types / compile headers 已提取，后续复杂 compiler/cache/unsafe pass 前还需要把更多实现移出大型 public header。
4. **Asset pipeline 只有 metadata / catalog 基线**: 离真正的 source scan、product manifest、mesh/texture upload 还有工具链工作。
5. **renderer_basic_vulkan 仍是单 TU `.inl` 分片**: 后续 renderer pass 增多前需要拆分编译单元，降低改动耦合和编译成本。

---

## 8. 推进顺序建议

基于依赖关系和阻塞规则 (来自 `next-development-plan.md` A-F 门禁):

```
已完成:
  A. Format contract ✓
  B. RenderView camera → GPU contract B.2 + B.3 renderer slice ✓
  C. Multi-view request ✓
  D. Graph-visible compute fill ✓
  E. Editor command/transaction base ✓

立即 (阻塞后续功能):
  E.2 Capability-scoped EditorFrameContext / EditorContext 收敛 (2.1)

短期 (2-4 轮迭代):
  Grid camera-aware range/fade policy + pixel/readback camera smoke (1.2)
  F. RenderGraph API/implementation split Phase 4+ (1.4)
  2.4 Game View panel
  1.5 Frame wait stage 拓展 (在多 RenderView 稳定后)

中期 (4-8 轮迭代):
  3.1 Schema-first 迁移收尾
  4.1 Asset pipeline 第一批 importer
  5.1 Scene file persistence 接入 schema
  6.1 renderer_basic_vulkan split

长期:
  多线程 frame loop
  Material pipeline key 系统
```

---

## 9. 参考链接清单

### RenderGraph & RHI:
- Frostbite FrameGraph: https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-RenderingArc
- Unreal RDG: https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine
- Unity RenderGraph: https://docs.unity.cn/Packages/com.unity.render-pipelines.core%4017.0/manual/render-graph-fundamentals.html
- Filament FrameGraph: https://google.github.io/filament/notes/framegraph.html
- Granite render graph deep dive: https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive/
- Vulkan synchronization: https://github.khronos.org/Vulkan-Site/spec/latest/chapters/synchronization.html

### Editor & Viewport:
- Unity SceneView: https://docs.unity3d.com/ScriptReference/SceneView.html
- Unity Editor Windows: https://docs.unity.cn/Manual/editor-EditorWindows.html
- Unreal FEditorViewportClient: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FEditorViewportClient
- Godot Editor Plugins: https://docs.godotengine.org/en/stable/tutorials/plugins/editor/making_plugins.html
- O3DE Atom RPI: https://docs.o3de.org/docs/atom-guide/dev-guide/rpi/rpi-system/

### Asset Pipeline:
- Unity AssetDatabase: https://docs.unity.cn/Manual/AssetDatabase.html
- O3DE Asset Processor: https://docs.o3de.org/docs/user-guide/assets/asset-processor/
- Godot Import Process: https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/import_process.html
- Unreal Async Loading: https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine

### Reflection & Serialization:
- Unity Script Serialization: https://docs.unity3d.com/Manual/script-serialization.html
- Unreal Property System: https://www.unrealengine.com/blog/unreal-property-system-reflection
- Serde Data Model: https://serde.rs/data-model.html
- Protocol Buffers: https://protobuf.dev/overview/

### Architecture:
- Godot Architecture Overview: https://docs.godotengine.org/en/stable/engine_details/architecture/godot_architecture_diagram.html
- Bitsquid Tool Architecture: https://bitsquid.blogspot.com/2010/04/our-tool-architecture.html
- Game Programming Patterns: https://gameprogrammingpatterns.com/
- Unreal Threaded Rendering: https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine

---

## 10. 设计模式分析

分析日期: 2026-05-25

本节基于 GoF 23 种经典设计模式和游戏引擎常用模式，对代码库进行系统扫描。每个模式标注使用位置、质量评价和改进建议。

### 10.1 当前使用的模式 (按 GoF 分类)

#### 创建型 (Creational)

##### Builder (fluent) — `RenderGraph::PassBuilder`

- **位置**: `packages/rendergraph/include/asharia/rendergraph/render_graph.hpp:631-810`
- **形态**: Director-less Builder，fluent method chaining。每个 `.readTexture()` / `.writeColor()` / `.setParams()` / `.execute()` 返回 `PassBuilder&`
- **评价**: **优秀**。Builder 只能由 friend `RenderGraph` 内部创建，外部无法持有未绑定 builder。`static_assert` 校验 params type。比 Unity `RenderGraphBuilder` 的形态更严谨——Unity 靠运行时错误，你靠编译期检查
- **问题**: `PassBuilder` 也承担了验证逻辑 (`validatePass`)，可以提为 Validator

```cpp
// 当前用法 (正确)
graph.addPass("OpaqueGeometry", "builtin.raster-draw-list")
     .writeColor("target", colorImage)
     .writeDepth("depth", depthImage)
     .readTexture("albedoMap", albedoImage, RenderGraphShaderStage::Fragment)
     .setParams<DrawListParams>(params)
     .execute([](RenderGraphPassContext ctx) { ... });

// 对标: Unity RenderGraphBuilder
// builder.SetRenderFunc<PassData>(static ExecutePass) 是 C# 泛型等价物
```

##### Factory Method (static create) — 全局使用

- **位置**: `VulkanContext::create()`, `VulkanFrameLoop::create()`, `BasicFullscreenTextureRenderer::create()`, `EditorViewportCoordinator::create()`, `ImGuiRuntime::create()`
- **形态**: 轻量构造器 + `static Result<T> create(Desc)`。构造函数不执行可能失败的操作
- **评价**: **优秀**。和 AGENTS.md 规定的 "Constructors stay lightweight. Vulkan/OS/IO-failable work goes into explicit create()" 一致。对标 Unreal `UObject` 的 `CreateDefaultSubobject` + `PostInitProperties`
- **对标**: Godot 的 `ResourceFormatLoader::load()`, O3DE 的 `AZ::ComponentApplication::Create()`

```cpp
// 当前形态
auto ctx = VulkanContext::create(VulkanContextDesc{...});
if (!ctx) return ctx.error();  // Result<T> 传播

// 反模式 (当前未出现): 构造函数内调用 vkCreateDevice
VulkanContext(...) { vkCreateDevice(...); }  // 永远不要这样做
```

##### Prototype (隐式) — RenderView

- **位置**: `renderer_basic_vulkan` 的 `BasicRenderViewDesc` 作为视图原型
- **评价**: 使用正确但未显式标注。Scene View / Game View 的 RenderView 描述共享同一份 "原型" 结构体模板，各自 clone 后修改 target/camera/flags

---

#### 结构型 (Structural)

##### Adapter — `rhi_vulkan_rendergraph`

- **位置**: `packages/rhi-vulkan/include-rendergraph/asharia/rhi_vulkan_rendergraph/vulkan_render_graph.hpp`
- **形态**: 把 RenderGraph 的抽象 state (`RenderGraphImageState::ShaderRead`) 翻译成 Vulkan 具体类型 (`VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`, `VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT`, `VK_ACCESS_2_SHADER_READ_BIT`)
- **评价**: **优秀**。干净的单向 adapter，RenderGraph 不反向依赖 Vulkan。对标 O3DE 的 RHI adapter 层
- **行业对标**: Unity `RenderGraph` → `CommandBuffer` (C# 内部 adapter)，Unreal `RDG` → `RHICommandList`

```cpp
// adapter 映射表 (简化)
RenderGraphImageState::ShaderRead(fragment) → {
    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    .stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
    .access = VK_ACCESS_2_SHADER_READ_BIT
};
```

##### Facade

- **引擎层**: `VulkanContext` (instance+device+allocator+surface), `VulkanFrameLoop` (swapchain+acquire+submit+present+deferred deletion)
- **持久化层**: `persistence::saveObject()` / `loadObject()` — 隐藏 SchemaRegistry + BindingRegistry + ArchiveValue + Migration 的复杂度
- **编辑器层**: `EditorContext` — 暴露 8 个 editor 子系统的统一入口
- **评价**:
  - `VulkanContext` / `VulkanFrameLoop` → **优秀**。真正的 Facade: 隐藏 Vulkan 1.x 样板但不限制灵活性
  - `persistence::saveObject` / `loadObject` → **优秀**。两个函数签名背后是四层子系统
  - `EditorContext` → **中等**。向 Service Locator 退化 (见 10.2)

##### Composite — RenderGraph compile/snapshot

- **位置**: `RenderGraphCompileResult` 的 pass tree / resource graph
- **评价**: 编译后的 RenderGraph 是 Composite: 每个 pass node 包含子资源 node 和 access edge。Live RG View 遍历这棵树做可视化

##### Bridge — Schema/Wasm/C++ 三层分离

- **位置**: `schema` (type id) → `cpp-binding` (C++ member bridge) → `persistence` (save/load bridge)
- **评价**: **优秀**。和 Unreal FProperty bridge 或 Serde Serialize/Deserialize 思路一致——把三个独立变动的维度 (类型标识、C++ 内存布局、IO 格式) 分离

---

#### 行为型 (Behavioral)

##### Command — 两处使用

**a) Editor Action (经典 Command):**
- **位置**: `apps/editor/src/editor_action.hpp`
- **形态**: `EditorActionDesc` + `EditorActionCallback = std::function<void(EditorActionContext&)>`，通过 `EditorActionRegistry::invoke(actionId, actionInvokeContext)` 调用
- **评价**: **正确但仍是过渡阶段**。当前 action 已不接收完整 `EditorContext`，但仍直接修改 panel/debug/workspace state，没有 `execute()`/`undo()` 分离。和 `EditorCommand` / `EditorTransaction` 的差距见 10.4.1
- **对标**: Unity `EditorApplication.ExecuteMenuItem()`, Unreal `FUICommandList::ExecuteAction()`

**b) RenderGraph Command (Interpreter 变体):**
- **位置**: `RenderGraphCommandList` 和 `RenderGraphCommand` (render_graph.hpp:200-314)
- **形态**: 将 GPU 操作 reify 为抽象命令 (`SetShader`, `SetTexture`, `DrawFullscreenTriangle`, `ClearColor`, `CopyImage`, `Dispatch`)，executor 在后端解释执行
- **评价**: **优秀**。和 Unity RenderGraph 的 `PassData` + `cmd` 模式一致。命令对象化后可供 Frame Debug 和 RG View 消费

##### Strategy

**a) PersistencePolicy:**
- **位置**: `packages/persistence/include/asharia/persistence/persistence.hpp`
- **形态**: `saveObject(obj, policy)` 和 `loadObject(obj, policy)`，策略控制 unknown field、missing field 和 migration 行为
- **评价**: **优秀**。和 Serde 的 `Serializer` trait / `Deserializer` trait 思路相似

**b) RenderGraphExecutorRegistry:**
- **位置**: `render_graph.hpp:541-603`
- **形态**: 按 pass type string 映射到 callback，允许不同 renderer 注册不同的执行策略
- **评价**: **正确**。但 pass type 是字符串 key，未来应改为 typed ID (和 schema 体系对齐)

**c) renderer recordFrame*() 重载:**
- **位置**: `BasicFullscreenTextureRenderer`
- **形态**: 多个 `recordFrame*()` 通过重载选择不同策略，非继承
- **评价**: **可接受**。当前阶段 sample renderer 数量少，重载足够。未来 renderer 种类多时应转为 Strategy interface

##### Template Method — `ImGuiEditorPanel`

- **位置**: `apps/editor/src/editor_panel.hpp:74-86`
- **形态**: `prepareWindow()` (可覆写) + `draw()` (纯虚函数)，`EditorPanelRegistry::drawPanels()` 按固定流程调用
- **评价**: **简洁正确**。对标 Unity `EditorWindow.OnGUI()`, Godot `Control._draw()`

```cpp
class ImGuiEditorPanel {
    virtual void prepareWindow(EditorFrameContext&, EditorPanelState&) {}  // hook
    virtual void draw(EditorFrameContext&, EditorPanelState&) = 0;         // template step
};
```

##### Observer / Event Queue — `EditorEventQueue`

- **位置**: `apps/editor/src/editor_event.hpp`
- **形态**: 基于 pull 的消息队列（每帧 drain），不是 push-based callback
- **评价**: **优秀**。避免了传统 Observer 的三种常见问题:
  1. 没有 callback 注册/注销顺序依赖 (因为用 queue 而非直接调用)
  2. 没有 observer 循环通知导致的 reentrancy (只在帧边界 drain)
  3. 没有悬挂引用 (生产者只 push `EditorEvent` value type)
- **对标**: Unreal `FMessageLog`, Godot's `call_deferred`

##### State — `EditorFrameDebugger`

- **位置**: `apps/editor/src/editor_frame_debugger.hpp`
- **形态**: 显式状态枚举 + `transitionTo()` + 状态查询 (6 个状态)
- **评价**: **优秀**。比大多数自研引擎的状态机实现更严谨——每个 transition 有 stats 记录，smoke test 验证全部 transition

```cpp
enum class EditorFrameDebuggerState {
    Running, CaptureRequested, CapturingFrame,
    WaitingGpuFence, PausedFrameDebug, Resume
};
// 状态转换路径:
// Running → CaptureRequested → CapturingFrame → WaitingGpuFence → PausedFrameDebug → Resume → Running
```

##### Visitor — EditorToolRegistry

- **位置**: `apps/editor/src/editor_tool.hpp` 和 `editor_action.hpp`
- **形态**: `visitTools()`, `visitActions()`, `visitToolbarActions()`, `visitViewportOverlays()` 使用 `std::function` callback 而非 double dispatch
- **评价**: **正确**。`std::function` visitor 比经典 GoF double-dispatch Visitor 更灵活（不修改被访问类），适合工具/action 这类高频扩展、低频遍历的场景

##### Dependency Injection (手动)

- **位置**: `runEditorLoop()` 在 `editor_app.cpp:1536` 附近
- **形态**: 所有 editor 子系统在 `runEditorLoop()` 中创建，通过构造函数注入引用。无全局 singleton、无 magic static、无 service locator 自动解析
- **评价**: **优秀**。这是现代 C++ 的推荐做法 (和 Google C++ Style Guide 的 "Prefer dependency injection" 一致) — 比 Godot 的 `EditorNode::get_singleton()` 模式更清晰

```cpp
// 当前形态 (正确)
auto panelRegistry = EditorPanelRegistry();
auto actionRegistry = EditorActionRegistry();
auto eventQueue = EditorEventQueue();
EditorContext context{panelRegistry, eventQueue, /* ... */};

// 反模式 (当前未使用)
EditorContext::instance().panelRegistry().focusPanel("scene-view");  // 全局单例
```

##### Result<T> (Either Monad) — 全局使用，贯穿所有 package

- **位置**: `engine/core/include/asharia/core/result.hpp`
- **评价**: **决策正确**。`Result<T> = std::expected<T, Error>` 替代了异常和裸 bool/VkResult 返回值。`VkResult` 永远不会被忽略——总是转换为 `Error` 后传播。Unreal 的 `FErrorOr` / Rust 的 `Result` 同样思路
- **影响范围**: 从 `vkCreateDevice` 到 `saveObject` 到 `registerPanel`，全链路使用

---

### 10.2 当前存在的反模式

| 反模式 | 位置 | 严重程度 | 对标行业问题 |
|--------|------|----------|------------|
| **God Object (中等)** | `EditorViewportCoordinator` — 同时管理 viewport slot、texture registry、render view recording、frame debug preview、retired texture GC、40+ stats | **P3** | Unreal `FEditorViewportClient` 有类似问题(200+ methods)，Unity `SceneView` 也被批评"太大"。但它们的 scope 都更窄——coordinator、renderer、preview 是三个不同 owner |
| **Monolithic God Class (中等)** | `RenderGraph` — 3993 行 header-only，合并 declaration、compile、execute、diagnostics、validation 等 ~50 methods | **P3** | Unreal RDG's `FRDGBuilder` 约 3000 行但源码拆分清晰 (declaration/execute/diagnostics 各自独立文件)；Unity RenderGraph 也拆成 4-5 个文件 |
| **Service Locator lite (轻)** | `EditorContext` — 8 个 subsystems 被注入到每个 panel/action/tool，宽 API 导致 consumer 可访问不需要的服务 | **P3** | Unreal `UWorld` 暴露 GetWorld() 访问几乎所有子系统——但 Unreal 有 module 边界限制，你的 context 当前没有。Unity `EditorApplication` 作为 static facade 也是宽接口 |
| **String-based ID** | `EditorId`, pass type id, schema type/field id 全部用 `std::string` — 无编译期验证 | **P4** | Unity 也大量使用 string ID (`Shader.PropertyToID` 编译期 hash)；Unreal 用 FName (运行时 hash table)。当前阶段 string OK，但后续 material/pipeline key 应转 typed |
| **单体 registration** | `registerEditorPanels()`, `registerEditorActions()` — 所有 panel/action 在一个函数中逐个注册 | **P4** | Godot 用 `EditorPlugin::_enter_tree()` 分散注册；Unreal 用 `IModuleInterface::StartupModule()`。当前 6 panel / 12 action / 7 tool 可接受，超过 30+ 应拆 |
| **No mockable interfaces** | `EditorPanelRegistry` 是具体类，面板测试必须依赖实现 | **P5** | Godot 的 EditorInterface 是具体类且难 mock，但 Godot 有 GDScript 测试框架。当前无 panel 独立测试需求 |

---

### 10.3 按层级分布

```
apps/editor/src/
  Registry × 3            (PanelRegistry, ActionRegistry, ToolRegistry)
  Command                 (EditorAction)
  Template Method         (ImGuiEditorPanel)
  Observer (Event Queue)  (EditorEventQueue)
  State                   (EditorFrameDebugger)
  Visitor                 (ToolRegistry::visit*)
  Adapter (Interface)     (EditorViewportCoordinator 实现两个接口)
  Resource Pool           (ImGuiTextureRegistry)
  Facade (Service Locator lite) (EditorContext)
  Dependency Injection    (runEditorLoop)

packages/rendergraph/
  Builder (fluent)        (RenderGraph::PassBuilder)
  Command (Interpreter)   (RenderGraphCommandList)
  Strategy                (RenderGraphExecutorRegistry)
  Pipeline                (compile → execute)
  Composite               (compiled pass/resource/dependency graph)
  Registry                (RenderGraphSchemaRegistry)

packages/rhi-vulkan/
  Facade                  (VulkanContext, VulkanFrameLoop)
  Factory Method          (VulkanContext::create)
  RAII / Scope Guard      (VulkanDebugLabelScope, VulkanTimestampScope)

packages/renderer-basic/
  Factory Method          (BasicFullscreenTextureRenderer::create)
  Strategy (overload)     (recordFrame*)

packages/persistence/
  Facade                  (saveObject / loadObject)
  Strategy                (PersistencePolicy)

packages/schema + archive + cpp-binding + persistence
  Bridge                  (三层分离)

engine/core/
  Either Monad (Result)   (全局使用)
```

### 10.4 缺少的关键设计模式及引入方案

#### 10.4.1 Command + Transaction (编辑器缺少)

**当前状态**: `EditorActionRegistry` 有 Command pattern 雏形（按 id 执行 callback），但没有 undo/redo。

**行业对标**:
- Unity Undo: `Undo.RecordObject(obj, "Move")` + `Undo.PerformUndo()`
- Unreal Transaction: `FScopedTransaction` + `UObject::Modify()` + `GEditor->Trans->Undo()`
- Godot UndoRedo: `undo_redo.add_do_method()` / `undo_redo.add_undo_method()`

**引入方案** (已在 2.3 节详述):

```cpp
class EditorCommand {
public:
    virtual ~EditorCommand() = default;
    virtual Result<void> execute() = 0;
    virtual Result<void> undo() = 0;
};

class EditorTransaction {
    std::vector<std::unique_ptr<EditorCommand>> commands_;
public:
    void addCommand(std::unique_ptr<EditorCommand> cmd);
    Result<void> executeAll();
    Result<void> undoAll();
};

class EditorCommandHistory {
    std::deque<EditorTransaction> undoStack_;
    std::deque<EditorTransaction> redoStack_;
public:
    void push(EditorTransaction&& t);
    Result<void> undo();
    Result<void> redo();
};
```

**集成点**: `EditorActionRegistry::invoke()` 的 callback 不再直接修改 state，而是创建 Command 推入 `EditorCommandHistory`。

**阻塞**: 需要 EditorContext 收敛完成 → capability-scoped context (问题 2.1)。

---

#### 10.4.2 Abstract Factory (Renderer 缺少)

**当前状态**: `renderer_basic_vulkan` 的 sample renderers (triangle, mesh3D, draw-list, fullscreen, MRT, compute) 各自独立创建，没有统一的 renderer type → factory 映射。

**行业对标**:
- Unreal: `FRendererModule::CreateSceneRenderer()` 根据 feature level 创建不同 renderer
- Unity: `RenderPipelineAsset.CreatePipeline()` 创建完整的 SRP 实例
- Godot: `RenderingServer` 使用内部 `RendererCompositor` + `RenderSceneData` factory

**引入方案**:

```cpp
class IBasicRenderer {
public:
    virtual ~IBasicRenderer() = default;
    virtual Result<void> recordFrame(VulkanFrameRecordContext&, const BasicRenderViewDesc&) = 0;
    virtual BasicRendererStats stats() const = 0;
};

class BasicRendererFactory {
    std::unordered_map<std::string /* renderer type */, std::function<Result<std::unique_ptr<IBasicRenderer>>(RendererDesc)>> creators_;
public:
    Result<std::unique_ptr<IBasicRenderer>> create(const std::string& type, RendererDesc desc) {
        auto it = creators_.find(type);
        if (it == creators_.end()) return Error("Unknown renderer type");
        return it->second(desc);
    }
};
```

**价值**: 后续材质的 shader pass、G-buffer MRT pass、shadow map pass 都通过统一 factory 创建，让 `renderer_basic_vulkan` 不依赖具体 renderer 类型。

---

#### 10.4.3 Observer (Editor 内部缺少)

**当前状态**: `EditorEventQueue` 已经是 Observer 变体，但在 editor 子系统之间的状态变化通知仍然通过 polling (panel 每帧检查 `viewportCoordinator` 状态) 或通过 `EditorContext` 间接调用。

**行业对标**:
- Unreal: `FCoreDelegates::OnBeginFrame`, `FEditorDelegates::OnMapOpened` 等全局 delegate
- Godot: `EditorPlugin._handles()` / `EditorPlugin._forward_canvas_gui_input()` 等虚函数通知
- Unity: `EditorApplication.update`, `EditorApplication.hierarchyChanged` 等事件

**引入方案**:
将 `EditorEventQueue` 扩展为支持多 consumer 的 event bus:

```cpp
template<typename... Args>
class EditorSignal {
    std::vector<std::function<void(Args...)>> slots_;
public:
    void connect(std::function<void(Args...)> slot) { slots_.push_back(std::move(slot)); }
    void emit(Args... args) { for (auto& slot : slots_) slot(args...); }
};

// 用法示例
class EditorSelectionService {
public:
    EditorSignal<EditorId, SelectionOp> selectionChanged;
    // ...
};
```

**适用场景**:
- `EditorSelectionService::selectionChanged` → Inspector panel 自动刷新
- `EditorAssetCatalog::assetChanged` → Asset Browser panel 刷新
- `EditorWorkspaceController::layoutChanged` → Dock shell 重新布局

---

#### 10.4.4 Chain of Responsibility (Input 处理缺少)

**当前状态**: `EditorInputRouter` 负责聚合 ImGui capture flag + Scene View focus state，`EditorShortcutRouter` 负责把 shortcut 变为 action。但它们都是二值判断 (shortcuts enabled / not enabled)，不是链式处理。

**行业对标**:
- Godot: `Control::_gui_input()` 沿 scene tree 冒泡
- Unity IMGUI: `Event.current.Use()`——事件被消费后不继续传播
- Unreal: `FReply::Handled()` / `FReply::Unhandled()`——输入链

**引入方案**:
当 editor 有多个 viewport 或 pickable scene objects 时，input 需要 Chain of Responsibility:

```cpp
class IEditorInputHandler {
public:
    virtual ~IEditorInputHandler() = default;
    // 返回 true 表示已消费此事件，链停止
    virtual bool handleMouseButton(const EditorInputSnapshot&, MouseButtonEvent) = 0;
    virtual bool handleKey(const EditorInputSnapshot&, KeyEvent) = 0;
};

class EditorInputChain {
    std::vector<IEditorInputHandler*> handlers_; // 优先级从高到低
public:
    void addHandler(IEditorInputHandler* h) { handlers_.push_back(h); }
    bool process(MouseButtonEvent e) {
        for (auto h : handlers_)
            if (h->handleMouseButton(currentSnapshot_, e))
                return true;  // 已消费
        return false;  // 未消费
    }
};
```

**处理顺序**: Gizmo (最高) → Selection Picking → Scene View Camera → 未消费的 shortcut

---

#### 10.4.5 Proxy / Lazy Initialization (Asset 系统缺少)

**当前状态**: 没有任何 asset loading。后续 `AssetHandle<T>` 表示稳定引用，但不能立刻拿到 loaded resource。

**行业对标**:
- Unreal `TSoftObjectPtr<T>` / `FStreamableHandle` — proxy + async load
- Unity `AssetReference` + `Addressables.LoadAssetAsync<T>` — proxy + async load
- Godot `ResourceLoader.load()` — 同步 + cache

**引入方案**:
`AssetHandle<T>` 作为 Proxy:

```cpp
template<typename T>
class AssetHandle {
    AssetGuid guid_;
    AssetLoadState state_;  // Unloaded, Loading, Loaded, Failed
    mutable std::shared_ptr<T> cached_;
public:
    Result<std::shared_ptr<const T>> resolve();  // 可能触发 loading
    AssetLoadState state() const;
    AssetGuid guid() const;
};
```

**价值**: Proxy 模式隔离了 asset 引用和 asset data——scene 存储 `AssetHandle<Mesh>`，renderer 拿到 mesh data 时 proxy 已 resolved。

---

### 10.5 模式使用评分

按 package 统计:

| Package | 模式数量 | 设计质量 | 主要缺陷 |
|---------|---------|---------|---------|
| `rendergraph` | 6 (Builder, Command/Interpreter, Strategy, Pipeline, Composite, Registry) | **8/10** | God Class (1.4), string-based pass type |
| `rhi-vulkan` | 3 (Facade, Factory Method, RAII) | **9/10** | 无明显缺陷 |
| `renderer-basic-vulkan` | 2 (Factory Method, Strategy/overload) | **6/10** | 缺 Abstract Factory (10.4.2)，inl 分片不独立编译 (6.1) |
| `editor` | 9 (Registry×3, Command, TemplateMethod, Observer/Queue, State, Visitor, Adapter, Pool, DI) | **7/10** | God Object (coordinator), ServiceLocator lite (context), 缺 Command+Transaction (10.4.1) |
| `persistence` | 2 (Facade, Strategy) | **9/10** | Enum/Array 类型缺失 |
| `schema+archive+binding` | 1 (Bridge) | **8/10** | migration 未接入持久化格式 |
| `engine/core` | 1 (Result/Either Monad) | **10/10** | 无明显缺陷 |
| **全局** | **16+** | **7.5/10** | 见下 |

### 10.6 总体评价

**超越大部分自研项目的地方**:
1. **Result (Either Monad) 全链路**: 这是 `engine/core` 中最有价值的决策——和 Rust 的 `Result` 设计一致，Unreal 的 `FErrorOr` 类似但不强制传播
2. **Builder + Command (Interpreter) 双模式**: 你的 RenderGraph 的 `PassBuilder` (声明) + `RenderGraphCommandList` (执行) 两阶段设计直接对标 Unity/Unreal 的 RenderGraph
3. **手动 Dependency Injection**: 比 Godot 的 singleton pattern (`EditorNode::get_singleton()`) 更易测试、更清晰
4. **Registry + Visitor 组合**: 避免暴露内部 container，保持封装，同时提供扩展灵活性
5. **State pattern (Frame Debugger)**: 6 状态显式枚举 + 统计 + smoke，正确性可验证

**容易低估的好的地方**:
- **所有 VkResult 转换**: 没有遗漏的 `VkResult` 返回值。每个 Vulkan 调用都经 `Result<T>` 转换并传播
- **epoch-based GC**: `ImGuiTextureRegistry` 和 `VulkanFrameLoop` 的延迟回收机制避免了经典 dangling GPU resource bug
- **POD descriptor struct**: `VulkanContextDesc` / `PersistencePolicy` / `EditorActionDesc` 等把配置与行为分离

**当前最值得引入的设计模式** (按收益排序):
1. **Command + Transaction** (10.4.1) — editor mutation 的基础设施，没有它无法加 undo/redo
2. **Abstract Factory** (10.4.2) — renderer 种类增多时的自然需求
3. **Chain of Responsibility** (10.4.4) — Scene View 的 gizmo/picking/camera 输入冲突需要它
4. **Proxy** (10.4.5) — asset loading 的第一版必须的模式

**反模式截止线**:
- `EditorContext` 在 8 个以上 subsystem 后必须拆 (当前 8 个)
- `RenderGraph` 在 5000 行或增加 cache/alias/multi-queue 前必须拆 API/implementation
- `EditorViewportCoordinator` 在增加 gizmo/selection 后必须拆
