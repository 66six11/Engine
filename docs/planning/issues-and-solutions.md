# 架构问题记录与解决方案

记录日期: 2026-05-25
更新日期: 2026-06-01

本文基于 `docs/planning/next-development-plan.md` 的 A-F 门禁和全量代码审查，按层级归类所有已知架构缺陷，并针对每个问题提供根因分析、行业参考和具体可执行方案。已修复项标注 `Fixed` 并附修复摘要; 未修复项标注 `Open` 并按确定性推进顺序排列。

## A-F 门禁当前状态

| 门禁 | 问题 | 状态 | 完成日期 |
|------|------|------|---------|
| **A** | Format / capability contract | ✓ Fixed | 2026-05-23 |
| **B** | RenderView camera → GPU contract | ◐ Partially Fixed (B.2+B.3 renderer slice) | 2026-05-25 |
| **C** | Multi-view request model | ✓ Fixed | 2026-05-23 |
| **D** | Graph-visible GPU work | ✓ Fixed | 2026-05-25 |
| **E** | Editor state and command model | ◐ Partially Fixed (Step 1+2a+2b-a..2b-z+2c-a..2c-f+3) | 2026-06-01 |
| **F** | RenderGraph API / implementation split | ◐ Partially Fixed (Phase 1+2+3+4+5-A..5-BI) | 2026-06-01 |

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
3. 新增 `RenderGraphCommandKind::FillBuffer` / `RenderGraphCommandList::fillBuffer()`，让 diagnostics 与 Frame Debug 能看到 buffer fill command summary
4. Storage buffer 初始状态改为 `Undefined`，FillStorageBuffer pass 写入 `BufferTransferWrite`
5. 移除 graph 外 `vkCmdFillBuffer`，将 fill 操作封装为 RenderGraph pass
6. `--smoke-compute-dispatch` / `--smoke-rendergraph` 验证通过

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

**已修复 (Phase 1+2+3+4+5)**:
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
16. Phase 5-A/B: `RenderGraphCommandList` 提取到 `render_graph_command_list.hpp`，pass context 与 schema/executor registry 起初提取到 `render_graph_execution.hpp`
17. Phase 5-D: `RenderGraph` / `RenderGraph::PassBuilder` 声明提取到 `render_graph_builder.hpp`，`render_graph.hpp` 收敛为纯聚合头
18. Phase 5-E: 不访问 `RenderGraph` 私有状态的 compile/dependency/schema helper 收敛为 `.cpp` 文件局部函数，减少 `render_graph_builder.hpp` private static 声明
19. Phase 5-F: `src/render_graph.cpp` 继续瘦身为 RenderGraph 生命周期与 public compile/execute/diagnostics facade；command list、schema/executor registry、非模板 builder/resource declaration API 分别拆到 `render_graph_command_list.cpp`、`render_graph_registry.cpp`、`render_graph_builder.cpp`
20. Phase 5-G: 不访问 `RenderGraph::Impl` 私有状态的 debug name / command detail helper 拆到 `src/render_graph_debug_names.hpp/.cpp`，不再作为 `Impl` 成员声明
21. Phase 5-H: 不访问 `RenderGraph::Impl` 私有状态的 schema/slot/command validation helper 和 duplicate slot message 收敛为 `render_graph_validation.cpp` 文件局部模板/函数，不再作为 `Impl` 成员声明
22. Phase 5-I: 跨 compile/dependency/lifetime 使用、但不访问 `Impl` 私有容器的 pass/resource query 和 transition helper 拆到 `src/render_graph_pass_queries.hpp`，不再作为 `Impl` 成员声明或在 lifetime TU 重复实现
23. Phase 5-J: validation-only slot group structs、execution-only missing callback message 和 diagnostics-only command row helper 收敛到各自 `.cpp` 文件局部，不再出现在 `render_graph_internal.hpp`
24. Phase 5-K: diagnostics-only slot list / slot row / transition row formatting helper 收敛为 `render_graph_diagnostics.cpp` 文件局部；未使用的 `imageHandleList()` 从 `Impl` 声明和 debug 实现中移除
25. Phase 5-L: validation-only image/buffer access conflict message helper 收敛为 `render_graph_validation.cpp` 文件局部模板，不再作为 `Impl` 成员声明或 debug TU 实现
26. Phase 5-M: dependency-only cycle edge/message 与 image/buffer producer error message helper 收敛为 `render_graph_dependencies.cpp` 文件局部模板/函数，不再作为 `Impl` 成员声明
27. Phase 5-N: culling-only `passCanBeCulled()` / imported-resource write check 收敛为 `render_graph_dependencies.cpp` 文件局部模板，compile-only culled-pass materialization 收敛为 `render_graph_compile.cpp` 文件局部模板
28. Phase 5-O: validation-only slot/access/stage validation helper 收敛为 `render_graph_validation.cpp` 文件局部模板；`render_graph_internal.hpp` 仅保留跨 lifetime TU 使用的 handle validation 入口
29. Phase 5-P: dependency-only add/read/build 子步骤收敛为 `render_graph_dependencies.cpp` 文件局部模板；`render_graph_internal.hpp` 仅保留 compile TU 调用的 dependency entrypoints
30. Phase 5-Q: compile 使用的 transient allocation、declared-use 和 transition helper 拆到 `src/render_graph_lifetime.hpp` 窄内部模板 helper，不再作为 `Impl` 成员声明
31. Phase 5-R: handle/pass/dependency label helper 拆到 `src/render_graph_labels.hpp` 窄内部模板 helper，`src/render_graph_debug.cpp` 删除，label helper 不再作为 `Impl` 成员声明
32. Phase 5-S: diagnostics snapshot resource/pass/access/dependency/transition 构造拆到 `src/render_graph_diagnostics_snapshot.hpp` 窄内部模板 helper，`RenderGraph::Impl::diagnosticsSnapshot()` 回到薄 facade
33. Phase 5-T: debug table section/list/row formatting helper 拆到 `src/render_graph_debug_tables.hpp` 窄内部模板 helper，`RenderGraph::Impl::formatDebugTables()` 回到薄 facade
34. Phase 5-U: 跨 validation/lifetime/compile 使用的 handle validation，以及 compile-only image/buffer graph validation 拆到 `src/render_graph_validation.hpp` 窄内部模板 helper；`RenderGraph::Impl` 不再暴露这些成员声明
35. Phase 5-V: dependency topo sort / cycle reporting helper 拆到 `src/render_graph_dependency_sort.hpp` 窄内部模板 helper；`RenderGraph::Impl::sortPassesByDependencies()` 成员入口移除，compile 直接调用内部排序 helper
36. Phase 5-W: active-pass culling / imported-resource write check 拆到 `src/render_graph_dependency_culling.hpp` 窄内部 helper；`RenderGraph::Impl::findActivePasses()` 成员入口移除，compile 直接调用内部 culling helper
37. Phase 5-X: dependency producer inference / add/read/build 子步骤拆到 `src/render_graph_dependency_builder.hpp` 窄内部 helper；`RenderGraph::Impl::buildDependencies()` 成员入口移除，旧 `src/render_graph_dependencies.cpp` 编译单元删除
38. Phase 5-Y: pass declaration state 从 private nested `RenderGraph::Impl::Pass` 解耦到 `src/render_graph_pass.hpp` 的 `rendergraph_internal::Pass`；pass validation 入口改为接收 image/buffer span 的 `rendergraph_internal::validatePass()`，`RenderGraph::Impl::validatePass()` 成员入口移除
39. Phase 5-Z: public pass execution context/callback 拆到 `render_graph_pass_context.hpp`；`render_graph_execution.hpp` 收窄为 schema/executor registry，内部 `render_graph_pass.hpp` 不再为存 callback 依赖 registry 头
40. Phase 5-AA: `src/render_graph_validation.hpp` 改为 forward declare `RenderGraphSchemaRegistry`，不再为 validation 入口声明依赖完整 execution registry 头；`render_graph_internal.hpp` 移除旧 command-list / string-view include 残留
41. Phase 5-AB: `render_graph_builder.hpp` 改为只依赖 `render_graph_pass_context.hpp` 并 forward declare schema/executor registry；内部 helper 头不再 include `render_graph_internal.hpp`，完整 registry 定义只留在实际调用 registry 方法的 `.cpp` / helper 头
42. Phase 5-AC: schema registry lookup 从 `render_graph_pass_queries.hpp` 拆到 `src/render_graph_schema_queries.hpp/.cpp`，让 pass query / culling / dependency / lifetime helper 头不再传递 include 完整 `render_graph_execution.hpp`
43. Phase 5-AD: dependency topo sort helper 改为接收 `std::span<const Pass>`，不再模板化接收完整 graph/Impl；cycle 诊断只消费 pass 声明与 dependency 内已记录的 resource name
44. Phase 5-AE: compile-only culled-pass materialization 改为接收 `std::span<const Pass>`，不再模板化接收完整 graph/Impl
45. Phase 5-AF: active-pass culling / imported-resource write check 改为接收 pass/image/buffer span 与 dependency 列表，不再模板化接收完整 graph/Impl
46. Phase 5-AG: dependency producer inference / add/read/build 子步骤改为接收 `DependencyBuildInputs` 的 image/buffer/pass spans，不再模板化接收完整 graph/Impl
47. Phase 5-AH: validation handle/image/buffer graph checks 与 lifetime transient allocation/declared-use/transition helper 改为接收 image/buffer/pass/compiled pass spans，不再模板化接收完整 graph/Impl
48. Phase 5-AI: diagnostics/debug label、snapshot 和 table helper 改为接收 `RenderGraphDeclarationView` 的 image/buffer/pass 只读 spans，不再模板化接收完整 graph/Impl
49. Phase 5-AJ: diagnostics facade 的 declaration view 构造收敛到 `makeRenderGraphDeclarationView()`，复用入口不命名 private nested `RenderGraph::Impl`
50. Phase 5-AK: pass/resource query helper 与 validation-only slot/schema/access helper 改为接收具体内部 `Pass`，不再保留泛型 `template <typename Pass>` 入口
51. Phase 5-AL: public diagnostics facade 实现下沉到 `render_graph_diagnostics.cpp`，`RenderGraph::Impl` 不再声明 diagnostics snapshot / debug table 成员入口
52. Phase 5-AM: compile/execute 行为入口下沉到 `render_graph_operations.hpp` 声明的内部 operation；`RenderGraph::Impl` 不再声明 compile/execute 成员入口，只保留 image/buffer/pass 声明容器
53. Phase 5-AN: public compile facade 移入 `render_graph_compile.cpp`，public execute facade 移入 `render_graph_execution.cpp`；`render_graph.cpp` 只保留 `RenderGraph` 生命周期 / copy / move 实现
54. Phase 5-AO: dependency producer inference / add/read/build 实现从 `src/render_graph_dependency_builder.hpp` 下沉到 `src/render_graph_dependency_builder.cpp`；内部头只保留 `DependencyBuildInputs` 与 `buildDependencies()` 声明，并继续只消费 image/buffer/pass spans
55. Phase 5-AP: dependency topo sort / cycle reporting 实现从 `src/render_graph_dependency_sort.hpp` 下沉到 `src/render_graph_dependency_sort.cpp`；内部头只保留 `sortPassesByDependencies()` 声明，并继续只消费 pass span 与 dependency 列表
56. Phase 5-AQ: transient allocation、declared-use 与 transition 实现从 `src/render_graph_lifetime.hpp` 下沉到 `src/render_graph_lifetime.cpp`；内部头只保留 lifetime helper 声明，并继续只消费 image/buffer/pass/compiled pass spans
57. Phase 5-AR: active-pass culling / imported-resource write check 实现从 `src/render_graph_dependency_culling.hpp` 下沉到 `src/render_graph_dependency_culling.cpp`；内部头只保留 `findActivePasses()` 声明，并继续只消费 pass/image/buffer spans 与 dependency 列表
58. Phase 5-AS: handle validation 与 image/buffer graph validation 实现从 `src/render_graph_validation.hpp` 下沉到 `src/render_graph_validation.cpp`；内部头只保留 validation 入口声明并 forward declare 内部 `Pass` / schema registry，不再 include `render_graph_pass.hpp`
59. Phase 5-AT: debug table section/list/row formatting 实现从 `src/render_graph_debug_tables.hpp` 下沉到 `src/render_graph_debug_tables.cpp`；内部头只保留 `formatDebugTables()` 声明，并 forward declare declaration view / compiled result
60. Phase 5-AU: diagnostics snapshot resource/pass/access/dependency/transition 构造实现从 `src/render_graph_diagnostics_snapshot.hpp` 下沉到 `src/render_graph_diagnostics_snapshot.cpp`；内部头只保留 `makeDiagnosticsSnapshot()` 声明，并 forward declare declaration view / diagnostics snapshot / compiled result
61. Phase 5-AV: pass/resource query 与 transition value helper 实现从 `src/render_graph_pass_queries.hpp` 下沉到 `src/render_graph_pass_queries.cpp`；内部头只保留跨 compile/dependency/lifetime 使用的查询/转换声明，并 forward declare 内部 `Pass` / schema registry
62. Phase 5-AW: handle/pass/dependency label helper 只被 debug table formatting 使用，已折回 `src/render_graph_debug_tables.cpp` 文件局部实现，并删除 `src/render_graph_labels.hpp`
63. Phase 5-AX: `RenderGraphDeclarationView` factory 实现从 `src/render_graph_declaration_view.hpp` 下沉到 `src/render_graph_declaration_view.cpp`；内部头只保留 view/factory 声明，并通过 `render_graph_pass.hpp` 保持 `std::span<const Pass>` 自包含
64. Phase 5-AY: schema/command/required-slot validation helper 从 `render_graph_validation.cpp` 拆到 `src/render_graph_schema_validation.hpp/.cpp`；`render_graph_validation.cpp` 只保留 handle/image/buffer 与 pass slot/access validation，`validatePass()` 通过 `validatePassSchema()` 调用 schema gate
65. Phase 5-AZ: pass slot/access/stage validation helper 从 `render_graph_validation.cpp` 拆到 `src/render_graph_slot_validation.hpp/.cpp`；`render_graph_validation.cpp` 只保留 image/buffer handle/final-state validation 与 `validatePass()` 薄编排，pass validation 通过 `validatePassSlots()` + `validatePassSchema()` 组合
66. Phase 5-BA: image slot validation 与 buffer slot validation 分别从 `render_graph_slot_validation.cpp` 拆到 `src/render_graph_image_slot_validation.hpp/.cpp` 和 `src/render_graph_buffer_slot_validation.hpp/.cpp`；`render_graph_slot_validation.cpp` 只保留跨 image/buffer 的 duplicate slot name check 与 `validatePassSlots()` 编排
67. Phase 5-BB: compiled pass materialization 与 per-pass transition append 从 `render_graph_compile.cpp` 拆到 `src/render_graph_compiled_pass.hpp/.cpp`；`render_graph_compile.cpp` 保留 validate / dependency / culling / sort / final-transient summary 的 compile 编排
68. Phase 5-BC: image dependency producer inference 与 buffer dependency producer inference 分别从 `render_graph_dependency_builder.cpp` 拆到 `src/render_graph_image_dependency_builder.hpp/.cpp` 和 `src/render_graph_buffer_dependency_builder.hpp/.cpp`；`render_graph_dependency_builder.cpp` 只保留 dependency build 编排入口
69. Phase 5-BD: diagnostics pass/command/access/before-transition node 构造从 `render_graph_diagnostics_snapshot.cpp` 拆到 `src/render_graph_diagnostics_pass_snapshot.hpp/.cpp`；`render_graph_diagnostics_snapshot.cpp` 保留 resource/dependency/final-transition 与 snapshot 总装
70. Phase 5-BE: debug table slot/command/transition/transient 明细表从 `render_graph_debug_tables.cpp` 拆到 `src/render_graph_debug_detail_tables.hpp/.cpp`；当时 `render_graph_debug_tables.cpp` 保留 resource/pass/dependency/culled pass 总览与 Markdown 总装，后续 5-BH 继续拆出总览表实现
71. Phase 5-BF: resource slot/required-slot schema 校验与 command whitelist schema 校验从 `render_graph_schema_validation.cpp` 分别拆到 `src/render_graph_resource_schema_validation.hpp/.cpp` 和 `src/render_graph_command_schema_validation.hpp/.cpp`；`render_graph_schema_validation.cpp` 保留 schema lookup、params contract 与 gate 编排
72. Phase 5-BG: graph resource declaration 与 pass declaration 实现从 `render_graph_builder.cpp` 分别拆到 `src/render_graph_resource_declarations.cpp` 和 `src/render_graph_pass_declarations.cpp`；`render_graph_builder.cpp` 保留 `PassBuilder` fluent mutation 实现
73. Phase 5-BH: debug table resource/pass/dependency/culled pass 总览表从 `render_graph_debug_tables.cpp` 拆到 `src/render_graph_debug_summary_tables.hpp/.cpp`；`render_graph_debug_tables.cpp` 只保留 summary/detail table Markdown 拼装顺序
74. Phase 5-BI: image/buffer 同一 resource 的 access conflict validation 从 slot validation 文件拆到 `src/render_graph_image_access_validation.hpp/.cpp` 与 `src/render_graph_buffer_access_validation.hpp/.cpp`；image/buffer slot validation 文件只保留 name/handle/shader-stage 基础校验

**当前结构**:
```
render_graph_types.hpp      — 纯数据契约，无内部依赖
render_graph_command_list.hpp — command summary accumulator，只依赖 types
render_graph_pass_context.hpp — pass execution context 与 callback，只依赖 types/core result
render_graph_execution.hpp  — schema/executor registry，只依赖 types/pass context
render_graph_builder.hpp    — RenderGraph/PassBuilder 声明与模板 builder 入口，只 forward declare schema/executor registry
render_graph_compile.hpp    — 编译产物，只依赖 types
render_graph_diagnostics.hpp — diagnostics snapshot，只依赖 compile/types
render_graph.hpp            — aggregate header，兼容旧 include
src/render_graph.cpp        — RenderGraph 生命周期、copy/move facade
src/render_graph_compile.cpp — public compile facade 与 compile operation 实现
src/render_graph_compiled_pass.hpp/.cpp — compiled pass materialization 与 per-pass image/buffer transition append 窄内部入口，只消费 pass、declaration spans、schema registry pointer 与 current access vectors
src/render_graph_execution.cpp — public execute facade 与 execute operation 实现
src/render_graph_diagnostics.cpp — public diagnostics facade 与 diagnostics/debug helper 调度
src/render_graph_operations.hpp — compile/execute 内部 operation 入口，只消费 declaration view 与 registry pointers
src/render_graph_builder.cpp — 非模板 `PassBuilder` fluent mutation API
src/render_graph_resource_declarations.cpp — graph image/buffer import 与 transient declaration API
src/render_graph_pass_declarations.cpp — graph pass declaration API 与声明期 pass 默认状态构造
src/render_graph_command_list.cpp — command summary accumulator 实现
src/render_graph_registry.cpp — schema/executor registry 实现
src/render_graph_dependency_builder.hpp — dependency build 入口声明，只暴露 DependencyBuildInputs 与 buildDependencies()，只消费 image/buffer/pass spans
src/render_graph_dependency_builder.cpp — dependency build 编排入口，组合 image/buffer dependency producer inference
src/render_graph_image_dependency_builder.hpp/.cpp — image dependency producer inference 与 add/read/build 实现，只消费 DependencyBuildInputs spans
src/render_graph_buffer_dependency_builder.hpp/.cpp — buffer dependency producer inference 与 add/read/build 实现，只消费 DependencyBuildInputs spans
src/render_graph_dependency_culling.hpp — active-pass culling 入口声明，只暴露 findActivePasses()，只消费 pass/image/buffer spans 与 dependency 列表
src/render_graph_dependency_culling.cpp — active-pass culling 与 imported-resource write check 实现
src/render_graph_dependency_sort.hpp — dependency topo sort 入口声明，只暴露 sortPassesByDependencies()，只消费 pass span 与 dependency 列表
src/render_graph_dependency_sort.cpp — dependency topo sort 与 cycle reporting 实现
src/render_graph_debug_names.hpp/.cpp — enum/access/command 文本化内部 helper
src/render_graph_declaration_view.hpp — diagnostics/debug helper 使用的 image/buffer/pass 只读 declaration view 与 factory 声明，保持 `std::span<const Pass>` 自包含
src/render_graph_declaration_view.cpp — declaration view factory 实现，集中构造 image/buffer/pass 只读 spans
src/render_graph_debug_tables.hpp — debug table formatting 入口声明，只 forward declare declaration view 与 compiled result
src/render_graph_debug_tables.cpp — debug table Markdown 总装入口，组合 summary/detail table helpers
src/render_graph_debug_summary_tables.hpp/.cpp — debug table resource/pass/dependency/culled pass 总览表实现，只消费 declaration view 与 compiled result
src/render_graph_debug_detail_tables.hpp/.cpp — debug table slot/command/transition/transient 明细表实现，只消费 declaration view 与 compiled result
src/render_graph_diagnostics_snapshot.hpp — diagnostics snapshot 构造入口声明，只 forward declare declaration view / diagnostics snapshot / compiled result
src/render_graph_diagnostics_snapshot.cpp — diagnostics snapshot resource/dependency/final-transition 与 snapshot 总装实现，只消费 declaration view 与 compiled result
src/render_graph_diagnostics_pass_snapshot.hpp/.cpp — diagnostics pass/command/access/before-transition node 构造实现，只消费 declaration view 与 compiled result
src/render_graph_pass.hpp — graph pass 声明期状态，避免 validation/dependency helper 直接依赖 private nested `Impl::Pass`
src/render_graph_pass_queries.hpp — pass/resource 查询与 transition value 入口声明，只 forward declare 内部 `Pass` / schema registry，不传递 include execution registry
src/render_graph_pass_queries.cpp — pass/resource 查询、schema-backed culling/side-effect 查询与 transition value helper 实现，只消费内部 `Pass` / compiled pass
src/render_graph_schema_queries.hpp/.cpp — schema registry lookup 窄内部入口，完整 registry 定义仅在 `.cpp` 使用
src/render_graph_schema_validation.hpp/.cpp — pass schema validation 编排入口，只消费内部 `Pass` 与 schema registry，并组合 resource/command schema gates
src/render_graph_resource_schema_validation.hpp/.cpp — resource slot allowed/required schema gate，只消费内部 `Pass` 与 `RenderGraphPassSchema`
src/render_graph_command_schema_validation.hpp/.cpp — command whitelist schema gate，只消费内部 `Pass` 与 `RenderGraphPassSchema`
src/render_graph_image_slot_validation.hpp/.cpp — image slot name/handle/shader-stage 基础校验窄内部入口，只消费 image spans 与内部 `Pass`
src/render_graph_buffer_slot_validation.hpp/.cpp — buffer slot name/handle/shader-stage 基础校验窄内部入口，只消费 buffer spans 与内部 `Pass`
src/render_graph_image_access_validation.hpp/.cpp — image 同一 resource access conflict validation，只消费 image spans 与内部 `Pass`
src/render_graph_buffer_access_validation.hpp/.cpp — buffer 同一 resource access conflict validation，只消费 buffer spans 与内部 `Pass`
src/render_graph_slot_validation.hpp/.cpp — pass slot/access/stage validation 编排入口，只消费 image/buffer spans 与内部 `Pass`，组合 image/buffer slot validation、access conflict validation 与跨资源 duplicate slot name check
src/render_graph_lifetime.hpp — transient lifetime/resource transition 入口声明，只消费 image/buffer/pass/compiled pass spans，不依赖 render_graph_internal
src/render_graph_lifetime.cpp — transient allocation、declared-use 与 transition 实现
src/render_graph_validation.hpp — pass/handle/image/buffer validation 入口声明，只消费 image/buffer spans；仅 forward declare schema registry 与内部 Pass
src/render_graph_validation.cpp — image/buffer handle/final-state validation 与 pass validation 薄编排；slot/access gate 委托 `validatePassSlots()`，schema validation gate 委托 `validatePassSchema()`
```

**待完成 (Phase 5+)**:
- 后续新增 cache、alias、multi-queue 或 unsafe/native pass 前，优先保持窄头自包含测试与 include 边界
- 保持 `render_graph_internal.hpp` 只承载 PImpl 容器，不再回填 compile/execute/validation/dependency/lifetime/diagnostics helper 声明

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
- `EditorContext` 曾暴露 panel registry、event queue、diagnostics、frame debugger、i18n、settings、workspace 和 tools；
  当前过渡 facade 已删除，调用点改为显式服务参数
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
3. `editor_app.cpp` 继续保留 bootstrap 和 frame loop 入口，不再承载大块 smoke validation

**已修复 (Step 2c-a+2c-b+2c-c+2c-d+2c-e+2c-f)**:
1. `prepareWindow()` 已改用只暴露 `ui` 的 `EditorPanelWindowContext`
2. 顶层 `ImGuiEditorPanel::draw()` 虚接口已改用 `EditorPanelDrawContext`；`EditorPanelRegistry::drawPanels()`
   是从顶层 `EditorFrameContext` 到 panel draw capability context 的唯一适配点
3. `EditorPanelDrawContext` 已拆成 viewport / diagnostics / settings / tools category context；内置 panel
   通过 category-specific base class 只覆写对应窄 context
4. 宽 `EditorPanelDrawContext` 的字段布局已收回到 `editor_panel.cpp`，public header 只暴露 opaque dispatch type
   和 category context
5. diagnostics category 已继续拆成 `EditorLogPanelDrawContext` / `EditorRenderGraphPanelDrawContext` /
   `EditorFrameDebuggerPanelDrawContext`，Log、Live RG 和 Frame Debugger 不再共享 diagnostics/input/renderGraph 大包
6. viewport / settings / tools category 已继续拆成 `EditorSceneViewPanelDrawContext` /
   `EditorSettingsPanelDrawContext` / `EditorUiStylePreviewPanelDrawContext`，Scene View、Editor Settings
   和 UI Style Preview 不再共享 category 大包
7. 当前内置 panel 的 `draw()` 实现会继续映射为 panel-local context，helper 不再传播完整 frame context

**已修复 (Step 2b-d)**:
1. `imgui_editor_shell` public API 已改为 `EditorDockspaceContext` / `EditorMenuContext` /
   `EditorCommandBarContext` / `EditorStatusBarContext`
2. shell 不再 include 或接收完整 `EditorContext`；`editor_app.cpp` 是当前 shell context 的适配点
3. 菜单和命令栏 action binding 只消费 `EditorActionInvokeContext`、i18n、panel/tool/workspace/frame debugger
   等窄能力
4. status bar 只接收 `EditorFrameUiContext`，不再为了显示 frame index / extent 而持有完整
   `EditorFrameContext`

**已修复 (Step 2b-e)**:
1. `EditorActionServices` 已接管 action invoke 所需的 event queue、panel registry、frame debugger 和 workspace
2. `EditorContext` 不再暴露 `actionInvokeContext()`，也不再持有 panel registry / frame debugger
3. shell、shortcut、Frame Debugger smoke 和 registration smoke 都通过 action-only services 构造
   `EditorActionInvokeContext`

**已修复 (Step 2b-f)**:
1. 删除 `apps/editor/src/editor_context.hpp/.cpp` 和 CMake 源条目
2. `editor_app.cpp` 的 shell、frame context 和 diagnostics append 不再经由 `EditorContext` 转发服务
3. settings smoke 改为直接接收 `EditorSettingsController` 与 `EditorI18n`

**已修复 (Step 2b-g)**:
1. 新增 `apps/editor/src/editor_app_config.hpp/.cpp`
2. run path、smoke layout/settings isolation、i18n resource directory 和 locale env 解析不再内联在
   `editor_app.cpp`
3. `editor_app_config` 只返回配置值，不聚合 editor services，避免形成新的 app context facade

**已修复 (Step 2b-h)**:
1. 新增 `apps/editor/src/editor_vulkan_host.hpp/.cpp`
2. renderable-window wait、Vulkan context/frame-loop creation、swapchain extent readiness 和一帧
   RenderView/ImGui submission glue 不再内联在 `editor_app.cpp`
3. `editor_vulkan_host` 不持有 panel/action/settings 等 editor service，只接收当前帧所需引用

**已修复 (Step 2b-i)**:
1. 新增 `apps/editor/src/editor_shell_host.hpp/.cpp`
2. shell capability context 适配不再内联在 `editor_app.cpp`
3. `editor_shell_host` 只接收 shell 绘制当前帧所需引用，不接收完整 `EditorFrameContext`，
   不聚合 app service lifetime，也不记录 renderer/Vulkan commands

**已修复 (Step 2b-j)**:
1. 新增 `apps/editor/src/editor_loop_host.hpp/.cpp`
2. 主循环、每帧 `EditorFrameContext` 构造、panel draw dispatch、ImGui frame begin/end 顺序、input/shortcut routing 和 smoke loop state
   不再内联在 `editor_app.cpp`
3. `editor_loop_host` 仍通过显式参数接收当前运行所需服务，不重新引入宽 app context / service locator facade

**已修复 (Step 2b-k)**:
1. `registerEditorAppRegistries()` 接管 panel/action/tool app-level registration 和 panel event queue 绑定，
   `editor_app.cpp` 不再手写三段 registry bootstrap
2. 新增 `apps/editor/src/editor_app_run_completion.hpp/.cpp`
3. post-loop viewport/input/shortcut/layout smoke gates、layout save、viewport/imgui shutdown 和 run summary 输出
   不再内联在 `editor_app.cpp`

**已修复 (Step 2b-l)**:
1. `validateEditorStartupGates()` 接管 startup、registration 和 command smoke gates
2. `editor_smoke_validation.hpp` 不再向 app 层暴露三段零散 startup/registration/command smoke helper

**已修复 (Step 2b-m)**:
1. 新增 `apps/editor/src/editor_app_services.hpp/.cpp`
2. editor event queue、diagnostics log、frame debugger、i18n、settings/workspace controller、
   panel/action/tool registry 和 `EditorActionServices` construction 已从 `editor_app.cpp` 拆出
3. `EditorAppServices` 禁止 copy/move，避免内部 reference 聚合被按值搬迁后悬挂
4. `editor_app.cpp` 只保留 window/Vulkan/ImGui/renderer/viewport 与 app service bundle 的运行编排

**已修复 (Step 2b-n)**:
1. 新增 `apps/editor/src/editor_render_runtime.hpp/.cpp`
2. ImGui runtime、`BasicFullscreenTextureRenderer` 和 `EditorViewportCoordinator` 创建与拥有关系已从
   `editor_app.cpp` 拆出
3. `EditorRenderRuntime` 不接收 editor service bundle，也不向 panel/action/smoke helper 传播新的宽 context；
   `runEditorLoop()` 仍显式接收 renderer / viewport capability 引用
4. `editor_app.cpp` 不再直接 include renderer-basic Vulkan 头或手写 viewport/render runtime 创建细节

**已修复 (Step 2b-o)**:
1. `EditorRenderRuntime` public header 改为 PImpl，只 forward declare ImGui、renderer、viewport 和 Vulkan
   运行时类型
2. `fullscreen_texture_renderer.hpp`、`editor_viewport_coordinator.hpp` 和 `imgui_runtime.hpp` 的完整依赖
   下沉到 `editor_render_runtime.cpp`
3. `editor_app.cpp` 仅因构造 `ImGuiRuntimeDesc` 显式 include `imgui_runtime.hpp`，不再经由 render runtime
   header 传递 backend-heavy 依赖

**已修复 (Step 2b-p)**:
1. 新增 `apps/editor/src/editor_command_smoke.hpp/.cpp`
2. command/transaction smoke 的 test command 与 undo/redo 验证从 `editor_smoke_validation.cpp`
   拆出，startup gate 只调用 `validateEditorCommandSmoke()`
3. `editor_smoke_validation.cpp` 不再直接 include `editor_command.hpp` 或承载 command history 测试实现

**已修复 (Step 2b-q)**:
1. 新增 `apps/editor/src/editor_registration_smoke.hpp/.cpp`
2. panel/action/tool/settings/shortcut registration smoke 从 `editor_smoke_validation.cpp`
   拆出，startup gate 只调用 `validateEditorRegistrationSmoke()`
3. `editor_smoke_validation.cpp` 继续保留 startup gate 聚合、viewport、resize、Frame Debugger、
   input/shortcut run-level 和 layout saved smoke，不再承载 registry/action routing 细节

**已修复 (Step 2b-r)**:
1. 新增 `apps/editor/src/editor_startup_smoke.hpp/.cpp`
2. layout persistence/saved、i18n、font 和 theme startup smoke 从 `editor_smoke_validation.cpp`
   拆出，startup gate 只调用 `validateEditorStartupSmoke()`
3. `editor_app_run_completion.cpp` 直接 include `editor_startup_smoke.hpp` 获取 layout saved gate，
   `editor_smoke_validation.hpp` 不再暴露 startup/layout 细节

**已修复 (Step 2b-s)**:
1. 新增 `apps/editor/src/editor_viewport_smoke.hpp/.cpp`
2. viewport presentation、overlay flags、camera/unproject、RenderView diagnostics、multi-view 和 resize
   smoke 从 `editor_smoke_validation.cpp` 拆出
3. `editor_app_run_completion.cpp` 直接 include `editor_viewport_smoke.hpp` 获取 viewport gates；
   `editor_smoke_validation.cpp` 只保留 startup gate 聚合、Frame Debugger、input 和 shortcut
   run-level smoke

**已修复 (Step 2b-t)**:
1. 新增 `apps/editor/src/editor_frame_debugger_smoke.hpp/.cpp`
2. Frame Debugger capture、preview、replay、resume 和 inspected-world safe-point smoke 从
   `editor_smoke_validation.cpp` 拆出
3. `editor_app_run_completion.cpp` 直接 include `editor_frame_debugger_smoke.hpp` 获取 frame debug gate；
   `editor_smoke_validation.cpp` 只保留 startup gate 聚合、input 和 shortcut run-level smoke

**已修复 (Step 2b-u)**:
1. 新增 `apps/editor/src/editor_shortcut_smoke.hpp/.cpp`
2. shortcut router registration/routing smoke 从 `editor_registration_smoke.cpp` 拆出，shortcut
   run-level smoke 从 `editor_smoke_validation.cpp` 拆出
3. `editor_registration_smoke.cpp` 只调用 shortcut smoke gate，不再直接 include input/shortcut router；
   `editor_smoke_validation.cpp` 只保留 startup gate 聚合和 input run-level smoke

**已修复 (Step 2b-v)**:
1. 新增 `apps/editor/src/editor_input_smoke.hpp/.cpp`
2. input router run-level smoke 从 `editor_smoke_validation.cpp` 拆出
3. `editor_app_run_completion.cpp` 直接 include `editor_input_smoke.hpp` 获取 input gate；
   `editor_smoke_validation.cpp` 只保留 startup/registration/command gate 聚合

**已修复 (Step 2b-w)**:
1. 新增 `apps/editor/src/editor_frame_debug_preview.cpp`
2. Frame Debug image preview / replay recording 从 `editor_viewport_coordinator.cpp` 拆出
3. `EditorViewportCoordinator` 保留普通 viewport request、slot、texture lifetime 与 diagnostics 编排，
   不再直接承载 Frame Debug preview 录制实现

**已修复 (Step 2b-x)**:
1. 新增 `apps/editor/src/editor_ui_widgets.cpp`
2. Editor UI section header、property table、status pill 与 color swatch 绘制 helper 从 `editor_ui.cpp` 拆出
3. `editor_ui.cpp` 保留主题 catalog、当前主题状态、color token 与 ImGui style 应用，避免同时承载通用控件绘制实现

**已修复 (Step 2b-y)**:
1. 新增 `apps/editor/src/editor_frame_debugger_replay.hpp/.cpp`
2. Frame Debug replay pass/event/image 选择、preview request consume/publish/unavailable 状态更新从 `editor_frame_debugger.cpp` 拆出
3. `editor_frame_debugger.cpp` 保留 capture/resume/fence 状态机、统计通知与只读查询；Frame Debug GPU preview 录制仍由 `editor_frame_debug_preview.cpp` 承担

**已修复 (Step 2b-z)**:
1. 新增 `apps/editor/src/panels/render_graph_snapshot_format.hpp/.cpp`
2. RenderGraph snapshot 的 enum/name、resource/pass label 与 access cell 数据整形从 `render_graph_snapshot_view.cpp` 拆出
3. `render_graph_snapshot_view.cpp` 保留 ImGui summary、timeline matrix 与 detail table 绘制；format helper 不依赖 ImGui/Vulkan

**待完成 (Step 2b)**:
- 继续保持 app service bundle 不扩展成宽 service locator；后续新增 asset browser / inspector /
  material editor 时按 capability-scoped context 接入
- 避免为方便传参重新引入宽 app context / service locator facade

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

1. **Editor app glue 仍需 capability-scoped 收敛**: command/transaction 已有基础，command/transaction smoke 已拆到 `editor_command_smoke`，registration/settings/action/tool smoke 已拆到 `editor_registration_smoke`，input run-level smoke 已拆到 `editor_input_smoke`，shortcut registration/run-level smoke 已拆到 `editor_shortcut_smoke`，startup/layout/i18n/font/theme smoke 已拆到 `editor_startup_smoke`，viewport presentation/overlay/camera/diagnostics/resize smoke 已拆到 `editor_viewport_smoke`，Frame Debugger capture/preview/replay/resume smoke 已拆到 `editor_frame_debugger_smoke`，Frame Debug replay/preview selection 已拆到 `editor_frame_debugger_replay`，Frame Debug image preview/replay GPU recording 已拆到 `editor_frame_debug_preview`，RenderGraph snapshot format helper 已拆到 `render_graph_snapshot_format`，通用 Editor UI 小控件绘制 helper 已拆到 `editor_ui_widgets`，panel `draw()` 已不再直接接收顶层 `EditorFrameContext`，宽 dispatch bundle 已是 implementation detail，内置 panel draw context 已按 Scene View / Log / Live RG / Frame Debugger / Editor Settings / UI Style Preview 拆到 per-panel context，status bar 已收窄为 `EditorFrameUiContext`，过渡期 `EditorContext` 已删除，run config helpers 已拆到 `editor_app_config`，Vulkan/window/frame submission helpers 已拆到 `editor_vulkan_host`，shell capability context 适配已拆到 `editor_shell_host`，主循环、panel dispatch 和 frame context 构造已拆到 `editor_loop_host`，startup/registration/command smoke gates 已收进 `validateEditorStartupGates()`，post-loop smoke/layout/shutdown/summary 已拆到 `editor_app_run_completion`，app service construction 已拆到 `editor_app_services`，ImGui/runtime fullscreen renderer/viewport coordinator 创建与拥有已拆到 `editor_render_runtime`，且 render runtime header 已用 PImpl 隔离 backend-heavy 依赖；后续新增 asset browser、material editor 或 inspector mutation 前需要继续防止 app service bundle 扩成 service locator。
2. **Grid 仍需 camera-aware policy 和 pixel smoke**: renderer-owned debug-line GPU pass 已落地，但 grid provider 仍是固定原点 packet，尚未按 camera range/fade 生成稳定可读网格，也未做像素/readback 级 camera-difference 验证。
3. **RenderGraph 内部 helper 声明仍需继续收敛**: public headers 已拆成 types / command / pass context / execution registry / builder / compile / diagnostics / aggregate，compile/execute operation 已下沉到 `render_graph_operations.hpp` 且 `RenderGraph::Impl` 已退为声明容器，debug name / command detail helper 已从 `RenderGraph::Impl` 成员移到内部 `render_graph_debug_names` helper，debug summary label/table helper 已收敛到 `render_graph_debug_summary_tables.cpp` 文件局部实现，diagnostics snapshot 构造声明已收敛到 `render_graph_diagnostics_snapshot.hpp`、resource/pass/access/dependency/transition 构造实现已下沉到 `render_graph_diagnostics_snapshot.cpp`，debug table formatting 声明已收敛到 `render_graph_debug_tables.hpp`、summary/detail 表格实现已分别下沉到 `render_graph_debug_summary_tables.cpp` 与 `render_graph_debug_detail_tables.cpp`，diagnostics/debug helper 通过 `RenderGraphDeclarationView` 消费 image/buffer/pass 只读 spans 而不再模板化接收完整 graph/Impl，旧 `render_graph_debug.cpp` 已删除；无状态 schema/slot/command validation helper、validation-only slot group、image/buffer access conflict message、validation-only slot/access/stage helper、execution-only callback error message、compile-only culled-pass materialization、pass/resource query 与 transition value helper、dependency producer inference 实现、dependency topo sort 实现、lifetime transient allocation/declared-use/transition 实现、active-pass culling 实现、handle/image/buffer graph validation 实现、schema/command/required-slot validation 实现、pass slot/access/stage validation 实现、image slot validation 实现、buffer slot validation 实现、image/buffer access conflict validation 实现、diagnostics snapshot 构造实现、debug table formatting 实现已收敛为文件局部或窄内部 helper，其中 culled-pass materialization、active-pass culling、pass/resource query、dependency producer inference、dependency topo sort、validation handle/image/buffer graph checks、schema/command/required-slot validation、pass slot/access/stage validation、image slot validation、buffer slot validation、image/buffer access conflict validation、diagnostics snapshot 构造、debug table formatting 与 lifetime helper 只消费 pass/image/buffer/compiled pass spans、declaration view、schema registry 或 dependency 列表而不再模板化接收完整 graph/Impl；pass declaration state 已解耦到 `render_graph_pass.hpp`，pass validation 入口已收敛到 `rendergraph_internal::validatePass()`，validation 入口声明不再依赖完整 execution registry 头，pass/resource query 与 transition value helper 声明已收敛到 `render_graph_pass_queries.hpp`、实现已下沉到 `render_graph_pass_queries.cpp`，handle 与 image/buffer graph validation 声明已收敛到 `render_graph_validation.hpp`、实现已下沉到 `render_graph_validation.cpp`，schema/command/required-slot validation 声明已收敛到 `render_graph_schema_validation.hpp`、实现已下沉到 `render_graph_schema_validation.cpp`，pass slot/access/stage validation 编排声明已收敛到 `render_graph_slot_validation.hpp`、实现已下沉到 `render_graph_slot_validation.cpp`，image slot validation 声明已收敛到 `render_graph_image_slot_validation.hpp`、实现已下沉到 `render_graph_image_slot_validation.cpp`，buffer slot validation 声明已收敛到 `render_graph_buffer_slot_validation.hpp`、实现已下沉到 `render_graph_buffer_slot_validation.cpp`，image/buffer access conflict validation 声明已分别收敛到 `render_graph_image_access_validation.hpp` 与 `render_graph_buffer_access_validation.hpp`、实现已下沉到对应 `.cpp`，diagnostics snapshot 构造声明已收敛到 `render_graph_diagnostics_snapshot.hpp`、实现已下沉到 `render_graph_diagnostics_snapshot.cpp`，debug table formatting 声明已收敛到 `render_graph_debug_tables.hpp`、总装实现保留在 `render_graph_debug_tables.cpp`，dependency producer inference 与 add/read/build 声明已收敛到 `render_graph_dependency_builder.hpp`、实现已下沉到 `render_graph_dependency_builder.cpp`，dependency topo sort 与 cycle reporting 声明已收敛到 `render_graph_dependency_sort.hpp`、实现已下沉到 `render_graph_dependency_sort.cpp`，lifetime helper 声明已收敛到 `render_graph_lifetime.hpp`、实现已下沉到 `render_graph_lifetime.cpp`，active-pass culling 与 imported-resource write check 声明已收敛到 `render_graph_dependency_culling.hpp`、实现已下沉到 `render_graph_dependency_culling.cpp`；后续复杂 compiler/cache/unsafe pass 前需要防止 helper 声明重新回流到 `RenderGraph::Impl`。
3a. **Phase 5-BC dependency builder 补充**: `render_graph_dependency_builder.cpp` 已进一步收敛为 build 编排入口，image/buffer producer inference 与 add/read/build 实现分别下沉到 `render_graph_image_dependency_builder.cpp` 和 `render_graph_buffer_dependency_builder.cpp`，避免 dependency builder 重新变成跨资源大实现单元。
3b. **Phase 5-BD diagnostics snapshot 补充**: `render_graph_diagnostics_snapshot.cpp` 已进一步收敛为 resource/dependency/final-transition 与 snapshot 总装入口，pass/command/access/before-transition node 构造下沉到 `render_graph_diagnostics_pass_snapshot.cpp`，避免 diagnostics snapshot 重新变成所有 diagnostics 节点类型的聚合实现。
3c. **Phase 5-BE debug table 补充**: `render_graph_debug_tables.cpp` 在该阶段先收敛为 resource/pass/dependency/culled pass 总览与 Markdown 总装入口，slot/command/transition/transient 明细表下沉到 `render_graph_debug_detail_tables.cpp`，避免 debug table 重新变成所有诊断表格的单一实现单元。
3d. **Phase 5-BF schema validation 补充**: `render_graph_schema_validation.cpp` 已进一步收敛为 schema lookup、params contract 与 gate 编排入口，resource slot/required-slot schema gate 下沉到 `render_graph_resource_schema_validation.cpp`，command whitelist schema gate 下沉到 `render_graph_command_schema_validation.cpp`，避免 schema validation 重新混合 pass/schema/command 三类细节。
3e. **Phase 5-BG builder facade 补充**: `render_graph_builder.cpp` 已进一步收敛为 `PassBuilder` fluent mutation 实现，graph image/buffer import 与 transient declaration 下沉到 `render_graph_resource_declarations.cpp`，pass declaration 与默认 pass 状态构造下沉到 `render_graph_pass_declarations.cpp`，避免 builder facade 重新混合 pass mutation、resource declaration 与 pass declaration。
3f. **Phase 5-BH debug summary table 补充**: `render_graph_debug_tables.cpp` 已进一步收敛为 Markdown 总装入口，resource/pass/dependency/culled pass 总览表下沉到 `render_graph_debug_summary_tables.cpp`，slot/command/transition/transient 明细表继续留在 `render_graph_debug_detail_tables.cpp`，避免 debug table 总装入口重新承担所有表格实现。
3g. **Phase 5-BI access validation 补充**: image/buffer slot validation 进一步按职责分离：`render_graph_image_slot_validation.cpp` 与 `render_graph_buffer_slot_validation.cpp` 保留 name/handle/shader-stage 基础校验，同一 resource 的 access conflict 检查下沉到 `render_graph_image_access_validation.cpp` 和 `render_graph_buffer_access_validation.cpp`，避免 slot validation 同时承担声明合法性与跨访问冲突两类逻辑。
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
  E.2 Capability-scoped EditorFrameContext / app glue 收敛 (2.1)

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
- **问题**: Fixed；pass declaration state 已转入 `src/render_graph_pass.hpp`，pass
  validation 入口已转为 `rendergraph_internal::validatePass()`；`PassBuilder` 只保留
  声明期 builder 入口。

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
- **编辑器层**: 过渡期 `EditorContext` facade 已删除；app loop 通过显式服务参数连接 shell、
  frame context、smoke validation 和 action dispatch
- **评价**:
  - `VulkanContext` / `VulkanFrameLoop` → **优秀**。真正的 Facade: 隐藏 Vulkan 1.x 样板但不限制灵活性
  - `persistence::saveObject` / `loadObject` → **优秀**。两个函数签名背后是四层子系统
  - editor app glue → **中等**。若重新聚合为宽 context，仍会向 Service Locator 退化 (见 10.2)

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
- **形态**: `EditorActionDesc` + `EditorActionCallback = std::function<void(EditorActionContext&)>`，通过 `EditorActionRegistry::invoke(actionId, makeEditorActionInvokeContext(actionServices))` 调用
- **评价**: **正确但仍是过渡阶段**。当前 action 已不接收完整 app context，但仍直接修改 panel/debug/workspace state，没有 `execute()`/`undo()` 分离。和 `EditorCommand` / `EditorTransaction` 的差距见 10.4.1
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
    virtual void prepareWindow(EditorPanelWindowContext&, EditorPanelState&) {}  // hook
    virtual void draw(EditorPanelDrawContext&, EditorPanelState&) = 0;           // opaque dispatch step
};

class ImGuiSceneViewEditorPanel : public ImGuiEditorPanel {
    void draw(EditorPanelDrawContext&, EditorPanelState&) final;                 // dispatch
    virtual void drawSceneViewPanel(EditorSceneViewPanelDrawContext&,
                                      EditorPanelState&) = 0;                   // narrow step
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

- **位置**: `main()` / `runEditorLoop()` in `apps/editor/src/editor_app.cpp`
- **形态**: editor app bootstrap 显式创建服务对象，并把所需能力按 action、shell、frame、smoke validation 等入口传入。无全局 singleton、无 magic static、无 service locator 自动解析
- **评价**: **优秀**。这是现代 C++ 的推荐做法 (和 Google C++ Style Guide 的 "Prefer dependency injection" 一致) — 比 Godot 的 `EditorNode::get_singleton()` 模式更清晰

```cpp
// 当前形态 (正确)
auto panelRegistry = EditorPanelRegistry();
auto actionRegistry = EditorActionRegistry();
auto eventQueue = EditorEventQueue();
EditorActionServices actionServices{eventQueue, panelRegistry, frameDebugger, workspace};
runEditorLoop(/* ... */, actionRegistry, actionServices, eventQueue, diagnosticsLog, i18n,
              settings, panelRegistry, toolRegistry, workspace, mode);

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
| **God Object (中等)** | `EditorViewportCoordinator` — 仍同时管理 viewport slot、texture registry、render view recording、retired texture GC 和 40+ stats；Frame Debug preview recording 已拆到 `editor_frame_debug_preview.cpp` | **P3** | Unreal `FEditorViewportClient` 有类似问题(200+ methods)，Unity `SceneView` 也被批评"太大"。但它们的 scope 都更窄——coordinator、renderer、preview 是三个不同 owner |
| **Monolithic God Class (中等)** | `RenderGraph` — 3993 行 header-only，合并 declaration、compile、execute、diagnostics、validation 等 ~50 methods | **P3** | Unreal RDG's `FRDGBuilder` 约 3000 行但源码拆分清晰 (declaration/execute/diagnostics 各自独立文件)；Unity RenderGraph 也拆成 4-5 个文件 |
| **Service Locator lite (轻)** | `editor_app.cpp` 显式服务参数仍较多；`EditorContext` 已删除，但后续若重新聚合为宽 app context 会回到同类风险 | **P3** | Unreal `UWorld` 暴露 GetWorld() 访问几乎所有子系统——但 Unreal 有 module 边界限制，你的 app glue 当前应继续用 capability context 控制边界。Unity `EditorApplication` 作为 static facade 也是宽接口 |
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
  Explicit DI             (editor app service parameters)
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

**阻塞**: 需要继续把 app glue 的显式服务参数收敛到 command/transaction owner 和 capability-scoped context (问题 2.1)。

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

**当前状态**: `EditorEventQueue` 已经是 Observer 变体，但在 editor 子系统之间的状态变化通知仍然通过 polling (panel 每帧检查 `viewportCoordinator` 状态) 或 app glue 显式服务调用。

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
- 不再引入新的宽 app/editor context；超过一个稳定消费者时拆 capability context 或局部 owner
- `RenderGraph` 在 5000 行或增加 cache/alias/multi-queue 前必须拆 API/implementation
- `EditorViewportCoordinator` 在增加 gizmo/selection 后必须拆
